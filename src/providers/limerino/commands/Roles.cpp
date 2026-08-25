// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/Roles.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "widgets/dialogs/limerino/LimerinoResultDialog.hpp"
#include "widgets/dialogs/limerino/LimerinoResultList.hpp"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <memory>

namespace chatterino::LimerinoCommands {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

void say(const ChannelPtr &channel, const QString &text)
{
    if (channel)
    {
        channel->addSystemMessage(text);
    }
}

// --------------------------------------------------------
// 7tv helper: twitch id -> 7tv user id (v4, no auth)
// --------------------------------------------------------
void fetchSeventvUserId(const QString &twitchUserId,
                        const std::function<void(const QString &stiId)> &onOk,
                        const std::function<void(const QString &err)> &onErr)
{
    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v4/gql")),
                   NetworkRequestType::Post)
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .payload(QJsonDocument(QJsonObject{{QStringLiteral("query"),
                                            gql::SEVENTV_USER_BY_CONNECTION_QUERY
                                                .arg(twitchUserId)}})
                     .toJson(QJsonDocument::Compact))
        .timeout(5000)
        .onSuccess([onOk, onErr](NetworkResult result) {
            const QString id = QJsonDocument::fromJson(result.getData())
                                   .object()[QStringLiteral("data")]
                                   .toObject()[QStringLiteral("users")]
                                   .toObject()[QStringLiteral("userByConnection")]
                                   .toObject()[QStringLiteral("id")]
                                   .toString();
            if (id.isEmpty())
            {
                onErr(QStringLiteral("Failed to fetch 7TV user ID."));
                return;
            }
            onOk(id);
        })
        .onError([onErr](NetworkResult result) {
            onErr(LimerinoAuth::errors::describeHttpFailure(
                result.status().value_or(0), QStringLiteral("fetch 7tv id")));
        })
        .execute();
}

// --------------------------------------------------------
// shared community-role mutation (grant/revoke, ARTIST)
// --------------------------------------------------------
void communityRoleMutation(bool grant, const QString &userLogin,
                           const ChannelPtr &channel)
{
    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        say(channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      grant ? QStringLiteral("grant artist")
                            : QStringLiteral("revoke artist"))
                : err);
        return;
    }

    const QString opField = grant ? QStringLiteral("grantCommunityRole")
                                  : QStringLiteral("revokeCommunityRole");
    const QString manageField = grant ? QStringLiteral("granteeLogin")
                                      : QStringLiteral("revokeeLogin");
    gql::executePersisted(
        grant ? gql::PQ_GRANT_COMMUNITY_ROLE : gql::PQ_REVOKE_COMMUNITY_ROLE,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), token.userId},
                                 {manageField, userLogin},
                                 {QStringLiteral("role"),
                                  QStringLiteral("ARTIST")}}}},
        token.token,
        [weak = std::weak_ptr(channel), grant, opField](
            const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                const QString code =
                    data[opField].toObject()[QStringLiteral("error")]
                        .toObject()[QStringLiteral("code")]
                        .toString();
                if (!code.isEmpty())
                {
                    chan->addSystemMessage(
                        grant ? QStringLiteral("Unable to grant artist! Status: %1")
                                    .arg(code)
                              : QStringLiteral("Unable to revoke artist role! Error: %1")
                                    .arg(code));
                }
                else
                {
                    chan->addSystemMessage(
                        grant ? QStringLiteral("Successfully granted artist!")
                              : QStringLiteral("Successfully revoked artist!"));
                }
            }
        },
        [weak = std::weak_ptr(channel), grant](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage((grant ? QStringLiteral("Unable to grant artist! ")
                                              : QStringLiteral("Unable to revoke artist role! ")) +
                                       e.message);
            }
        });
}

}  // namespace

// ---------------------------------------------------------------
// Slash commands (plugin parity)
// ---------------------------------------------------------------

QString grantArtistCmd(const CommandContext &ctx)
{
    const QString name = ctx.words.value(1);
    if (name.isEmpty())
    {
        say(ctx.channel,
            QStringLiteral("Usage: /artist <username> - Grants artist role to "
                           "specified user"));
        return {};
    }
    grantArtist(name, ctx.channel);
    return {};
}

QString revokeArtistCmd(const CommandContext &ctx)
{
    const QString name = ctx.words.value(1);
    if (name.isEmpty())
    {
        say(ctx.channel,
            QStringLiteral("Usage: /unartist <username> - Revokes artist from "
                           "specified user."));
        return {};
    }
    revokeArtist(name, ctx.channel);
    return {};
}

QString grantLeadModCmd(const CommandContext &ctx)
{
    const QString name = ctx.words.value(1);
    if (name.isEmpty())
    {
        say(ctx.channel, QStringLiteral("Usage: /leadmod [modname]"));
        return {};
    }
    grantLeadMod(name, ctx.channel);
    return {};
}

