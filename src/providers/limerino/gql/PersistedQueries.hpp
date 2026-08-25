// SPDX-License-Identifier: MIT
// Registry of GraphQL operations used by the source plugin (pluginforreference/).
// Persisted-query hashes and inline query texts are transcribed VERBATIM from
// pluginforreference/requests.lua - nothing invented, nothing "modernized".

#pragma once

#include <QString>

namespace chatterino::LimerinoAuth::gql {

// ---------------------------------------------------------------------------
// Persisted queries (operationName -> sha256Hash, version 1)
// ---------------------------------------------------------------------------

struct PersistedQuery {
    const char *name;
    const char *sha256;
};

constexpr PersistedQuery PQ_GET_USER_ID{
    "GetUserID",
    "bf6c594605caa0c63522f690156aa04bd434870bf963deb76668c381d16fcaa5"};
constexpr PersistedQuery PQ_FOLLOW_USER{
    "FollowButton_FollowUser",
    "800e7346bdf7e5278a3c1d3f21b2b56e2639928f86815677a7126b093b2fdd08"};
constexpr PersistedQuery PQ_PIN_CHAT_MESSAGE{
    "PinChatMessage",
    "214191369c21f1ad67ac074795d53832329c70e4088c979040c9f86334a7d736"};
constexpr PersistedQuery PQ_SEND_PINNED_CHAT_MESSAGE{
    "SendPinnedChatMessage",
    "3fbdef44bc463479eebaad86f182df0a96288449ed92cb68bebe09ffb9a4a719"};
constexpr PersistedQuery PQ_GET_PINNED_CHAT{
    "GetPinnedChat",
    "2d099d4c9b6af80a07d8440140c4f3dbb04d516b35c401aab7ce8f60765308d5"};
constexpr PersistedQuery PQ_UNPIN_CHAT_MESSAGE{
    "unpinChatMessage",
    "86409b9c86510bdc9f2c6d8e58fdc4041963c001de53577160ab649e03334511"};
constexpr PersistedQuery PQ_REVOKE_COMMUNITY_ROLE{
    "revokeCommunityRole",
    "06a18bd1481ad054b9b711ef33e23f21f6f29c77743662660b8e0ae63eed26f5"};
constexpr PersistedQuery PQ_GRANT_COMMUNITY_ROLE{
    "grantCommunityRole",
    "7a681d869bb3ad6457693daea566c8672afd66dc91c81b52b8b1bb7961fae4ec"};
constexpr PersistedQuery PQ_CREATE_PREDICTION_EVENT{
    "createPredictionEvent",
    "92268878ac4abe722bcdcba85a4e43acdd7a99d86b05851759e1d8f385cc32ea"};
constexpr PersistedQuery PQ_MAKE_PREDICTION{
    "MakePrediction",
    "b44682ecc88358817009f20e69d75081b1e58825bb40aa53d5dbadcc17c881d8"};
constexpr PersistedQuery PQ_DELETE_PREDICTION{
    "DeletePrediction",
    "35d375614e426624456ee7be4a2e0fbc0a410c0a91c21f6044cb3cd5c38c4e4d"};
constexpr PersistedQuery PQ_CHANNEL_POINTS_PREDICTION_CONTEXT{
    "ChannelPointsPredictionContext",
    "beb846598256b75bd7c1fe54a80431335996153e358ca9c7837ce7bb83d7d383"};
constexpr PersistedQuery PQ_RESOLVE_PREDICTION{
    "ResolvePrediction",
    "10c803ec11bb8c2957d66bc6a47349dc3c5f51d694585b5ebc37ba656da413c1"};
constexpr PersistedQuery PQ_LOCK_PREDICTION{
    "LockPrediction",
    "1f2b1eb44af35f055308e78ffbe81c2f958408f9b32d076a759a84ab213285d4"};
constexpr PersistedQuery PQ_CREATE_POLL{
    "CreatePoll",
    "4b1461a13fe166a59044961db192747d606f71a89abc3bfdecf79fe862d205cf"};
constexpr PersistedQuery PQ_GET_VIEWABLE_POLL{
    "ChannelPollContext_GetViewablePoll",
    "e83188a3836c636393df3191665e543a03733d7c51d3ade3d85e42aa46c2bf55"};
constexpr PersistedQuery PQ_TERMINATE_POLL{
    "TerminatePoll",
    "2701ef0594dae5f532ce68e58cc3036a6d020755eef49927f98c14017fd819b2"};
constexpr PersistedQuery PQ_CHANNEL_POINTS_CONTEXT{
    "ChannelPointsContext",
    "374314de591e69925fce3ddc2bcf085796f56ebb8cad67a0daa3165c03adc345"};
constexpr PersistedQuery PQ_REDEEM_CUSTOM_REWARD{
    "RedeemCustomReward",
    "d56249a7adb4978898ea3412e196688d4ac3cea1c0c2dfd65561d229ea5dcc42"};
constexpr PersistedQuery PQ_CHAT_SETTINGS_BADGES{
    "ChatSettings_Badges",
    "a0300a9d8c43ec7a6bf653d46478948cc943d4ad9b2b28654241916b621dbfe5"};
constexpr PersistedQuery PQ_SELECT_GLOBAL_BADGE{
    "ChatSettings_SelectGlobalBadge",
    "5e1b7f0ba771ca8eb81c0fcd5b8f4ff559ec2dc71cc9256e04ec2665049fc4e5"};
constexpr PersistedQuery PQ_ADD_BLOCKED_TERM{
    "AddChannelBlockedTerm",
    "10f4c5c8dd6817c21058040b50181040e91e894ca324b14beda6b5f5e429aa02"};
constexpr PersistedQuery PQ_ACKNOWLEDGE_CHAT_WARNING{
    "AcknowledgeChatWarning",
    "f97404a69caf9d152118bae17e962eca27c87c8a85224538173b3dfcd6c9df60"};
constexpr PersistedQuery PQ_MOD_ACTIONS_LIST{
    "ModActionsList",
    "66d88c2ba9637cb5582e3b53ff946eb44c4140c5aef8778b50abefdec3206ce8"};
constexpr PersistedQuery PQ_SEND_CHEER{
    "ChatInput_SendCheer",
    "57b0d6bd979e516ae3767f6586e7f23666d612d3a65af1d5436dba130c9426fd"};
constexpr PersistedQuery PQ_ASSIGN_CHANNEL_ROLE{
    "AssignChannelRole",
    "2d373c90d0d0e6d4fe771bc6136febe6a148eb3d5700d2a0575883a043fbd581"};

// Crossban (viewer-card strike + mod comments). Hashes from live captures.
constexpr PersistedQuery PQ_CHAT_MODERATOR_STRIKE_STATUS{
    "ChatModeratorStrikeStatus",
    "7f50f7190a840cd9fe9a91398f34ebb690eeba7cb28bce70e4cbf7ed1d06f268"};
constexpr PersistedQuery PQ_VIEWER_CARD_MOD_LOGS_COMMENTS{
    "ViewerCardModLogsComments",
    "58273e486b4e50dafbad9f736bc1ebcc7db935572664cb9c1e561b8e1e076dc6"};
constexpr PersistedQuery PQ_CREATE_MOD_COMMENT{
    "createModComment",
    "1274e01f38489378e48dbb35f3cd4d11b9bbfbb38171ae7815426d7a56bca3f6"};

// ---------------------------------------------------------------------------
// Inline (non-persisted) queries, transcribed verbatim
// ---------------------------------------------------------------------------

// requests.lua: updateDisplayName (updateUser mutation)
inline const QString UPDATE_DISPLAY_NAME_MUTATION = QStringLiteral(R"GQL(
mutation($input: UpdateUserInput!) {
    updateUser(input: $input) {
        error {
            code
        }
    }
}
)GQL");

// requests.lua: resubNotification (ShareResub mutation)
inline const QString SHARE_RESUB_MUTATION = QStringLiteral(R"GQL(
mutation ShareResub($input: UseChatNotificationTokenInput!) {
    useChatNotificationToken(input: $input) {
        isSuccess
    }
}
)GQL");

// requests.lua: recommendedPrefix
inline const QString RECOMMENDED_PREFIX_QUERY = QStringLiteral(R"GQL(
query UserRecommendedEmoticonPrefix($id: ID!) {
    user(id: $id) {
        recommendedEmoticonPrefix
    }
}
)GQL");

// requests.lua: get7tvid -> POST https://7tv.io/v4/gql
// NOTE: the plugin string-substituted the twitch id into the query text;
// the port substitutes it into a QString placeholder to keep the text verbatim.
inline const QString SEVENTV_USER_BY_CONNECTION_QUERY = QStringLiteral(R"GQL(
{
    users {
        userByConnection(platform: TWITCH, platformId: "%1") {
            id
        }
    }
}
)GQL");

// requests.lua: editorOf -> POST https://7tv.io/v3/gql
inline const QString SEVENTV_EDITOR_OF_QUERY = QStringLiteral(R"GQL(
query GetUserEditorOf($id: ObjectID!) {
    user(id: $id) {
        id
        display_name
        editor_of {
            user {
                id
                username
                display_name
                __typename
            }
            __typename
        }
        __typename
    }
}
)GQL");

// requests.lua: editors -> POST https://7tv.io/v4/gql
// NOTE: the plugin query fetches only mainConnection.platformDisplayName.
// `editor { id }` was added (on the nested User, not UserEditor) solely to
// enable https://7tv.app/users/<id> links. Selecting `id` on UserEditor is
// rejected by current v4 schema ("Unknown field id on type UserEditor").
inline const QString SEVENTV_ONE_USER_QUERY = QStringLiteral(R"GQL(
query OneUser($id: Id!) {
    users {
        user(id: $id) {
            editors {
                editor {
                    id
                    mainConnection {
                        platformDisplayName
                        __typename
                    }
                }
                __typename
            }
            __typename
        }
    }
}
)GQL");

// requests.lua: getFollowers -> op "follows" (channels the login FOLLOWS)
inline const QString FOLLOWS_QUERY = QStringLiteral(R"GQL(
query follows($login: String!, $cursor: Cursor, $order: SortOrder) {
    user(login: $login) {
        followedGames(first: 100, type: ALL) {
            nodes {
                displayName
            }
        }
        follows(first: 100, after: $cursor, order: $order) {
            totalCount
            edges {
                cursor
                followedAt
                notificationSettings {
                    isEnabled
                }
                node {
                    ...FollowerFragment
                }
            }
        }
    }
}

fragment FollowerFragment on User {
    login
    stream {
        id
        viewersCount
        game {
            displayName
        }
    }
}
)GQL");

// requests.lua: getFollowing -> op "followers" (the login's follower list)
inline const QString FOLLOWERS_QUERY = QStringLiteral(R"GQL(
query followers($login: String!, $cursor: Cursor, $order: SortOrder) {
    user(login: $login) {
        followers(first: 100, after: $cursor, order: $order) {
            totalCount
            edges {
                cursor
                followedAt
                node {
                    ...FollowerFragment
                }
            }
        }
    }
}

fragment FollowerFragment on User {
    login
}
)GQL");

// requests.lua: fetchMessages (/logsextended)
inline const QString VIEWER_CARD_MODLOG_MESSAGES_QUERY = QStringLiteral(R"GQL(
query TCN_ViewerCardModLogsMessagesBySender($channelID: ID!, $senderID: ID!, $cursor: Cursor) {
    viewerCardModLogs(channelID: $channelID, targetID: $senderID) {
        messages(first: 1000, after: $cursor) {
                ... on ViewerCardModLogsMessagesConnection {
                    edges {
                        ...viewerCardModLogsMessagesEdgeFragment
                        __typename
                    }
                    pageInfo {
                        hasNextPage
                        __typename
                    }
                    __typename
                }
                __typename
            }
            __typename
        }
    }

    fragment viewerCardModLogsMessagesEdgeFragment on ViewerCardModLogsMessagesEdge {
        __typename
        node {

            ...viewerCardModLogsChatMessageFragment
        }
        cursor
    }

    fragment viewerCardModLogsChatMessageFragment on ViewerCardModLogsChatMessage {
        sender {
            login
        }
        sentAt
        content {
            text
        }
        id
        isDeleted
        lastUpdatedBy {
            displayName
        }
    }
)GQL");

// Same query without the message `id` field selection (batch-11 requirement:
// /logsextended omits ids unless the -id flag is passed). ONLY the `id` line
// differs from the query above.
inline const QString VIEWER_CARD_MODLOG_MESSAGES_NOID_QUERY = QStringLiteral(R"GQL(
query TCN_ViewerCardModLogsMessagesBySender($channelID: ID!, $senderID: ID!, $cursor: Cursor) {
    viewerCardModLogs(channelID: $channelID, targetID: $senderID) {
        messages(first: 1000, after: $cursor) {
                ... on ViewerCardModLogsMessagesConnection {
                    edges {
                        ...viewerCardModLogsMessagesEdgeFragment
                        __typename
                    }
                    pageInfo {
                        hasNextPage
                        __typename
                    }
                    __typename
                }
                __typename
            }
            __typename
        }
    }

    fragment viewerCardModLogsMessagesEdgeFragment on ViewerCardModLogsMessagesEdge {
        __typename
        node {

            ...viewerCardModLogsChatMessageFragment
        }
        cursor
    }

    fragment viewerCardModLogsChatMessageFragment on ViewerCardModLogsChatMessage {
        sender {
            login
        }
        sentAt
        content {
            text
        }
        isDeleted
        lastUpdatedBy {
            displayName
        }
    }
)GQL");

}  // namespace chatterino::LimerinoAuth::gql
