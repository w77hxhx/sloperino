// SPDX-License-Identifier: MIT
// Helix client for the ported command set.
// Token selection per the batch-0 decision: prefer the PRIMARY chatterino
// login when its validated scopes cover the call, otherwise a LimerinoAuth
// resolver token. Client-Id is always sourced from LimerinoAuth (never
// hardcoded here).

#pragma once

#include <QString>
#include <QStringList>

#include "providers/limerino/LimerinoAuth.hpp"

#include <functional>
#include <optional>

namespace chatterino::LimerinoApi {

using ApiErrorCallback = std::function<void(const QString &redactedMessage)>;

// Evaluates the correct token for a Helix call needing `requiredScope`.
// Order: primary chatterino login (if its /validate scopes include it),
// then a LimerinoAuth resolver token (checked against its recorded scopes).
// `err` receives a friendly token-free reason when no token qualifies.
void helixTokenFor(
    const QString &requiredScope,
    const std::function<void(const LimerinoAuth::LimerinoAuthToken &token,
                             bool fromPrimary)> &cb,
    const std::function<void(const QString &err)> &onError);

// Plugin's sendMessage: POST /helix/chat/messages. dropReason text comes back
// on Twitch-side rejection (identical shape to the plugin's handling).
void sendChatMessage(
    const QString &token, const QString &broadcasterId, const QString &senderId,
    const QString &message, const std::function<void()> &onSent,
    const std::function<void(const QString &dropReason)> &onDrop);

// Plugin's paste upload: POST {pasteHost}/documents -> {key} -> {pasteHost}/raw/{key}
void uploadPaste(const QString &text,
                 const std::function<void(const QString &url)> &onSuccess,
                 const ApiErrorCallback &onError);

// The configured paste host ("/limerino/paste/host"), no trailing slash.
QString pasteHost();

// --- Moderation actions (batch N3) -------------------------------------------
// Every one of these wraps the canonical Helix call (getHelix()->...) inside
// LimerinoRateLimiter::execute, so a nuke never bursts against the same
// endpoint. Success/failure text mirrors the built-in slash-command shapes.

void banUser(const QString &broadcasterID, const QString &moderatorID,
             const QString &userID, std::optional<int> durationSeconds,
             const QString &reason, const QString &bucketKey,
             std::function<void()> onSuccess,
             std::function<void(QString)> onError);

void warnUser(const QString &broadcasterID, const QString &moderatorID,
              const QString &userID, const QString &reason,
              const QString &bucketKey, std::function<void()> onSuccess,
              std::function<void(QString)> onError);

void deleteChatMessage(const QString &broadcasterID,
                       const QString &moderatorID, const QString &messageID,
                       const QString &bucketKey, std::function<void()> onSuccess,
                       std::function<void(QString)> onError);

// Undo for ban/timeout (both use Helix DELETE moderation/bans).
void unbanUser(const QString &broadcasterID, const QString &moderatorID,
               const QString &userID, const QString &bucketKey,
               std::function<void()> onSuccess,
               std::function<void(QString)> onError);

}  // namespace chatterino::LimerinoApi
