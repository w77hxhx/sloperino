// SPDX-License-Identifier: MIT

#include "providers/limerino/crossban/CrossbanPresets.hpp"

#include "providers/limerino/LimerinoAuth.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace chatterino::limerino {

namespace {

// Fixed UUID so migrations can recognize the built-in default across installs.
constexpr const char *kAllModeratedId =
    "7e0c0001-11a0-41a0-a001-000000000001";
constexpr const char *kAllModeratedName = "Every moderated channel";

CrossbanChannel channelFromJson(const QJsonObject &o)
{
    return CrossbanChannel{
        o[QStringLiteral("id")].toString(),
        o[QStringLiteral("login")].toString(),
        o[QStringLiteral("displayName")].toString(),
    };
}

QJsonObject channelToJson(const CrossbanChannel &c)
{
    return QJsonObject{
        {QStringLiteral("id"), c.id},
        {QStringLiteral("login"), c.login},
        {QStringLiteral("displayName"), c.displayName},
    };
}

CrossbanPreset presetFromJson(const QJsonObject &o)
{
    CrossbanPreset p;
    p.id = QUuid::fromString(o[QStringLiteral("id")].toString());
    if (p.id.isNull())
    {
        p.id = QUuid::createUuid();
    }
    p.name = o[QStringLiteral("name")].toString().trimmed();
    p.useAllModeratedChannels =
        o[QStringLiteral("useAllModeratedChannels")].toBool(false);
    // Legacy: recognize the well-known id even if the flag was missing.
    if (p.id == allModeratedChannelsPresetId())
    {
        p.useAllModeratedChannels = true;
    }
    const auto channels = o[QStringLiteral("channels")].toArray();
    for (int i = 0; i < channels.size(); ++i)
    {
        const QJsonValue v = channels.at(i);
        if (!v.isObject())
        {
            continue;
        }
        auto c = channelFromJson(v.toObject());
        if (c.id.isEmpty() && c.login.isEmpty())
        {
            continue;
        }
        p.channels.append(std::move(c));
    }
    // Dynamic preset never keeps a persisted channel snapshot.
    if (p.useAllModeratedChannels)
    {
        p.channels.clear();
        p.name = QString::fromUtf8(kAllModeratedName);
    }
    return p;
}

QJsonObject presetToJson(const CrossbanPreset &p)
{
    QJsonArray channels;
    // Do not snapshot the live moderated list into settings.
    if (!p.useAllModeratedChannels)
    {
        for (const auto &c : p.channels)
        {
            channels.append(channelToJson(c));
        }
    }
    return QJsonObject{
        {QStringLiteral("id"), p.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), p.name},
        {QStringLiteral("useAllModeratedChannels"), p.useAllModeratedChannels},
        {QStringLiteral("channels"), channels},
    };
}

}  // namespace

QUuid allModeratedChannelsPresetId()
{
    return QUuid(QString::fromUtf8(kAllModeratedId));
}

CrossbanPreset makeAllModeratedChannelsPreset()
{
    CrossbanPreset p;
    p.id = allModeratedChannelsPresetId();
    p.name = QString::fromUtf8(kAllModeratedName);
    p.useAllModeratedChannels = true;
    return p;
}

QVector<CrossbanChannel> collectAllModeratedChannels()
{
    QVector<CrossbanChannel> out;
    QSet<QString> seenIds;
    QSet<QString> seenLogins;

    for (const auto &account : LimerinoAuth::accounts())
    {
        for (const auto &c : account.moderatedChannels)
        {
            if (!c.id.isEmpty())
            {
                if (seenIds.contains(c.id))
                {
                    continue;
                }
                seenIds.insert(c.id);
            }
            else if (!c.login.isEmpty())
            {
                const QString key = c.login.toLower();
                if (seenLogins.contains(key))
                {
                    continue;
                }
                seenLogins.insert(key);
            }
            else
            {
                continue;
            }
            out.append(CrossbanChannel{c.id, c.login, c.displayName});
        }
    }

    std::sort(out.begin(), out.end(),
              [](const CrossbanChannel &a, const CrossbanChannel &b) {
                  return a.login.toLower() < b.login.toLower();
              });
    return out;
}

bool ensureAllModeratedPreset(QVector<CrossbanPreset> &presets)
{
    for (auto &p : presets)
    {
        if (!(p.useAllModeratedChannels ||
              p.id == allModeratedChannelsPresetId()))
        {
            continue;
        }
        bool changed = false;
        if (!p.useAllModeratedChannels)
        {
            p.useAllModeratedChannels = true;
            changed = true;
        }
        if (p.id != allModeratedChannelsPresetId())
        {
            p.id = allModeratedChannelsPresetId();
            changed = true;
        }
        const QString expectedName = QString::fromUtf8(kAllModeratedName);
        if (p.name != expectedName)
        {
            p.name = expectedName;
            changed = true;
        }
        if (!p.channels.isEmpty())
        {
            p.channels.clear();
            changed = true;
        }
        return changed;
    }
    presets.prepend(makeAllModeratedChannelsPreset());
    return true;
}

QVector<CrossbanPreset> loadCrossbanPresets()
{
    QVector<CrossbanPreset> out;
    if (Settings::hasInstance())
    {
        const auto raw = getSettings()->limerinoCrossbanPresets.getValue();
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray())
        {
            const auto arr = doc.array();
            for (int i = 0; i < arr.size(); ++i)
            {
                const QJsonValue v = arr.at(i);
                if (!v.isObject())
                {
                    continue;
                }
                auto p = presetFromJson(v.toObject());
                if (p.name.isEmpty())
                {
                    continue;
                }
                out.append(std::move(p));
            }
        }
    }

    const bool mutated = ensureAllModeratedPreset(out);
    if (mutated && Settings::hasInstance())
    {
        saveCrossbanPresets(out);
    }

    if (Settings::hasInstance() && lastCrossbanPresetId().isNull())
    {
        setLastCrossbanPresetId(allModeratedChannelsPresetId());
    }

    return out;
}

void saveCrossbanPresets(const QVector<CrossbanPreset> &presets)
{
    if (!Settings::hasInstance())
    {
        return;
    }
    QJsonArray arr;
    for (const auto &p : presets)
    {
        arr.append(presetToJson(p));
    }
    getSettings()->limerinoCrossbanPresets.setValue(
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QUuid lastCrossbanPresetId()
{
    if (!Settings::hasInstance())
    {
        return {};
    }
    return QUuid::fromString(
        getSettings()->limerinoCrossbanLastPresetId.getValue());
}

void setLastCrossbanPresetId(const QUuid &id)
{
    if (!Settings::hasInstance())
    {
        return;
    }
    getSettings()->limerinoCrossbanLastPresetId.setValue(
        id.isNull() ? QString() : id.toString(QUuid::WithoutBraces));
}

}  // namespace chatterino::limerino
