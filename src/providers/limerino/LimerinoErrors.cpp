// SPDX-License-Identifier: MIT

#include "providers/limerino/LimerinoErrors.hpp"

namespace chatterino::LimerinoAuth::errors {

namespace {

QString humanizeStatus(int status)
{
    switch (status)
    {
        case 400:
            return QStringLiteral("bad request (400)");
        case 401:
            return QStringLiteral(
                "not authorized (401). The extra-features login may have "
                "expired - re-add it under Settings > Limerino");
        case 403:
            return QStringLiteral(
                "forbidden (403). The extra-features login lacks the needed "
                "access - check Settings > Limerino");
        case 404:
            return QStringLiteral("not found (404)");
        case 429:
            return QStringLiteral("rate limited (429) - try again later");
        default:
            break;
    }
    if (status >= 500)
    {
        return QStringLiteral("Twitch server error (%1)").arg(status);
    }
    return QStringLiteral("HTTP status %1").arg(status);
}

}  // namespace

QString describeHttpFailure(int status, const QString &action)
{
    if (action.isEmpty())
    {
        return humanizeStatus(status);
    }
    return QStringLiteral("%1: %2").arg(action, humanizeStatus(status));
}

QString fromGraphQlErrors(const QJsonArray &errors)
{
    if (errors.isEmpty())
    {
        return {};
    }
    QString text = errors.first().toObject()[QStringLiteral("message")].toString();
    if (text.isEmpty())
    {
        text = QStringLiteral("graphQL error");
    }
    if (errors.size() > 1)
    {
        text += QStringLiteral(" (+%1 more)").arg(errors.size() - 1);
    }
    return text;
}

QString fromPayloadErrorField(const QJsonObject &fieldError)
{
    if (fieldError.isEmpty())
    {
        return {};
    }
    const QString code = fieldError[QStringLiteral("code")].toString();
    const QString msg = fieldError[QStringLiteral("message")].toString();
    if (!code.isEmpty() && !msg.isEmpty())
    {
        return QStringLiteral("%1 (%2)").arg(code, msg);
    }
    if (!code.isEmpty())
    {
        return code;
    }
    return msg;
}

QString fromValidationErrorField(const QJsonObject &validationError)
{
    return fromPayloadErrorField(validationError);
}

QString missingScopeMessage(const QString &scope, const QString &action)
{
    return QStringLiteral("the extra-features login needs the scope %1 "
                          "to %2. Re-add the account under Settings > "
                          "Limerino.")
        .arg(scope, action);
}

QString tokenExpiredMessage(const QString &login)
{
    return QStringLiteral(
               "the extra-features login for %1 expired or was revoked. "
               "Re-add it under Settings > Limerino.")
        .arg(login);
}

QString tokenRequiredMessage(const QString &action)
{
    return QStringLiteral(
               "this needs an extra-features login (Settings > Limerino > "
               "Extra features) to %1.")
        .arg(action);
}

}  // namespace chatterino::LimerinoAuth::errors
