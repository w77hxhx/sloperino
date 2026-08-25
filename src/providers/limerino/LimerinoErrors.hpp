// SPDX-License-Identifier: MIT
// Central user-facing error text for the Limerino extra-features surface.
// Everything here is token-free by construction (see redact() in LimerinoAuth).

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace chatterino::LimerinoAuth::errors {

// HTTP status -> base message. `detail` may hold the request's own error body
// snippet (already redacted by callers).
QString describeHttpFailure(int status, const QString &action);

// Central GQL errors[] extractor: HTTP 200 with populated errors[] is a FAILURE.
// Joins the messages (first wins for brevity), never echoes variables or tokens.
QString fromGraphQlErrors(const QJsonArray &errors);

// Payload-level error objects the plugin reads (e.g. data.op.error.code,
// updateUser.error.code, sendCheer.validationError).
// Returns empty when no error was found.
QString fromPayloadErrorField(const QJsonObject &fieldError);
QString fromValidationErrorField(const QJsonObject &validationError);

// Scope/token cases, named plainly for the settings page + chat messages.
QString missingScopeMessage(const QString &scope, const QString &action);
QString tokenExpiredMessage(const QString &login);
QString tokenRequiredMessage(const QString &action);

}  // namespace chatterino::LimerinoAuth::errors
