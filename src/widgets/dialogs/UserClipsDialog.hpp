// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/twitch/api/TwitchGql.hpp"
#include "widgets/DraggablePopup.hpp"

#include <QPointer>
#include <QString>
#include <QVector>

#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QScrollBar;
class QShowEvent;
class QVBoxLayout;

namespace chatterino {

class Button;
class SvgButton;

class UserClipsDialog : public DraggablePopup
{
public:
    UserClipsDialog(const QString &userLogin, const QString &displayName = {},
                    QWidget *parent = nullptr);

    static void showDialog(const QString &userLogin,
                           const QString &displayName = {},
                           QWidget *parent = nullptr);

protected:
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void loadClips(bool force = false);
    void loadNextPage();
    void rebuildContent();
    void clearContent();
    void refreshStyle();
    void setStatus(const QString &text, bool error = false);
    void applySizeConstraints();
    void scheduleUnpinParentOnClose(QWidget *parent);
    void setActiveRole(const QString &role);
    [[nodiscard]] QString authTokenOrMessage();

    QString userLogin_;
    QString displayName_;

    QVBoxLayout *mainLayout_{};
    QWidget *headerWidget_{};
    QLabel *headerTitleLabel_{};
    QLineEdit *searchInput_{};
    QPushButton *broadcasterTab_{};
    QPushButton *curatorTab_{};
    Button *pinButton_{};
    SvgButton *closeButton_{};
    QScrollArea *scrollArea_{};
    QWidget *contentWidget_{};
    QVBoxLayout *contentLayout_{};
    QLabel *statusLabel_{};

    QVector<GqlClip> clips_;
    QString activeRole_ = QStringLiteral("BROADCASTER");
    QString nextCursor_;
    bool hasNextPage_ = false;
    bool clipsLoaded_ = false;
    bool clipsLoading_ = false;
    bool initialFetchDone_ = false;
    QString statusText_;
    bool statusIsError_ = false;
    QString searchQuery_;
    bool parentUnpinScheduled_ = false;

    static std::vector<QPointer<UserClipsDialog>> activeDialogs_;
};

}  // namespace chatterino
