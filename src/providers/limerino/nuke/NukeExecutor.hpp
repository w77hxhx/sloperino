// SPDX-License-Identifier: MIT
// Serial execution of a NukePlan through the rate-limiting Helix wrappers in
// LimerinoApi (batch N3). One operation in flight; progress signals for the
// dialog; cancel() stops dispatch after the current in-flight request settles.

#pragma once

#include "providers/limerino/nuke/NukePlan.hpp"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

class Channel;
class TwitchChannel;
class KickChannel;
using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace chatterino

namespace chatterino::limerino {

class NukeExecutor : public QObject
{
    Q_OBJECT

public:
    NukeExecutor(ChannelPtr channel, NukePlan plan, NukeAction action,
                 int timeoutSeconds, const QString &reason,
                 QObject *parent = nullptr);
    ~NukeExecutor() override;

    void start();
    void cancel();

    bool isRunning() const
    {
        return this->running_;
    }

    /// The currently-live executor (for /cancelnuke).
    static QPointer<NukeExecutor> &active();

    /// Record of a completed nuke run, retained for undo. Stored by
    /// LimerinoNukeDialog on finish; one entry (last run) kept in memory.
    struct LastRun {
        QString channelKey;          // "twitch:name" / "kick:name"
        QString channelName;
        QString channelBroadcasterId;  // Twitch room id or Kick channel uid
        bool isKick = false;
        QString operatorModeratorId;   // acting moderator id at run time
        NukeAction action;
        int timeoutSeconds = 0;        // 0 means permanent (or n/a)
        QString reason;
        QList<NukeTarget> succeeded;   // only successes — undo touches these
        QList<QString> deletedIds;     // kept for the report; not reversible
        QDateTime finishedAt;
    };

    /// Populated at settled-finish by the dialog. Kept in a function-local
    /// static so it survives dialog destruction but not app shutdown.
    static std::optional<LastRun> &lastRun();

Q_SIGNALS:
    /// After every settled operation. `done` = succeeded + failed so far.
    void progress(int done, int total, int succeeded, int failed);
    /// Terminal state.
    void finished(bool cancelled, const QStringList &failureReasons);

private:
    enum class OpKind { Ban, Timeout, Warn, Delete };

    struct Operation {
        OpKind kind;
        QString targetUserId;
        QString targetLogin;
        QString messageId;
        uint64_t targetKickId = 0;  // Kick only: numeric user id
    };

    void buildQueue();
    void dispatchNext();
    void dispatchTwitch(TwitchChannel *chan, const Operation &op);
    void dispatchKick(KickChannel *chan, const Operation &op);
    void reportFailure(const QString &context, const QString &message);
    void settle();
    void rememberRun();

    ChannelPtr channel_;
    NukePlan plan_;
    NukeAction action_;
    int timeoutSeconds_ = 600;
    QString reason_;

    QList<Operation> queue_;
    int cursor_ = 0;
    int succeeded_ = 0;
    int failed_ = 0;
    QStringList failures_;

    // For undo retention: only the ops that actually succeeded.
    QList<NukeTarget> succeededUserOps_;
    QList<QString> succeededDeletes_;

    bool running_ = false;
    bool cancelled_ = false;
    bool inFlight_ = false;
};

/// Command entry: cancel whatever nuke is running. Returns a system-message
/// body to echo into the channel.
QString cancelRunningNuke();

/// Command entry: reverse the reversible parts (bans/timeouts) of the most
/// recent completed nuke in the current channel. Deletes cannot be undone.
QString undoLastNuke(const std::shared_ptr<Channel> &channel);

}  // namespace chatterino::limerino
