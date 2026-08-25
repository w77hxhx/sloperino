// SPDX-License-Identifier: MIT

#include "providers/limerino/theme/LimerinoThemeStore.hpp"

#include "Application.hpp"
#include "providers/limerino/theme/LimerinoThemeGenerator.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace chatterino::limerino {

namespace {

constexpr int LIMERINO_THEME_SEED_VERSION = 1;

QString colorHex(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor parseColorHex(const QString &s, const QColor &fallback)
{
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

}  // namespace

QJsonObject serializeSeedFile(const QString &name, const LimerinoThemeSeed &seed)
{
    return QJsonObject{
        {QStringLiteral("limerinoThemeVersion"), LIMERINO_THEME_SEED_VERSION},
        {QStringLiteral("name"), name},
        {QStringLiteral("seed"),
         QJsonObject{
             {QStringLiteral("background"), colorHex(seed.background)},
             {QStringLiteral("surface"), colorHex(seed.surface)},
             {QStringLiteral("accent"), colorHex(seed.accent)},
             {QStringLiteral("text"), colorHex(seed.text)},
         }},
    };
}

bool isSeedFile(const QJsonObject &root)
{
    return root.contains(QStringLiteral("limerinoThemeVersion"));
}

std::optional<LimerinoThemeFile> parseSeedFile(const QJsonObject &root,
                                               QString *err)
{
    const auto bad = [&](const QString &reason) {
        if (err)
        {
            *err = reason;
        }
        return std::nullopt;
    };

    if (!isSeedFile(root))
    {
        return bad(QStringLiteral("not a limerino_theme.json seed file"));
    }
    if (root[QStringLiteral("limerinoThemeVersion")].toInt(-1) != 1)
    {
        return bad(QStringLiteral("unsupported limerinoThemeVersion"));
    }

    const auto seedObj = root[QStringLiteral("seed")].toObject();
    if (seedObj.isEmpty())
    {
        return bad(QStringLiteral("seed object missing"));
    }

    LimerinoThemeFile out{};
    out.name = root[QStringLiteral("name")].toString().trimmed();
    if (out.name.isEmpty())
    {
        out.name = QStringLiteral("Limerino theme");
    }

    // A missing key keeps its family default; an unparsable key keeps it too -
    // import never produces an invalid theme.
    const LimerinoThemeSeed defaults = LimerinoThemeSeed::darkPreset();
    const auto readSeedColor = [&](QLatin1String key, const QColor &def) {
        const auto raw = seedObj.value(key).toString();
        if (raw.isEmpty())
        {
            return def;
        }
        return parseColorHex(raw, def);
    };
    out.seed.background =
        readSeedColor(QLatin1String("background"), defaults.background);
    out.seed.surface = readSeedColor(QLatin1String("surface"), defaults.surface);
    out.seed.accent = readSeedColor(QLatin1String("accent"), defaults.accent);
    out.seed.text = readSeedColor(QLatin1String("text"), defaults.text);
    return out;
}

QString sanitizeThemeFilename(const QString &name)
{
    QString out = name.trimmed();
    // Windows-forbidden path characters, plus path separators for safety.
    static const QString illegal = QStringLiteral("<>:\"/\\|?*");
    for (const auto ch : illegal)
    {
        out.remove(ch);
    }
    while (out.contains(QStringLiteral("  ")))
    {
        out.replace(QStringLiteral("  "), QStringLiteral(" "));
    }
    if (out.isEmpty())
    {
        out = QStringLiteral("Limerino theme");
    }
    // Theme keys are the filename with extension (loadAvailableThemes), so a
    // user-facing name always maps to "<name>.json" exactly once.
    if (!out.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
    {
        out += QStringLiteral(".json");
    }
    return out;
}

bool installThemeJson(const QString &name, const QJsonObject &themeJson,
                      QString *err)
{
    const auto setErr = [&](const QString &e) {
        if (err)
        {
            *err = e;
        }
        return false;
    };

    if (themeJson.isEmpty() || !themeJson.contains(QStringLiteral("colors")) ||
        !themeJson.contains(QStringLiteral("metadata")))
    {
        return setErr(QStringLiteral("not a theme JSON (no colors/metadata)"));
    }

    const QString filename = sanitizeThemeFilename(name);
    const QString path = QDir(getApp()->getPaths().themesDirectory)
                             .filePath(filename);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return setErr(QStringLiteral("failed to write %1").arg(path));
    }
    file.write(QJsonDocument(themeJson).toJson(QJsonDocument::Indented));
    file.close();

    if (auto *theme = getTheme(); theme != nullptr)
    {
        theme->rescanCustomThemes(getApp()->getPaths());
        theme->themeName.setValue(filename);
    }
    pushThemeRecent(filename);
    return true;
}

bool installGeneratedTheme(const QString &name, const LimerinoThemeSeed &seed,
                           QString *err)
{
    return installThemeJson(name, generateTheme(seed), err);
}

bool installFullThemeFile(const QString &sourcePath, const QString &name,
                          QString *err)
{
    const auto setErr = [&](const QString &e) {
        if (err)
        {
            *err = e;
        }
        return false;
    };

    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return setErr(QStringLiteral("failed to read %1").arg(sourcePath));
    }
    const auto bytes = in.readAll();
    // Sanity: must already be a theme JSON (has colors + metadata), else the
    // import-as-is would silently install a file that isn't a theme.
    const auto doc = QJsonDocument::fromJson(bytes);
    const auto root = doc.object();
    if (root.isEmpty() || !root.contains(QStringLiteral("colors")) ||
        !root.contains(QStringLiteral("metadata")))
    {
        return setErr(QStringLiteral("not a theme JSON (no colors/metadata)"));
    }

    const QString filename = sanitizeThemeFilename(name);
    const QString path = QDir(getApp()->getPaths().themesDirectory)
                             .filePath(filename);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return setErr(QStringLiteral("failed to write %1").arg(path));
    }
    out.write(bytes);
    out.close();

    if (auto *theme = getTheme(); theme != nullptr)
    {
        theme->rescanCustomThemes(getApp()->getPaths());
        theme->themeName.setValue(filename);
    }
    pushThemeRecent(filename);
    return true;
}

QStringList loadThemeRecents()
{
    if (!Settings::hasInstance())
    {
        return {};
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(
        getSettings()->limerinoThemeRecents.getValue().toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        return {};
    }
    QStringList out;
    const auto arr = doc.array();
    for (int i = 0; i < arr.size(); ++i)
    {
        const QString name = arr.at(i).toString().trimmed();
        if (name.isEmpty() || out.contains(name, Qt::CaseInsensitive))
        {
            continue;
        }
        out.append(name);
        if (out.size() >= kThemeRecentsLimit)
        {
            break;
        }
    }
    return out;
}

void pushThemeRecent(const QString &filename)
{
    if (!Settings::hasInstance())
    {
        return;
    }
    const QString clean = sanitizeThemeFilename(filename);
    if (clean.startsWith(QLatin1Char('_')))
    {
        return;  // preview files
    }
    QStringList recents = loadThemeRecents();
    recents.removeAll(clean);
    recents.prepend(clean);
    while (recents.size() > kThemeRecentsLimit)
    {
        recents.removeLast();
    }
    QJsonArray arr;
    for (const auto &item : recents)
    {
        arr.append(item);
    }
    getSettings()->limerinoThemeRecents.setValue(
        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

}  // namespace chatterino::limerino
