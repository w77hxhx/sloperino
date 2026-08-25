// SPDX-License-Identifier: MIT

#include "providers/limerino/nuke/NukePresetsStore.hpp"

#include "providers/limerino/nuke/NukePreset.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace chatterino::limerino {

namespace {

QJsonArray presetsToJson(const QVector<LimerinoNukePreset> &presets)
{
    QJsonArray arr;

    // Route through the pajlada serde so the persisted shape is the canonical
    // one (and the Qt-json / rapidjson boundary is confined to this module).
    rapidjson::Document tmp;
    auto &a = tmp.GetAllocator();
    for (const auto &p : presets)
    {
        auto rj = pajlada::Serialize<LimerinoNukePreset>::get(p, a);
        const auto bytes = [&] {
            rapidjson::StringBuffer buf;
            rapidjson::Writer<rapidjson::StringBuffer> w(buf);
            rj.Accept(w);
            return QByteArray(buf.GetString(),
                              static_cast<int>(buf.GetSize()));
        }();
        arr.append(QJsonDocument::fromJson(bytes).object());
    }
    return arr;
}

QVector<LimerinoNukePreset> presetsFromJson(const QJsonArray &arr)
{
    QVector<LimerinoNukePreset> out;
    out.reserve(arr.size());
    for (const auto &v : arr)
    {
        if (!v.isObject())
        {
            continue;
        }
        const auto bytes =
            QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
        rapidjson::Document doc;
        doc.Parse(bytes.constData(), bytes.size());
        if (doc.HasParseError())
        {
            continue;
        }
        bool parseError = false;
        auto preset =
            pajlada::Deserialize<LimerinoNukePreset>::get(doc, &parseError);
        if (!parseError && !preset.name.isEmpty())
        {
            out.append(std::move(preset));
        }
    }
    return out;
}

}  // namespace

QVector<LimerinoNukePreset> loadNukePresets()
{
    const auto raw = getSettings()->limerinoNukePresets.getValue();
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        return {};
    }
    return presetsFromJson(doc.array());
}

void saveNukePresets(const QVector<LimerinoNukePreset> &presets)
{
    const auto json =
        QJsonDocument(presetsToJson(presets)).toJson(QJsonDocument::Compact);
    getSettings()->limerinoNukePresets.setValue(QString::fromUtf8(json));
}

void upsertNukePreset(const LimerinoNukePreset &preset)
{
    auto presets = loadNukePresets();
    bool replaced = false;
    for (auto &p : presets)
    {
        if (p.name.compare(preset.name, Qt::CaseInsensitive) == 0)
        {
            p = preset;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        presets.append(preset);
    }
    saveNukePresets(presets);
}

void eraseNukePreset(const QString &name)
{
    auto presets = loadNukePresets();
    for (int i = 0; i < presets.size(); ++i)
    {
        if (presets[i].name.compare(name, Qt::CaseInsensitive) == 0)
        {
            presets.removeAt(i);
            break;
        }
    }
    saveNukePresets(presets);
}

}  // namespace chatterino::limerino