// ---------------------------------------------------------------
// Usercard button entries (own channel only for grant ops)
// ---------------------------------------------------------------

void grantArtist(const QString &userLogin, const ChannelPtr &channel)
{
    communityRoleMutation(true, userLogin, channel);
}

void revokeArtist(const QString &userLogin, const ChannelPtr &channel)
{
    communityRoleMutation(false, userLogin, channel);
}

void grantLeadMod(const QString &userLogin, const ChannelPtr &channel)
{
    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        say(channel, err.isEmpty()
                         ? LimerinoAuth::errors::tokenRequiredMessage(
                               QStringLiteral("grant lead mod"))
                         : err);
        return;
    }

    // Plugin: /leadmod <modname> -> resolve user id, then assign lead_mod.
    gql::executePersisted(
        gql::PQ_GET_USER_ID,
        QJsonObject{{QStringLiteral("login"), userLogin},
                    {QStringLiteral("lookupType"), QStringLiteral("ALL")}},
        token.token,
        [weak = std::weak_ptr(channel), token](
            const QJsonObject &data) {
            const QString uid = data[QStringLiteral("user")]
                                    .toObject()[QStringLiteral("id")]
                                    .toString();
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }
            if (uid.isEmpty())
            {
                chan->addSystemMessage(QStringLiteral("No such user."));
                return;
            }
            gql::executePersisted(
                gql::PQ_ASSIGN_CHANNEL_ROLE,
                QJsonObject{{QStringLiteral("input"),
                             QJsonObject{{QStringLiteral("channelID"),
                                          token.userId},
                                         {QStringLiteral("targetUserID"), uid},
                                          {QStringLiteral("roleID"),
                                           QStringLiteral("lead_mod")}}}},
                token.token,
                [weak](const QJsonObject &assignData) {
                    if (auto ch = weak.lock())
                    {
                        const QString code =
                            assignData[QStringLiteral("assignChannelRole")]
                                .toObject()[QStringLiteral("error")]
                                .toObject()[QStringLiteral("code")]
                                .toString();
                        ch->addSystemMessage(
                            code.isEmpty()
                                ? QStringLiteral("Successfully assigned role!")
                                : QStringLiteral("Unable to assign role! Status: %1")
                                      .arg(code));
                    }
                },
                [weak](const gql::GqlError &e) {
                    if (auto ch = weak.lock())
                    {
                        ch->addSystemMessage(
                            QStringLiteral("Unable to grant lead mod! %1")
                                .arg(e.message));
                    }
                });
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e.message);
            }
        });
}

// ---------------------------------------------------------------
// Avatar-menu 7tv info widgets
// ---------------------------------------------------------------

void showSeventvUserEditors(const QString &twitchUserId,
                            const QString &userLogin,
                            const ChannelPtr &feedbackChannel)
{
    if (twitchUserId.isEmpty())
    {
        say(feedbackChannel,
            QStringLiteral("No Twitch user id for '%1'.").arg(userLogin));
        return;
    }

    const QString label =
        userLogin.trimmed().isEmpty() ? twitchUserId : userLogin.trimmed();

    fetchSeventvUserId(
        twitchUserId,
        [label, feedbackChannel](const QString &stvUserId) {
            NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v4/gql")),
                           NetworkRequestType::Post)
                .header("Content-Type", "application/json")
                .payload(QJsonDocument(
                             QJsonObject{
                                 {QStringLiteral("query"),
                                  gql::SEVENTV_ONE_USER_QUERY},
                                 {QStringLiteral("variables"),
                                  QJsonObject{
                                      {QStringLiteral("id"), stvUserId}}}})
                             .toJson(QJsonDocument::Compact))
                .timeout(5000)
                .onSuccess([label, feedbackChannel](NetworkResult result) {
                    const QJsonObject root =
                        QJsonDocument::fromJson(result.getData()).object();
                    // 7TV returns HTTP 200 with data:null + errors[] for bad
                    // selection sets; treat that as failure, not an empty list.
                    if (root.contains(QStringLiteral("errors")) &&
                        !root.value(QStringLiteral("errors"))
                             .toArray()
                             .isEmpty() &&
                        (root.value(QStringLiteral("data")).isNull() ||
                         !root.contains(QStringLiteral("data"))))
                    {
                        say(feedbackChannel,
                            QStringLiteral("Failed to fetch 7tv editors."));
                        return;
                    }
                    const QJsonArray editors =
                        root.value(QStringLiteral("data"))
                            .toObject()[QStringLiteral("users")]
                            .toObject()[QStringLiteral("user")]
                            .toObject()[QStringLiteral("editors")]
                            .toArray();
                    QVector<QStringList> rows;
                    for (int i = 0; i < editors.size(); ++i)
                    {
                        const QJsonObject editor =
                            editors.at(i)
                                .toObject()
                                .value(QStringLiteral("editor"))
                                .toObject();
                        rows.append(
                            {editor.value(QStringLiteral("mainConnection"))
                                 .toObject()
                                 .value(QStringLiteral("platformDisplayName"))
                                 .toString(),
                             editor.value(QStringLiteral("id")).toString()});
                    }

                    auto *dialog = new limerino::LimerinoResultDialog;
                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->setWindowTitle(
                        QStringLiteral("7TV editors — %1").arg(label));
                    dialog->resultList()->setTitleText(
                        QStringLiteral("The editors (%1) of %2 are:")
                            .arg(rows.size())
                            .arg(label));
                    dialog->resultList()->setColumns(
                        {QStringLiteral("name"), QStringLiteral("7tv id")});
                    dialog->resultList()->setRows(rows);
                    dialog->resultList()->setStatusText(QStringLiteral(
                        "click a row to open the 7tv profile"));
                    dialog->resultList()->setRowOpenUrlProvider(
                        [](const QStringList &row) {
                            const QString id = row.value(1);
                            return id.isEmpty()
                                       ? QString()
                                       : QStringLiteral(
                                             "https://7tv.app/users/%1")
                                             .arg(id);
                        });
                    dialog->resize(420, 460);
                    dialog->show();
                })
                .onError([feedbackChannel](NetworkResult) {
                    say(feedbackChannel,
                        QStringLiteral("Failed to fetch 7tv editors."));
                })
                .execute();
        },
        [feedbackChannel](const QString &err) { say(feedbackChannel, err); });
}

