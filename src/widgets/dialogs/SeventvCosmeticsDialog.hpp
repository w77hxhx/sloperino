// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/ImageSet.hpp"
#include "providers/seventv/paints/Paint.hpp"
#include "widgets/DraggablePopup.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;

namespace chatterino {

class Button;
class SvgButton;
class TwitchChannel;
class CosmeticPreviewWidget;

struct SeventvPaintItem {
    QString id;
    QString name;
    QJsonObject rawJson;
    std::shared_ptr<Paint> paint;
};

struct SeventvBadgeItem {
    QString id;
    QString name;
    QString description;
    QString imageUrl;
    ImageSet images;
};

struct SeventvConnection {
    QString id;
    QString platform;
    QString username;
    QString displayName;
};

class SeventvCosmeticsDialog : public DraggablePopup
{
public:
    SeventvCosmeticsDialog(TwitchChannel *channel, QWidget *parent = nullptr);

    static void showDialog(TwitchChannel *channel, QWidget *parent = nullptr);

protected:
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class View {
        Badges,
        Paints,
    };

    void loadCosmetics(bool force = false);
    void switchView(View view);
    void rebuildContent();
    void rebuildBadges();
    void rebuildPaints();
    void clearContent();
    void refreshStyle();
    void setStatus(const QString &text, bool error = false);

    void selectPaint(const QString &paintId);
    void selectBadge(const QString &badgeId);
    void sendPresence();
    void applyCosmeticsToChatterino();

    QString getSeventvToken() const;
    QString getSeventvUserId() const;

    void applySizeConstraints();
    void updatePreview();

    TwitchChannel *channel_{};

    QVBoxLayout *mainLayout_{};
    QWidget *headerWidget_{};
    QLabel *headerTitleLabel_{};
    Button *pinButton_{};
    SvgButton *closeButton_{};

    CosmeticPreviewWidget *previewWidget_{};

    QWidget *controlsRowWidget_{};
    QPushButton *badgesTabButton_{};
    QPushButton *paintsTabButton_{};
    QLineEdit *searchInput_{};

    QScrollArea *scrollArea_{};
    QWidget *contentWidget_{};
    QVBoxLayout *contentLayout_{};
    QLabel *statusLabel_{};

    View currentView_{View::Badges};
    QString searchQuery_;
    QString statusText_;
    bool statusIsError_{false};
    bool loading_{false};
    bool loaded_{false};

    QString seventvUserId_;
    QString seventvUsername_;
    QString seventvDisplayName_;
    QColor seventvColor_;
    QString activePaintId_;
    QString activeBadgeId_;

    std::vector<SeventvConnection> connections_;
    std::map<QString, SeventvPaintItem> allPaintsMap_;
    std::map<QString, SeventvBadgeItem> allBadgesMap_;
    std::vector<SeventvPaintItem> paints_;
    std::vector<SeventvBadgeItem> badges_;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
