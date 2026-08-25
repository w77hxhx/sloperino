// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoThemeDialog.hpp"

#include "Application.hpp"
#include "providers/limerino/theme/LimerinoThemeGenerator.hpp"
#include "providers/limerino/theme/LimerinoThemeStore.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Theme.hpp"
#include "widgets/dialogs/limerino/LimerinoColorField.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace {

constexpr const char *PREVIEW_FILENAME = "_LimerinoPreview.json";

}  // namespace

LimerinoThemeDialog::LimerinoThemeDialog(QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
{
    this->setWindowTitle(QStringLiteral("Create theme (Limerino)"));
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->buildUi();

    bool loadedCurrent = false;
    if (auto *theme = getTheme(); theme != nullptr)
    {
        const QString current = theme->themeName.getValue();
        if (!current.isEmpty() &&
            !current.startsWith(QLatin1Char('_')) &&
            current.compare(QStringLiteral("System"), Qt::CaseInsensitive) != 0)
        {
            const QString path =
                QDir(getApp()->getPaths().themesDirectory).filePath(current);
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                const auto doc = QJsonDocument::fromJson(file.readAll());
                loadedCurrent = this->applyBaseJson(doc.object(), current);
            }
            if (!loadedCurrent)
            {
                if (current.compare(QStringLiteral("Dark"),
                                    Qt::CaseInsensitive) == 0)
                {
                    this->applyBuiltinBase(BuiltinTheme::Dark);
                    loadedCurrent = true;
                }
                else if (current.compare(QStringLiteral("Light"),
                                         Qt::CaseInsensitive) == 0)
                {
                    this->applyBuiltinBase(BuiltinTheme::Light);
                    loadedCurrent = true;
                }
                else if (current.compare(QStringLiteral("Black"),
                                         Qt::CaseInsensitive) == 0)
                {
                    this->applyBuiltinBase(BuiltinTheme::Black);
                    loadedCurrent = true;
                }
                else if (current.compare(QStringLiteral("White"),
                                         Qt::CaseInsensitive) == 0)
                {
                    this->applyBuiltinBase(BuiltinTheme::White);
                    loadedCurrent = true;
                }
            }
        }
    }
    if (!loadedCurrent)
    {
        this->applyBuiltinBase(BuiltinTheme::Dark);
    }

    this->rebuildRecentsCombo();
    this->syncUiFromSeed();
    this->startLivePreview();
}

void LimerinoThemeDialog::closeEvent(QCloseEvent *event)
{
    this->teardownPreview(!this->applied_);
    BasePopup::closeEvent(event);
}

void LimerinoThemeDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    {
        auto *nameRow = new QHBoxLayout;
        nameRow->addWidget(new QLabel(QStringLiteral("Name")));
        this->nameEdit_ = new QLineEdit(QStringLiteral("My theme"), this);
        nameRow->addWidget(this->nameEdit_, 1);
        root->addLayout(nameRow);
    }

    {
        auto *recentRow = new QHBoxLayout;
        recentRow->addWidget(new QLabel(QStringLiteral("Recent")));
        this->recentCombo_ = new QComboBox(this);
        QObject::connect(this->recentCombo_,
                         QOverload<int>::of(&QComboBox::activated), this,
                         &LimerinoThemeDialog::onRecentSelected);
        recentRow->addWidget(this->recentCombo_, 1);
        root->addLayout(recentRow);
    }

    this->baseLabel_ = new QLabel(this);
    this->baseLabel_->setWordWrap(true);
    root->addWidget(this->baseLabel_);

    {
        auto *presetRow = new QHBoxLayout;
        auto *darkBtn = new QPushButton(QStringLiteral("Dark.json"), this);
        auto *lightBtn = new QPushButton(QStringLiteral("Light.json"), this);
        auto *blackBtn = new QPushButton(QStringLiteral("Black.json"), this);
        auto *whiteBtn = new QPushButton(QStringLiteral("White.json"), this);
        QObject::connect(darkBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onDarkPreset);
        QObject::connect(lightBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onLightPreset);
        QObject::connect(blackBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onBlackPreset);
        QObject::connect(whiteBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onWhitePreset);
        presetRow->addWidget(darkBtn);
        presetRow->addWidget(lightBtn);
        presetRow->addWidget(blackBtn);
        presetRow->addWidget(whiteBtn);
        presetRow->addStretch(1);
        root->addLayout(presetRow);
    }

    auto makeColorRow = [this](const QString &label, SeedField field,
                               LimerinoColorField *&fieldWidget) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this), 1);
        fieldWidget = new LimerinoColorField(this);
        QObject::connect(fieldWidget, &LimerinoColorField::colorChanged, this,
                         [this, field](QColor color) {
                             this->setFieldColor(field, color);
                         });
        row->addWidget(fieldWidget, 1);
        return row;
    };

    root->addLayout(makeColorRow(QStringLiteral("Background"),
                                SeedField::Background, this->backgroundField_));
    root->addLayout(makeColorRow(QStringLiteral("Surface"), SeedField::Surface,
                                this->surfaceField_));
    root->addLayout(makeColorRow(QStringLiteral("Accent"), SeedField::Accent,
                                this->accentField_));
    root->addLayout(makeColorRow(QStringLiteral("Text / font color"),
                                SeedField::Text, this->textField_));

    this->warningsLabel_ = new QLabel(this);
    this->warningsLabel_->setWordWrap(true);
    this->warningsLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    root->addWidget(this->warningsLabel_);

    {
        auto *buttons = new QHBoxLayout;
        auto *importBtn = new QPushButton(QStringLiteral("Import..."), this);
        auto *exportSeedBtn =
            new QPushButton(QStringLiteral("Export seed..."), this);
        auto *exportThemeBtn =
            new QPushButton(QStringLiteral("Export theme..."), this);
        auto *applyBtn =
            new QPushButton(QStringLiteral("Apply & close"), this);
        auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
        QObject::connect(importBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onImport);
        QObject::connect(exportSeedBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onExportSeed);
        QObject::connect(exportThemeBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onExportTheme);
        QObject::connect(applyBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onApply);
        QObject::connect(cancelBtn, &QPushButton::clicked, this,
                         &LimerinoThemeDialog::onCancel);
        buttons->addWidget(importBtn);
        buttons->addWidget(exportSeedBtn);
        buttons->addWidget(exportThemeBtn);
        buttons->addStretch(1);
        buttons->addWidget(applyBtn);
        buttons->addWidget(cancelBtn);
        root->addLayout(buttons);
    }
}

void LimerinoThemeDialog::syncUiFromSeed()
{
    this->backgroundField_->setColor(this->seed_.background);
    this->surfaceField_->setColor(this->seed_.surface);
    this->accentField_->setColor(this->seed_.accent);
    this->textField_->setColor(this->seed_.text);
    this->refreshWarnings();
}

void LimerinoThemeDialog::setFieldColor(SeedField field, const QColor &color)
{
    if (!color.isValid())
    {
        return;
    }
    this->fieldColor(field) = color;
    this->refreshWarnings();
    this->writePreviewAndReload();
}

QColor &LimerinoThemeDialog::fieldColor(SeedField field)
{
    switch (field)
    {
        case SeedField::Background:
            return this->seed_.background;
        case SeedField::Surface:
            return this->seed_.surface;
        case SeedField::Accent:
            return this->seed_.accent;
        case SeedField::Text:
            return this->seed_.text;
    }
    return this->seed_.background;
}

const QColor &LimerinoThemeDialog::fieldColor(SeedField field) const
{
    switch (field)
    {
        case SeedField::Background:
            return this->seed_.background;
        case SeedField::Surface:
            return this->seed_.surface;
        case SeedField::Accent:
            return this->seed_.accent;
        case SeedField::Text:
            return this->seed_.text;
    }
    return this->seed_.background;
}

void LimerinoThemeDialog::refreshWarnings()
{
    const auto warnings = contrastWarnings(this->seed_);
    if (warnings.isEmpty())
    {
        this->warningsLabel_->clear();
        return;
    }
    this->warningsLabel_->setText(QStringLiteral("Low contrast: %1")
                                      .arg(warnings.join(QStringLiteral("; "))));
}

QString LimerinoThemeDialog::previewFilePath() const
{
    return QDir(getApp()->getPaths().themesDirectory)
        .filePath(QString::fromUtf8(PREVIEW_FILENAME));
}

QJsonObject LimerinoThemeDialog::currentThemeJson() const
{
    if (this->baseJson_.isEmpty())
    {
        return generateTheme(this->seed_);
    }
    return generateThemeFromBase(this->baseJson_, this->baseSeed_, this->seed_);
}

