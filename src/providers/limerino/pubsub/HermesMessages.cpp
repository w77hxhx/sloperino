// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/HermesMessages.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QRandomGenerator>

namespace chatterino::limerino {

namespace {

// client.js L31-32
constexpr char BASE64URL_CHARSET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

QString hermesTimestamp()
{
    // client.js L335: new Date().toISOString()
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QByteArray makeHermesSubMessage(const QString &type, const QString &id,
                                const QString &topic)
{
    // client.js L335-343: msg[type] = { id, type: 'pubsub', pubsub: { topic } }
    QJsonObject root{
        {QStringLiteral("type"), type},
        {QStringLiteral("id"), id},
        {QStringLiteral("timestamp"), hermesTimestamp()},
    };
    QJsonObject sub{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), QStringLiteral("pubsub")},
        {QStringLiteral("pubsub"),
         QJsonObject{{QStringLiteral("topic"), topic}}},
    };
    root[type] = sub;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<HermesResultResponse> parseResult(const QJsonObject &object,
                                                const QString &key)
{
    const QJsonObject result = object[key].toObject();
    if (result.isEmpty())
    {
        return std::nullopt;
    }
    return HermesResultResponse{
        .result = result[QStringLiteral("result")].toString(),
        .error = result[QStringLiteral("error")].toString(),
        .errorCode = result[QStringLiteral("errorCode")].toString(),
    };
}

}  // namespace

QString hermesRandomId()
{
    QString id(21, Qt::Uninitialized);
    for (auto &ch : id)
    {
        ch = QLatin1Char(
            BASE64URL_CHARSET[QRandomGenerator::global()->bounded(64)]);
    }
    return id;
}

QByteArray makeHermesAuthenticateMessage(const QString &token)
{
    // client.js L336-339: msg.authenticate = { token }
    // NOTE: never log the returned frame, it carries credential material.
    QJsonObject root{
        {QStringLiteral("type"), QStringLiteral("authenticate")},
        {QStringLiteral("id"), hermesRandomId()},
        {QStringLiteral("timestamp"), hermesTimestamp()},
        {QStringLiteral("authenticate"),
         QJsonObject{{QStringLiteral("token"), token}}},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray makeHermesSubscribeMessage(const QString &id, const QString &topic)
{
    return makeHermesSubMessage(QStringLiteral("subscribe"), id, topic);
}

QByteArray makeHermesUnsubscribeMessage(const QString &id, const QString &topic)
{
    return makeHermesSubMessage(QStringLiteral("unsubscribe"), id, topic);
}

std::optional<HermesFrame> parseHermesFrame(const QByteArray &payload)
{
    const auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject())
    {
        return std::nullopt;
    }

    HermesFrame frame;
    frame.object = doc.object();
    frame.typeString = frame.object[QStringLiteral("type")].toString();

    if (frame.typeString == QLatin1String("welcome"))
    {
        frame.type = HermesFrame::Type::Welcome;
    }
    else if (frame.typeString == QLatin1String("keepalive"))
    {
        frame.type = HermesFrame::Type::Keepalive;
    }
    else if (frame.typeString == QLatin1String("reconnect"))
    {
        frame.type = HermesFrame::Type::Reconnect;
    }
    else if (frame.typeString == QLatin1String("authenticateResponse"))
    {
        frame.type = HermesFrame::Type::AuthenticateResponse;
    }
    else if (frame.typeString == QLatin1String("subscribeResponse"))
    {
        frame.type = HermesFrame::Type::SubscribeResponse;
    }
    else if (frame.typeString == QLatin1String("unsubscribeResponse"))
    {
        frame.type = HermesFrame::Type::UnsubscribeResponse;
    }
    else if (frame.typeString == QLatin1String("notification"))
    {
        frame.type = HermesFrame::Type::Notification;
    }
    return frame;
}

std::optional<HermesWelcome> parseHermesWelcome(const QJsonObject &object)
{
    const QJsonObject welcome = object[QStringLiteral("welcome")].toObject();
    if (welcome.isEmpty())
    {
        return std::nullopt;
    }
    HermesWelcome out;
    // client.js L356: (msg.welcome.keepaliveSec || 10)
    out.keepaliveSec = welcome[QStringLiteral("keepaliveSec")].toInt(10);
    if (out.keepaliveSec <= 0)
    {
        out.keepaliveSec = 10;
    }
    return out;
}

std::optional<HermesResultResponse> parseHermesAuthenticateResponse(
    const QJsonObject &object)
{
    return parseResult(object, QStringLiteral("authenticateResponse"));
}

std::optional<HermesResultResponse> parseHermesSubscribeResponse(
    const QJsonObject &object)
{
    return parseResult(object, QStringLiteral("subscribeResponse"));
}

std::optional<HermesResultResponse> parseHermesUnsubscribeResponse(
    const QJsonObject &object)
{
    return parseResult(object, QStringLiteral("unsubscribeResponse"));
}

std::optional<QString> hermesResponseParentId(const QJsonObject &object)
{
    const QString parentId = object[QStringLiteral("parentId")].toString();
    if (parentId.isEmpty())
    {
        return std::nullopt;
    }
    return parentId;
}

std::optional<HermesNotification> parseHermesNotification(
    const QJsonObject &object)
{
    // client.js L442-460
    const QJsonObject notification =
        object[QStringLiteral("notification")].toObject();

    const QString raw = notification[QStringLiteral("pubsub")].toString();
    if (raw.isEmpty())
    {
        return std::nullopt;
    }
    const QString subscriptionId =
        notification[QStringLiteral("subscription")]
            .toObject()[QStringLiteral("id")]
            .toString();
    if (subscriptionId.isEmpty())
    {
        return std::nullopt;
    }

    const auto doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject())
    {
        return std::nullopt;
    }
    QJsonObject payload = doc.object();
    if (payload[QStringLiteral("type")].toString().isEmpty())
    {
        return std::nullopt;
    }

    return HermesNotification{
        .subscriptionId = subscriptionId,
        .payload = std::move(payload),
    };
}

}  // namespace chatterino::limerino
