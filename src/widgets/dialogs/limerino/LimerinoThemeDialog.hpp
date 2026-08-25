// SPDX-License-Identifier: MIT
// Theme creator dialog (batch T3).
//
// Recolors a real base theme JSON (Dark/Light/Black/White or a recent custom
// file) using four seed colours. Live preview via Theme::setAutoReload +
// Themes/_LimerinoPreview.json.

#pragma once

#include "providers/limerino/theme/LimerinoThemeGenerator.hpp"
#include "providers/limerino/theme/LimerinoThemeSeed.hpp"
#include "widgets/BasePopup.hpp"

#include <QJsonObject>
#include <QString>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;

namespace chatterino::limerino {

class LimerinoColorField;

class LimerinoThemeDialog : public BasePopup
{
    Q_OBJECT

public:
    explicit LimerinoThemeDialog(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class SeedField {
        Background,
        Surface,
        Accent,
        Text,
    };

    void buildUi();
    void syncUiFromSeed();
    void setFieldColor(SeedField field, const QColor &color);
    QColor &fieldColor(SeedField field);
    const QColor &fieldColor(SeedField field) const;

    void refreshWarnings();
    void writePreviewAndReload();
    void startLivePreview();
    void teardownPreview(bool restorePreviousTheme);

    void applyBuiltinBase(BuiltinTheme builtin);
    bool applyBaseJson(const QJsonObject &json, const QString &displayName);
    void rebuildRecentsCombo();
    QJsonObject currentThemeJson() const;

    void onDarkPreset();
    void onLightPreset();
    void onBlackPreset();
    void onWhitePreset();
    void onRecentSelected(int index);
    void onImport();
    void onExportSeed();
    void onExportTheme();
    void onApply();
    void onCancel();

    bool isReservedThemeFilename(const QString &filename) const;
    QString previewFilePath() const;

    QJsonObject baseJson_;
    LimerinoThemeSeed baseSeed_ = LimerinoThemeSeed::darkPreset();
    LimerinoThemeSeed seed_ = LimerinoThemeSeed::darkPreset();
    bool applied_ = false;
    bool previewActive_ = false;
    QString previousThemeName_;

    QLineEdit *nameEdit_ = nullptr;
    QComboBox *recentCombo_ = nullptr;
    QLabel *baseLabel_ = nullptr;

    LimerinoColorField *backgroundField_ = nullptr;
    LimerinoColorField *surfaceField_ = nullptr;
    LimerinoColorField *accentField_ = nullptr;
    LimerinoColorField *textField_ = nullptr;

    QLabel *warningsLabel_ = nullptr;
};

}  // namespace chatterino::limerino