void LimerinoThemeDialog::writePreviewAndReload()
{
    if (!this->previewActive_)
    {
        return;
    }

    const QString path = this->previewFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return;
    }
    file.write(QJsonDocument(this->currentThemeJson())
                   .toJson(QJsonDocument::Indented));
    file.close();

    if (auto *theme = getTheme(); theme != nullptr)
    {
        theme->update();
    }
}

void LimerinoThemeDialog::startLivePreview()
{
    auto *theme = getTheme();
    if (theme == nullptr)
    {
        return;
    }

    this->previousThemeName_ = theme->themeName.getValue();
    this->previewActive_ = true;

    const QString path = this->previewFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        this->previewActive_ = false;
        QMessageBox::warning(
            this, QStringLiteral("Theme creator"),
            QStringLiteral("Could not write preview theme to:\n%1").arg(path));
        return;
    }
    file.write(QJsonDocument(this->currentThemeJson())
                   .toJson(QJsonDocument::Indented));
    file.close();

    theme->rescanCustomThemes(getApp()->getPaths());
    theme->themeName.setValue(QString::fromUtf8(PREVIEW_FILENAME));
    theme->setAutoReload(true);
}

void LimerinoThemeDialog::teardownPreview(bool restorePreviousTheme)
{
    if (!this->previewActive_)
    {
        return;
    }
    this->previewActive_ = false;

    if (auto *theme = getTheme(); theme != nullptr)
    {
        theme->setAutoReload(false);
        if (restorePreviousTheme && !this->previousThemeName_.isEmpty())
        {
            theme->themeName.setValue(this->previousThemeName_);
        }
    }

    QFile::remove(this->previewFilePath());
}

bool LimerinoThemeDialog::isReservedThemeFilename(
    const QString &filename) const
{
    static const QStringList reserved{
        QStringLiteral("Black.json"),
        QStringLiteral("Dark.json"),
        QStringLiteral("Light.json"),
        QStringLiteral("White.json"),
    };
    return reserved.contains(filename, Qt::CaseInsensitive);
}

bool LimerinoThemeDialog::applyBaseJson(const QJsonObject &json,
                                        const QString &displayName)
{
    if (json.isEmpty() || !json.contains(QStringLiteral("colors")))
    {
        return false;
    }
    this->baseJson_ = json;
    this->baseSeed_ = seedFromThemeJson(json);
    this->seed_ = this->baseSeed_;
    if (this->baseLabel_ != nullptr)
    {
        this->baseLabel_->setText(
            QStringLiteral("Base: %1 (only matching seed colours are "
                           "recolored; other leaves stay from this file).")
                .arg(displayName));
    }
    return true;
}

void LimerinoThemeDialog::applyBuiltinBase(BuiltinTheme builtin)
{
    const auto json = loadBuiltinThemeJson(builtin);
    if (!json.has_value() ||
        !this->applyBaseJson(*json, builtinThemeName(builtin) +
                                        QStringLiteral(".json")))
    {
        this->baseJson_ = {};
        this->baseSeed_ = builtin == BuiltinTheme::Light ||
                                  builtin == BuiltinTheme::White
                              ? LimerinoThemeSeed::lightPreset()
                              : LimerinoThemeSeed::darkPreset();
        this->seed_ = this->baseSeed_;
        if (this->baseLabel_ != nullptr)
        {
            this->baseLabel_->setText(QStringLiteral(
                "Base JSON missing from resources; using synthesized colours."));
        }
    }
}

void LimerinoThemeDialog::rebuildRecentsCombo()
{
    if (this->recentCombo_ == nullptr)
    {
        return;
    }
    const QSignalBlocker block(this->recentCombo_);
    this->recentCombo_->clear();
    this->recentCombo_->addItem(QStringLiteral("(recent themes)"));
    for (const auto &filename : loadThemeRecents())
    {
        this->recentCombo_->addItem(QFileInfo(filename).completeBaseName(),
                                    filename);
    }
}

void LimerinoThemeDialog::onDarkPreset()
{
    this->applyBuiltinBase(BuiltinTheme::Dark);
    this->syncUiFromSeed();
    this->writePreviewAndReload();
}

void LimerinoThemeDialog::onLightPreset()
{
    this->applyBuiltinBase(BuiltinTheme::Light);
    this->syncUiFromSeed();
    this->writePreviewAndReload();
}

void LimerinoThemeDialog::onBlackPreset()
{
    this->applyBuiltinBase(BuiltinTheme::Black);
    this->syncUiFromSeed();
    this->writePreviewAndReload();
}

