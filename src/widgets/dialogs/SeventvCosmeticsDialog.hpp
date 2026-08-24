// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Message.hpp"
#include "widgets/DraggablePopup.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QResizeEvent;
class QShowEvent;
class QVBoxLayout;

namespace chatterino {

class MessageView;
class SvgButton;
class Button;
class TwitchChannel;
class Paint;

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
        Paints,
        Badges,
    };

    void loadCosmetics(bool force = false);
    void switchView(View view);
    void rebuildContent();
    void rebuildPaints();
    void rebuildBadges();
    void clearContent();
    void refreshStyle();
    void setStatus(const QString &text, bool error = false);

    void selectPaint(const QString &paintId);
    void selectBadge(const QString &badgeId);
    void sendPresence();

    QString getSeventvToken() const;
    QString getSeventvUserId() const;

    void applySizeConstraints();
    void updatePreview();
    [[nodiscard]] MessagePtr buildPreviewMessage() const;

    TwitchChannel *channel_{};

    QVBoxLayout *mainLayout_{};
    QWidget *headerWidget_{};
    QLabel *headerTitleLabel_{};
    QPushButton *paintsTabButton_{};
    QPushButton *badgesTabButton_{};
    QWidget *searchRowWidget_{};
    QLineEdit *searchInput_{};
    QScrollArea *scrollArea_{};
    QWidget *contentWidget_{};
    QVBoxLayout *contentLayout_{};
    QLabel *statusLabel_{};
    MessageView *previewView_{};

    View currentView_{View::Paints};
    QString searchQuery_;
    QString statusText_;
    bool statusIsError_{false};
    bool loading_{false};
    bool loaded_{false};

    QString seventvUserId_;
    QString activePaintId_;
    QString activeBadgeId_;

    std::vector<SeventvPaintItem> paints_;
    std::vector<SeventvBadgeItem> badges_;
};

}  // namespace chatterino