void showSeventvUserEditorIn(const QString &userLogin)
{
    // chain: twitch user id (GQL GetUserID) -> 7tv user id -> editor_of list
    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        return;
    }
    gql::executePersisted(
        gql::PQ_GET_USER_ID,
        QJsonObject{{QStringLiteral("login"), userLogin},
                    {QStringLiteral("lookupType"), QStringLiteral("ALL")}},
        token.token,
        [userLogin](const QJsonObject &data) {
            const QString twitchId = data[QStringLiteral("user")]
                                             .toObject()[QStringLiteral("id")]
                                             .toString();
            if (twitchId.isEmpty())
            {
                return;
            }
            fetchSeventvUserId(twitchId, [userLogin](const QString &stvUserId) {
                NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/gql")),
                               NetworkRequestType::Post)
                    .header("Content-Type", "application/json")
                    .payload(QJsonDocument(
                                 QJsonObject{{QStringLiteral("operationName"),
                                              QStringLiteral("GetUserEditorOf")},
                                             {QStringLiteral("variables"),
                                              QJsonObject{{QStringLiteral("id"),
                                                           stvUserId}}},
                                             {QStringLiteral("query"),
                                              gql::SEVENTV_EDITOR_OF_QUERY}})
                                 .toJson(QJsonDocument::Compact))
                    .timeout(5000)
                    .onSuccess([userLogin](NetworkResult result) {
                        const QJsonObject user =
                            QJsonDocument::fromJson(result.getData())
                                .object()[QStringLiteral("data")]
                                .toObject()[QStringLiteral("user")]
                                .toObject();
                        const QString displayName =
                            user[QStringLiteral("display_name")].toString();
                        const QJsonArray editorOf =
                            user[QStringLiteral("editor_of")].toArray();

                        QVector<QStringList> rows;
                        for (int i = 0; i < editorOf.size(); ++i)
                        {
                            const QJsonObject u =
                                editorOf.at(i)
                                    .toObject()[QStringLiteral("user")]
                                    .toObject();
                            rows.append({u[QStringLiteral("display_name")]
                                             .toString(),
                                         u[QStringLiteral("id")].toString()});
                        }

                        auto *dialog = new limerino::LimerinoResultDialog;
                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->setWindowTitle(
                            QStringLiteral("Editor-in-channels - %1")
                                .arg(userLogin));
                        dialog->resultList()->setTitleText(QStringLiteral(
                            "Channels (%1) that %2 can edit:").arg(rows.size()).arg(displayName));
                        dialog->resultList()->setColumns(
                            {QStringLiteral("name"), QStringLiteral("7tv id")});
                        dialog->resultList()->setRows(rows);
                        dialog->resultList()->setStatusText(
                            QStringLiteral("click a row to open the 7tv profile"));
                        dialog->resultList()->setRowOpenUrlProvider(
                            [](const QStringList &row) {
                                const QString id = row.value(1);
                                return id.isEmpty()
                                           ? QString()
                                           : QStringLiteral("https://7tv.app/users/%1")
                                                 .arg(id);
                            });
                        dialog->resize(420, 460);
                        dialog->show();
                    })
                    .onError([](NetworkResult) {})
                    .execute();
            }, [](const QString &) {});
        },
        [](const gql::GqlError &) {});
}

}  // namespace chatterino::LimerinoCommands