void LimerinoThemeDialog::onWhitePreset()
{
    this->applyBuiltinBase(BuiltinTheme::White);
    this->syncUiFromSeed();
    this->writePreviewAndReload();
}

void LimerinoThemeDialog::onRecentSelected(int index)
{
    if (index <= 0)
    {
        return;
    }
    const QString filename = this->recentCombo_->itemData(index).toString();
    if (filename.isEmpty())
    {
        return;
    }
    const QString path =
        QDir(getApp()->getPaths().themesDirectory).filePath(filename);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            this, QStringLiteral("Recent theme"),
            QStringLiteral("Could not read %1").arg(path));
        return;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!this->applyBaseJson(doc.object(), filename))
    {
        QMessageBox::warning(this, QStringLiteral("Recent theme"),
                             QStringLiteral("%1 is not a theme JSON.")
                                 .arg(filename));
        return;
    }
    this->nameEdit_->setText(QFileInfo(filename).completeBaseName());
    this->syncUiFromSeed();
    this->writePreviewAndReload();
}

void LimerinoThemeDialog::onImport()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import theme"), QString(),
        QStringLiteral("JSON (*.json);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("Import"),
                             QStringLiteral("Could not read %1").arg(path));
        return;
    }
    const auto doc = QJsonDocument::fromJson(in.readAll());
    const auto root = doc.object();
    if (root.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("Import"),
                             QStringLiteral("File is not valid JSON."));
        return;
    }

    if (isSeedFile(root))
    {
        QString err;
        const auto parsed = parseSeedFile(root, &err);
        if (!parsed.has_value())
        {
            QMessageBox::warning(
                this, QStringLiteral("Import"),
                err.isEmpty() ? QStringLiteral("Could not parse seed file.")
                              : err);
            return;
        }
        this->seed_ = parsed->seed;
        this->nameEdit_->setText(parsed->name);
        this->syncUiFromSeed();
        this->writePreviewAndReload();
        return;
    }

    if (this->applyBaseJson(root, QFileInfo(path).fileName()))
    {
        this->nameEdit_->setText(QFileInfo(path).completeBaseName());
        this->syncUiFromSeed();
        this->writePreviewAndReload();
        return;
    }

    QMessageBox::warning(this, QStringLiteral("Import"),
                         QStringLiteral("File is not a theme JSON or seed."));
}

void LimerinoThemeDialog::onExportSeed()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export seed"),
        this->nameEdit_->text().trimmed() + QStringLiteral(".limerino_theme.json"),
        QStringLiteral("Limerino seed (*.json);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("Could not write %1").arg(path));
        return;
    }
    const auto json =
        serializeSeedFile(this->nameEdit_->text().trimmed(), this->seed_);
    out.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
}

void LimerinoThemeDialog::onExportTheme()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export theme"),
        this->nameEdit_->text().trimmed() + QStringLiteral(".json"),
        QStringLiteral("Chatterino theme (*.json);;All files (*)"));
    if (path.isEmpty())
    {
        return;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, QStringLiteral("Export"),
                             QStringLiteral("Could not write %1").arg(path));
        return;
    }
    out.write(QJsonDocument(this->currentThemeJson())
                  .toJson(QJsonDocument::Indented));
}

void LimerinoThemeDialog::onApply()
{
    const QString name = this->nameEdit_->text().trimmed();
    if (name.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("Apply"),
                             QStringLiteral("Name cannot be empty."));
        return;
    }

    const QString filename = sanitizeThemeFilename(name);
    if (this->isReservedThemeFilename(filename))
    {
        QMessageBox::warning(
            this, QStringLiteral("Apply"),
            QStringLiteral(
                "\"%1\" collides with a built-in theme. Choose another name.")
                .arg(filename));
        return;
    }

    QString err;
    if (!installThemeJson(name, this->currentThemeJson(), &err))
    {
        QMessageBox::warning(
            this, QStringLiteral("Apply"),
            err.isEmpty() ? QStringLiteral("Could not install theme.") : err);
        return;
    }

    this->applied_ = true;
    this->previewActive_ = false;
    if (auto *theme = getTheme(); theme != nullptr)
    {
        theme->setAutoReload(false);
    }
    QFile::remove(this->previewFilePath());
    this->close();
}

void LimerinoThemeDialog::onCancel()
{
    this->close();
}

}  // namespace chatterino::limerino
