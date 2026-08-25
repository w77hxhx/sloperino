// SPDX-License-Identifier: MIT

#include "providers/limerino/nuke/NukeExecutor.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/kick/KickApi.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/limerino/LimerinoApi.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringBuilder>

#include <algorithm>
#include <atomic>

namespace chatterino::limerino {

QPointer<NukeExecutor> &NukeExecutor::active()
{
    static QPointer<NukeExecutor> inst;
    return inst;
}

std::optional<NukeExecutor::LastRun> &NukeExecutor::lastRun()
{
    static std::optional<LastRun> run;
    return run;
}

NukeExecutor::NukeExecutor(ChannelPtr channel, NukePlan plan, NukeAction action,
                           int timeoutSeconds, const QString &reason,
                           QObject *parent)
    : QObject(parent)
    , channel_(std::move(channel))
    , plan_(std::move(plan))
    , action_(action)
    , timeoutSeconds_(timeoutSeconds)
    , reason_(reason)
{
}

NukeExecutor::~NukeExecutor()
{
    if (NukeExecutor::active() == this)
    {
        NukeExecutor::active() = nullptr;
    }
}

void NukeExecutor::buildQueue()
{
    this->queue_.clear();

    auto addUserOp = [&](OpKind kind) {
        for (const auto &t : this->plan_.targets)
        {
            Operation op{kind, t.userId, t.login, QString(), 0};
            // Kick messages carry the numeric sender id as Message::userID.
            if (!t.userId.isEmpty())
            {
                op.targetKickId = t.userId.toULongLong();
            }
            this->queue_.append(op);
        }
    };
    auto addDeleteOps = [&] {
        for (const auto &id : this->plan_.messageIds)
        {
            this->queue_.append(
                Operation{OpKind::Delete, QString(), QString(), id, 0});
        }
    };

    switch (this->action_)
    {
        case NukeAction::Ban:
            addUserOp(OpKind::Ban);
            break;
        case NukeAction::Timeout:
            addUserOp(OpKind::Timeout);
            break;
        case NukeAction::Warn:
            addUserOp(OpKind::Warn);
            break;
        case NukeAction::Delete:
            addDeleteOps();
            break;
        case NukeAction::DeleteAndTimeout:
            addDeleteOps();
            addUserOp(OpKind::Timeout);
            break;
    }
}

void NukeExecutor::start()
{
    if (this->running_)
    {
        return;
    }

    // Only one nuke at a time globally.
    if (auto prev = NukeExecutor::active(); prev && prev != this)
    {
        prev->cancel();
    }

    this->buildQueue();

    if (this->queue_.isEmpty())
    {
        Q_EMIT progress(0, 0, 0, 0);
        Q_EMIT finished(false, {});
        return;
    }

    NukeExecutor::active() = this;
    this->running_ = true;
    this->cursor_ = 0;
    this->succeeded_ = 0;
    this->failed_ = 0;
    this->cancelled_ = false;

    this->dispatchNext();
}

void NukeExecutor::cancel()
{
    if (this->running_)
    {
        this->cancelled_ = true;
    }
    if (NukeExecutor::active() == this)
    {
        NukeExecutor::active() = nullptr;
    }
}

void NukeExecutor::reportFailure(const QString &context, const QString &message)
{
    ++this->failed_;
    this->failures_.append(context % QStringLiteral(": ") % message);
}

void NukeExecutor::settle()
{
    Q_EMIT progress(this->cursor_, static_cast<int>(this->queue_.size()),
                    this->succeeded_, this->failed_);

    if (this->cancelled_ || this->cursor_ >= this->queue_.size())
    {
        this->running_ = false;

        // Retain the run so the dialog can offer Undo. Deletes are recorded
        // but not reversible — the dialog must state that.
        if (!this->cancelled_ && this->cursor_ >= this->queue_.size())
        {
            NukeExecutor::rememberRun();
        }

        Q_EMIT finished(this->cancelled_, this->failures_);
        return;
    }

    this->dispatchNext();
}

void NukeExecutor::rememberRun()
{
    // Only a completed (non-cancelled) run is ever retained.
    LastRun run;
    if (this->channel_)
    {
        run.channelName = this->channel_->getName();
        run.channelKey = [this] {
            const auto &c = this->channel_;
            if (dynamic_cast<KickChannel *>(c.get()))
            {
                return QStringLiteral("kick:") + c->getName().toLower();
            }
            return QStringLiteral("twitch:") + c->getName().toLower();
        }();
    }
    run.channelBroadcasterId = [this] {
        if (auto *t = dynamic_cast<TwitchChannel *>(this->channel_.get()))
        {
            return t->roomId();
        }
        if (auto *k = dynamic_cast<KickChannel *>(this->channel_.get()))
        {
            return QString::number(k->userID());
        }
        return QString();
    }();
    run.isKick = dynamic_cast<KickChannel *>(this->channel_.get()) != nullptr;
    run.operatorModeratorId =
        getApp()->getAccounts()->twitch.getCurrent()->getUserId();
    run.action = this->action_;
    run.timeoutSeconds = this->timeoutSeconds_;
    run.reason = this->reason_;
    run.succeeded = this->succeededUserOps_;
    run.deletedIds = this->succeededDeletes_;
    run.finishedAt = QDateTime::currentDateTime();

    NukeExecutor::lastRun() = std::move(run);
}

void NukeExecutor::dispatchTwitch(TwitchChannel *chan, const Operation &op)
{
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    const QString broadcasterID = chan->roomId();
    const QString moderatorID = currentUser->getUserId();
    const QString bucketKey = QStringLiteral("nuke:") + broadcasterID;

    // Raw this* in these two callbacks would be a use-after-free if the
    // executor is deleted while an HTTP request is still in flight.
    QPointer<NukeExecutor> self(this);

    auto handleOk = [self, op] {
        if (!self)
        {
            return;
        }
        self->inFlight_ = false;
        ++self->succeeded_;
        if (op.kind == OpKind::Delete)
        {
            self->succeededDeletes_.append(op.messageId);
        }
        else if (op.kind != OpKind::Warn)
        {
            self->succeededUserOps_.append(
                NukeTarget{op.targetUserId, op.targetLogin, op.targetLogin, 0});
        }
        self->settle();
    };
    auto handleErr = [self](const QString &label, const QString &message) {
        if (!self)
        {
            return;
        }
        self->inFlight_ = false;
        self->reportFailure(label, message);
        self->settle();
    };

    switch (op.kind)
    {
        case OpKind::Ban:
        case OpKind::Timeout: {
            const QString label = (op.kind == OpKind::Ban
                                       ? QStringLiteral("ban")
                                       : QStringLiteral("timeout")) %
                                  QStringLiteral(" ") % op.targetLogin;
            LimerinoApi::banUser(
                broadcasterID, moderatorID, op.targetUserId,
                op.kind == OpKind::Timeout
                    ? std::optional<int>(this->timeoutSeconds_)
                    : std::nullopt,
                this->reason_, bucketKey, handleOk,
                [handleErr, label](const QString &msg) {
                    handleErr(label, msg);
                });
        }
        break;

        case OpKind::Warn: {
            const QString label = QStringLiteral("warn ") % op.targetLogin;
            LimerinoApi::warnUser(
                broadcasterID, moderatorID, op.targetUserId, this->reason_,
                bucketKey, handleOk, [handleErr, label](const QString &msg) {
                    handleErr(label, msg);
                });
        }
        break;

        case OpKind::Delete: {
            const QString label =
                QStringLiteral("delete msg ") % op.messageId;
            LimerinoApi::deleteChatMessage(
                broadcasterID, moderatorID, op.messageId, bucketKey, handleOk,
                [handleErr, label](const QString &msg) {
                    handleErr(label, msg);
                });
        }
        break;
    }
}

void NukeExecutor::dispatchKick(KickChannel *chan, const Operation &op)
{
    auto *api = getKickApi();
    const auto broadcasterUserID = chan->userID();

    auto handleOk = [this, op] {
        this->inFlight_ = false;
        ++this->succeeded_;
        if (op.kind == OpKind::Delete)
        {
            this->succeededDeletes_.append(op.messageId);
        }
        else if (op.kind != OpKind::Warn)
        {
            this->succeededUserOps_.append(
                NukeTarget{op.targetUserId, op.targetLogin, op.targetLogin, 0});
        }
        this->settle();
    };
    auto handleErr = [this](const QString &label, const QString &msg) {
        this->inFlight_ = false;
        this->reportFailure(label, msg);
        this->settle();
    };

    switch (op.kind)
    {
        case OpKind::Ban:
        case OpKind::Timeout: {
            const QString label = (op.kind == OpKind::Ban
                                       ? QStringLiteral("ban")
                                       : QStringLiteral("timeout")) %
                                  QStringLiteral(" ") % op.targetLogin;
            std::optional<std::chrono::minutes> dur;
            if (op.kind == OpKind::Timeout)
            {
                // Kick has a 60 s floor; clamp, never clamp to zero.
                const int secs = std::max(this->timeoutSeconds_, 60);
                dur = std::chrono::round<std::chrono::minutes>(
                    std::chrono::seconds(secs));
            }
            api->banUser(broadcasterUserID, op.targetKickId, dur,
                         this->reason_,
                         [handleOk, handleErr, label](const auto &res) {
                             if (res)
                             {
                                 handleOk();
                             }
                             else
                             {
                                 handleErr(label, res.error());
                             }
                         });
        }
        break;

        case OpKind::Warn:
            // Kick has no warn endpoint. The plan zeroes targets on Kick, so
            // we never land here in practice — double-fail safe.
            handleErr(QStringLiteral("warn ") % op.targetLogin,
                      QStringLiteral("not available on Kick"));
            break;

        case OpKind::Delete: {
            const QString label =
                QStringLiteral("delete msg ") % op.messageId;
            api->deleteChatMessage(
                op.messageId, [handleOk, handleErr, label](const auto &res) {
                    if (res)
                    {
                        handleOk();
                    }
                    else
                    {
                        handleErr(label, res.error());
                    }
                });
        }
        break;
    }
}

void NukeExecutor::dispatchNext()
{
    if (!this->channel_)
    {
        this->reportFailure(QStringLiteral("nuke"),
                            QStringLiteral("channel disappeared"));
        this->settle();
        return;
    }

    const auto &op = this->queue_[this->cursor_];
    ++this->cursor_;
    this->inFlight_ = true;

    if (auto *twit = dynamic_cast<TwitchChannel *>(this->channel_.get()))
    {
        auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
        if (!currentUser || currentUser->isAnon())
        {
            this->inFlight_ = false;
            this->reportFailure(QStringLiteral("nuke"),
                                QStringLiteral("not logged in"));
            this->settle();
            return;
        }
        this->dispatchTwitch(twit, op);
        return;
    }
    if (auto *kick = dynamic_cast<KickChannel *>(this->channel_.get()))
    {
        this->dispatchKick(kick, op);
        return;
    }

    this->inFlight_ = false;
    this->reportFailure(QStringLiteral("nuke"),
                        QStringLiteral("unsupported channel type"));
    this->settle();
}

QString cancelRunningNuke()
{
    auto exec = NukeExecutor::active();
    if (!exec)
    {
        return QStringLiteral("No nuke is currently running.");
    }
    exec->cancel();
    exec->deleteLater();
    return QStringLiteral("Nuke cancelled.");
}

QString undoLastNuke(const std::shared_ptr<Channel> &channel)
{
    auto &slot = NukeExecutor::lastRun();
    if (!slot.has_value())
    {
        return QStringLiteral("No completed nuke to undo.");
    }
    if (!channel)
    {
        return QStringLiteral("No channel here.");
    }

    const auto run = *slot;

    // Same-channel check: undo only reverses the run it was issued from —
    // prevents an in-channel /unnuke from touching a different channel.
    const QString thisKeyPrefix =
        dynamic_cast<KickChannel *>(channel.get()) != nullptr
            ? QStringLiteral("kick:")
            : QStringLiteral("twitch:");
    const QString thisKey =
        thisKeyPrefix + channel->getName().toLower();
    if (!thisKey.isEmpty() && run.channelKey != thisKey)
    {
        return QStringLiteral(
            "The most recent nuke was in a different channel (#%1); "
            "nothing was reversed here.")
            .arg(run.channelName);
    }

    if (!run.isKick && run.succeeded.isEmpty() && run.deletedIds.isEmpty())
    {
        return QStringLiteral("Last nuke had no reversible action.");
    }

    // Only the most recent run can be undone — a single undo clears the slot
    // so Undo is never repeatable against stale state.
    slot.reset();

    switch (run.action)
    {
        case NukeAction::Ban:
        case NukeAction::Timeout:
        case NukeAction::DeleteAndTimeout: {
            const QString bucketKey =
                QStringLiteral("nuke-undo:") + run.channelBroadcasterId;
            for (const auto &t : run.succeeded)
            {
                if (run.isKick)
                {
                    getKickApi()->unbanUser(
                        run.channelBroadcasterId.toULongLong(),
                        t.login.toULongLong(), [](const auto &) {});
                }
                else
                {
                    LimerinoApi::unbanUser(run.channelBroadcasterId,
                                           run.operatorModeratorId, t.userId,
                                           bucketKey, [] {},
                                           [](const QString &) {});
                }
            }
            const int n = run.succeeded.size();
            const int d = run.deletedIds.size();
            return QStringLiteral(
                       "Undoing last nuke (%1 user(s) unbanned/untimed-out)%2")
                .arg(n)
                .arg(d > 0 ? QStringLiteral(", %1 deletions NOT reversible")
                                 .arg(d)
                           : QString());
        }

        case NukeAction::Warn: {
            return QStringLiteral("Warns cannot be undone.");
        }

        case NukeAction::Delete: {
            return QStringLiteral(
                "Deletes cannot be undone. Nothing was reversed.");
        }
    }
    return {};
}

}  // namespace chatterino::limerino
