// SPDX-License-Identifier: MIT
// Hermes (Twitch live-updates transport) wire protocol frames.
//
// Wire shapes, ids and field paths are transcribed from pubsubreference/
// (client.js sendMessage L329-352 and wsMessageHandlers L354-468).
// Do not invent message types or field names.

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace chatterino::limerino {

/// 21-character base64url identifier (client.js L31-32, L178, L333).
QString hermesRandomId();

/// {"type":"authenticate",id,timestamp,authenticate:{token}} (L286-289, L336-340)
QByteArray makeHermesAuthenticateMessage(const QString &token);

/// {"type":"subscribe",id,timestamp,subscribe:{id,type:"pubsub",pubsub:{topic}}}
/// (L340-343)
QByteArray makeHermesSubscribeMessage(const QString &id, const QString &topic);

/// {"type":"unsubscribe",id,timestamp,unsubscribe:{id,type:"pubsub",pubsub:{topic}}}
/// (L340-343)
QByteArray makeHermesUnsubscribeMessage(const QString &id, const QString &topic);

struct HermesFrame {
    enum class Type {
        Welcome,
        Keepalive,
        Reconnect,
        AuthenticateResponse,
        SubscribeResponse,
        UnsubscribeResponse,
        Notification,

        INVALID,
    };

    Type type = Type::INVALID;
    QString typeString;
    QJsonObject object;
};

/// Parses the outer JSON object and dispatches on its "type" field
/// (client.js L290-304).
std::optional<HermesFrame> parseHermesFrame(const QByteArray &payload);

struct HermesWelcome {
    int keepaliveSec = 10;  // client.js L356: (msg.welcome.keepaliveSec || 10)
};

/// welcome.keepaliveSec (client.js L355-356)
std::optional<HermesWelcome> parseHermesWelcome(const QJsonObject &object);

/// Shared result shape of authenticate/subscribe/unsubscribe -Response frames
/// (client.js L377-440: {result, error, errorCode}).
struct HermesResultResponse {
    QString result;  // "ok" | "error"
    QString error;
    QString errorCode;
};

std::optional<HermesResultResponse> parseHermesAuthenticateResponse(
    const QJsonObject &object);
std::optional<HermesResultResponse> parseHermesSubscribeResponse(
    const QJsonObject &object);
std::optional<HermesResultResponse> parseHermesUnsubscribeResponse(
    const QJsonObject &object);

/// Top-level parentId of subscribe/unsubscribe responses (client.js L404, L431).
std::optional<QString> hermesResponseParentId(const QJsonObject &object);

struct HermesNotification {
    QString subscriptionId;  // notification.subscription.id (L447)
    QJsonObject payload;     // parsed inner notification.pubsub JSON (L453-460)
};

/// Unwraps notification.pubsub (a JSON *string*) and requires a non-empty
/// inner payload .type (both dropped by the reference when absent, L442-460).
std::optional<HermesNotification> parseHermesNotification(
    const QJsonObject &object);

}  // namespace chatterino::limerino
