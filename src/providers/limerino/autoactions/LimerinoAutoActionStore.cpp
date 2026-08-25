// SPDX-License-Identifier: MIT

#include "providers/limerino/autoactions/LimerinoAutoActionStore.hpp"

#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace chatterino::limerino {

namespace {

QJsonArray rulesToJson(const QVector<LimerinoAutoAction> &rules)
{
    QJsonArray arr;

    rapidjson::Document doc;
    auto &a = doc.GetAllocator();
    for (const auto &r : rules)
    {
        auto v = pajlada::Serialize<LimerinoAutoAction>::get(r, a);
        rapidjson::StringBuffer buf;
        rapidjson::Writer<rapidjson::StringBuffer> w(buf);
        v.Accept(w);
        arr.append(QJsonDocument::fromJson(
                       QByteArray(buf.GetString(),
                                  static_cast<int>(buf.GetSize())))
                       .object());
    }
    return arr;
}

QVector<LimerinoAutoAction> rulesFromJson(const QJsonArray &arr)
{
    QVector<LimerinoAutoAction> out;
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
        bool error = false;
        auto rule =
            pajlada::Deserialize<LimerinoAutoAction>::get(doc, &error);
        if (!error)
        {
            out.append(std::move(rule));
        }
    }
    return out;
}

}  // namespace

QVector<LimerinoAutoAction> loadLimerinoAutoActions()
{
    const auto raw = getSettings()->limerinoAutoActions.getValue();
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        return {};
    }
    return rulesFromJson(doc.array());
}

void saveLimerinoAutoActions(const QVector<LimerinoAutoAction> &rules)
{
    const auto json =
        QJsonDocument(rulesToJson(rules)).toJson(QJsonDocument::Compact);
    getSettings()->limerinoAutoActions.setValue(QString::fromUtf8(json));
}

void addLimerinoAutoAction(LimerinoAutoAction rule)
{
    auto rules = loadLimerinoAutoActions();
    rules.append(std::move(rule));
    saveLimerinoAutoActions(rules);
}

void updateLimerinoAutoAction(const LimerinoAutoAction &rule)
{
    auto rules = loadLimerinoAutoActions();
    for (auto &r : rules)
    {
        if (r.id == rule.id)
        {
            r = rule;
            break;
        }
    }
    saveLimerinoAutoActions(rules);
}

void removeLimerinoAutoAction(const QUuid &id)
{
    auto rules = loadLimerinoAutoActions();
    for (int i = 0; i < rules.size(); ++i)
    {
        if (rules[i].id == id)
        {
            rules.removeAt(i);
            break;
        }
    }
    saveLimerinoAutoActions(rules);
}

}  // namespace chatterino::limerino

