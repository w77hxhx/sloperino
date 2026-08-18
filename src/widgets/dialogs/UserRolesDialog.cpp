// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/UserRolesDialog.hpp"

#include "Application.hpp"
#include "messages/Image.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/buttons/Button.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/dialogs/UserInfoPopup.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"
#include "widgets/Window.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino {

namespace {

constexpr int DIALOG_MIN_WIDTH = 460;
constexpr int DIALOG_MAX_WIDTH = 620;
constexpr int DIALOG_DEFAULT_WIDTH = 500;
constexpr int DIALOG_DEFAULT_HEIGHT = 620;
constexpr int CARD_PADDING = 8;
constexpr int AVATAR_SIZE = 40;

int scaledMetric(float scale, int value, int fallback = 0)
{
    if (scale <= 0.0F)
    {
        return fallback;
    }
    return std::max(1, static_cast<int>(std::round(value * scale)));
}

int scaledSeparatorHeight(float rawScale)
{
    return std::max(1, static_cast<int>(std::round(rawScale)));
}

QString formatFollowerCount(int count)
{
    if (count >= 1000000)
    {
        return QString::asprintf("%.1fM", count / 1000000.0);
    }
    if (count >= 1000)
    {
        return QString::asprintf("%.1fK", count / 1000.0);
    }
    return QString::number(count);
}

QString formatDate(const QDateTime &dt)
{
    if (!dt.isValid())
    {
        return QStringLiteral("N/A");
    }
    return dt.toLocalTime().toString("MMM d, yyyy");
}

QString getRoleBadgeText(const QString &role)
{
    if (role == "moderators")
    {
        return QStringLiteral("🛡️ Moderator");
    }
    if (role == "vips")
    {
        return QStringLiteral("💎 VIP");
    }
    if (role == "artists")
    {
        return QStringLiteral("🎨 Artist");
    }
    if (role == "founders")
    {
        return QStringLiteral("👑 Founder");
    }
    if (role == "subscribers")
    {
        return QStringLiteral("⭐ Subscriber");
    }
    return role;
}

}  // namespace

class RoleAvatarWidget final : public QWidget
{
public:
    RoleAvatarWidget(const RoleItem &item, float scale, QWidget *parent)
        : QWidget(parent)
        , item_(item)
    {
        const int size = scaledMetric(scale, AVATAR_SIZE, 40);
        this->setFixedSize(size, size);

        if (!item.avatar.isEmpty())
        {
            this->image_ = Image::fromUrl(Url{item.avatar}, 1, {size, size});
        }

        this->connections_.managedConnect(
            getApp()->getWindows()->layoutRequested, [this](Channel *) {
                this->refreshImageIfNeeded();
            });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto rect = this->rect();
        const int radius = std::max(2, int(rect.width() / 2));

        auto placeholderColor = getApp()->getThemes()->splits.input.background;
        painter.setBrush(placeholderColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(rect);

        if (this->image_)
        {
            if (auto pixmap = this->image_->pixmapOrLoad())
            {
                this->attemptRefresh_ = false;
                painter.save();
                QPainterPath path;
                path.addEllipse(rect);
                painter.setClipPath(path);
                painter.drawPixmap(rect, *pixmap, pixmap->rect());
                painter.restore();
            }
            else
            {
                this->attemptRefresh_ = true;
            }
        }
    }

private:
    void refreshImageIfNeeded()
    {
        if (this->attemptRefresh_)
        {
            this->update();
        }
    }

    RoleItem item_;
    ImagePtr image_{};
    bool attemptRefresh_{false};
    pajlada::Signals::SignalHolder connections_;
};

class RoleCardWidget final : public QFrame
{
public:
    RoleCardWidget(const RoleItem &item, const QString &role, float scale,
                   QWidget *parent)
        : QFrame(parent)
        , item_(item)
        , role_(role)
    {
        this->setCursor(Qt::PointingHandCursor);
        this->setObjectName("RoleCard");
        this->setFrameShape(QFrame::StyledPanel);

        auto *mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(CARD_PADDING, CARD_PADDING, CARD_PADDING,
                                       CARD_PADDING);
        mainLayout->setSpacing(10);

        // Avatar on the left
        auto *avatar = new RoleAvatarWidget(item, scale, this);
        mainLayout->addWidget(avatar, 0, Qt::AlignVCenter);

        // Right details layout
        auto *detailsLayout = new QVBoxLayout();
        detailsLayout->setContentsMargins(0, 0, 0, 0);
        detailsLayout->setSpacing(3);

        // Title row: Name + @login
        auto *nameLayout = new QHBoxLayout();
        nameLayout->setSpacing(6);
        nameLayout->setContentsMargins(0, 0, 0, 0);

        auto *nameLabel = new QLabel(
            item.displayName.isEmpty() ? item.login : item.displayName, this);
        nameLabel->setObjectName("RoleCardName");
        if (!item.chatColor.isEmpty())
        {
            nameLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(item.chatColor));
        }
        nameLayout->addWidget(nameLabel);

        if (!item.displayName.isEmpty() &&
            item.displayName.compare(item.login, Qt::CaseInsensitive) != 0)
        {
            auto *loginLabel =
                new QLabel(QStringLiteral("@%1").arg(item.login), this);
            loginLabel->setObjectName("RoleCardMuted");
            nameLayout->addWidget(loginLabel);
        }

        nameLayout->addStretch(1);
        detailsLayout->addLayout(nameLayout);

        // Role Tag + Partner / Affiliate badges
        auto *tagLayout = new QHBoxLayout();
        tagLayout->setSpacing(6);
        tagLayout->setContentsMargins(0, 0, 0, 0);

        auto *roleLabel = new QLabel(getRoleBadgeText(role), this);
        roleLabel->setObjectName("RoleCardBadge");
        tagLayout->addWidget(roleLabel);

        if (item.isPartner)
        {
            auto *partnerLabel = new QLabel(QStringLiteral("🟣 Partner"), this);
            partnerLabel->setObjectName("RoleCardPartner");
            tagLayout->addWidget(partnerLabel);
        }
        else if (item.isAffiliate)
        {
            auto *affiliateLabel =
                new QLabel(QStringLiteral("⚪ Affiliate"), this);
            affiliateLabel->setObjectName("RoleCardAffiliate");
            tagLayout->addWidget(affiliateLabel);
        }

        tagLayout->addStretch(1);
        detailsLayout->addLayout(tagLayout);

        // Meta row: Followers + Granted date
        auto *metaLayout = new QHBoxLayout();
        metaLayout->setSpacing(10);
        metaLayout->setContentsMargins(0, 0, 0, 0);

        if (item.followers > 0)
        {
            auto *followersLabel =
                new QLabel(QStringLiteral("👥 %1").arg(
                               formatFollowerCount(item.followers)),
                           this);
            followersLabel->setObjectName("RoleCardMuted");
            metaLayout->addWidget(followersLabel);
        }

        if (item.grantedAt.isValid())
        {
            auto *dateLabel = new QLabel(QStringLiteral("📅 Granted: %1")
                                             .arg(formatDate(item.grantedAt)),
                                         this);
            dateLabel->setObjectName("RoleCardMuted");
            metaLayout->addWidget(dateLabel);
        }

        metaLayout->addStretch(1);
        detailsLayout->addLayout(metaLayout);

        mainLayout->addLayout(detailsLayout, 1);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        if (event->button() == Qt::LeftButton)
        {
            this->openUsercard();
        }
        else if (event->button() == Qt::RightButton)
        {
            this->showContextMenu(event->globalPosition().toPoint());
        }
    }

private:
    void openUsercard()
    {
        Split *split = nullptr;
        if (auto *window = getApp()->getWindows()->getLastSelectedWindow())
        {
            if (auto *page = dynamic_cast<SplitContainer *>(
                    window->getNotebook().getSelectedPage()))
            {
                split = page->getSelectedSplit();
            }
        }
        if (split != nullptr)
        {
            auto *popup =
                new UserInfoPopup(getSettings()->autoCloseUserPopup, split);
            popup->setData(this->item_.login, split->getChannel());
            popup->show();
        }
        else
        {
            QDesktopServices::openUrl(QUrl(
                QStringLiteral("https://twitch.tv/%1").arg(this->item_.login)));
        }
    }

    void showContextMenu(const QPoint &pos)
    {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        menu->addAction(QStringLiteral("Open Usercard"), [this] {
            this->openUsercard();
        });

        menu->addAction(QStringLiteral("Open on Twitch"), [this] {
            QDesktopServices::openUrl(QUrl(
                QStringLiteral("https://twitch.tv/%1").arg(this->item_.login)));
        });

        menu->addAction(QStringLiteral("Open on roles.tv"), [this] {
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://roles.tv/c/%1")
                         .arg(this->item_.login)));
        });

        menu->addSeparator();

        menu->addAction(QStringLiteral("Copy Username"), [this] {
            QApplication::clipboard()->setText(this->item_.login);
        });

        menu->addAction(QStringLiteral("Copy Channel Link"), [this] {
            QApplication::clipboard()->setText(
                QStringLiteral("https://twitch.tv/%1").arg(this->item_.login));
        });

        menu->popup(pos);
    }

    RoleItem item_;
    QString role_;
};

std::vector<QPointer<UserRolesDialog>> UserRolesDialog::activeDialogs_;

UserRolesDialog::UserRolesDialog(const QString &targetLogin,
                                 const QString &displayName,
                                 const QString &channelName, QWidget *parent)
    : DraggablePopup(false, parent)
    , targetLogin_(targetLogin.trimmed().toLower())
    , displayName_(displayName.trimmed().isEmpty() ? targetLogin
                                                   : displayName.trimmed())
    , channelName_(channelName.trimmed().toLower())
{
    this->setObjectName("UserRolesDialog");
    this->setWindowTitle(QStringLiteral("Roles — %1").arg(this->displayName_));
    this->setWindowFlag(Qt::Window, true);

    auto *container = new QWidget(this);
    container->setObjectName("UserRolesDialogRoot");
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    this->mainLayout_ = new QVBoxLayout();
    this->mainLayout_->setContentsMargins(12, 10, 12, 12);
    this->mainLayout_->setSpacing(10);

    // Header layout
    this->headerWidget_ = new QWidget(container);
    this->headerWidget_->setObjectName("UserRolesHeader");
    auto *headerLayout = new QHBoxLayout(this->headerWidget_);
    headerLayout->setContentsMargins(12, 10, 12, 6);
    headerLayout->setSpacing(8);

    this->headerTitleLabel_ =
        new QLabel(QStringLiteral("Roles — %1").arg(this->displayName_),
                   this->headerWidget_);
    this->headerTitleLabel_->setObjectName("UserRolesTitle");
    headerLayout->addWidget(this->headerTitleLabel_);
    headerLayout->addStretch(1);

    this->pinButton_ = this->createPinButton();
    headerLayout->addWidget(this->pinButton_);

    this->closeButton_ = new SvgButton(
        {.dark = ":/buttons/cancel.svg", .light = ":/buttons/cancelDark.svg"},
        this, QSize{3, 3});
    this->closeButton_->setScaleIndependentSize(18, 18);
    this->closeButton_->setToolTip(QStringLiteral("Close"));
    this->closeButton_->setCursor(Qt::PointingHandCursor);
    this->closeButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(this->closeButton_, &Button::leftClicked, this,
                     &QWidget::close);
    headerLayout->addWidget(this->closeButton_);

    containerLayout->addWidget(this->headerWidget_);

    auto *separator = new QFrame(container);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setObjectName("UserRolesDialogSeparator");
    containerLayout->addWidget(separator);

    // Mode tabs row: Channel Mode vs User Mode
    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    modeRow->setContentsMargins(0, 0, 0, 0);

    this->channelModeTab_ = new QPushButton("📺 Channel Roles", container);
    this->channelModeTab_->setObjectName("RoleTabActive");
    this->channelModeTab_->setCursor(Qt::PointingHandCursor);
    this->channelModeTab_->setCheckable(true);
    this->channelModeTab_->setChecked(true);
    modeRow->addWidget(this->channelModeTab_);

    this->userModeTab_ = new QPushButton("👤 User Roles", container);
    this->userModeTab_->setObjectName("RoleTabInactive");
    this->userModeTab_->setCursor(Qt::PointingHandCursor);
    this->userModeTab_->setCheckable(true);
    this->userModeTab_->setChecked(false);
    modeRow->addWidget(this->userModeTab_);

    QObject::connect(this->channelModeTab_, &QPushButton::clicked, [this] {
        this->setMode("channel");
    });
    QObject::connect(this->userModeTab_, &QPushButton::clicked, [this] {
        this->setMode("user");
    });

    modeRow->addStretch(1);
    this->mainLayout_->addLayout(modeRow);

    // Role filter pills row: Moderators, VIPs, Artists, Founders, Subscribers
    auto *roleRow = new QHBoxLayout();
    roleRow->setSpacing(6);
    roleRow->setContentsMargins(0, 0, 0, 0);

    this->moderatorsTab_ = new QPushButton("🛡️ Mods", container);
    this->moderatorsTab_->setObjectName("RoleFilterActive");
    this->moderatorsTab_->setCursor(Qt::PointingHandCursor);
    roleRow->addWidget(this->moderatorsTab_);

    this->vipsTab_ = new QPushButton("💎 VIPs", container);
    this->vipsTab_->setObjectName("RoleFilterInactive");
    this->vipsTab_->setCursor(Qt::PointingHandCursor);
    roleRow->addWidget(this->vipsTab_);

    this->artistsTab_ = new QPushButton("🎨 Artists", container);
    this->artistsTab_->setObjectName("RoleFilterInactive");
    this->artistsTab_->setCursor(Qt::PointingHandCursor);
    roleRow->addWidget(this->artistsTab_);

    this->foundersTab_ = new QPushButton("👑 Founders", container);
    this->foundersTab_->setObjectName("RoleFilterInactive");
    this->foundersTab_->setCursor(Qt::PointingHandCursor);
    roleRow->addWidget(this->foundersTab_);

    this->subscribersTab_ = new QPushButton("⭐ Subs", container);
    this->subscribersTab_->setObjectName("RoleFilterInactive");
    this->subscribersTab_->setCursor(Qt::PointingHandCursor);
    roleRow->addWidget(this->subscribersTab_);

    QObject::connect(this->moderatorsTab_, &QPushButton::clicked, [this] {
        this->setRole("moderators");
    });
    QObject::connect(this->vipsTab_, &QPushButton::clicked, [this] {
        this->setRole("vips");
    });
    QObject::connect(this->artistsTab_, &QPushButton::clicked, [this] {
        this->setRole("artists");
    });
    QObject::connect(this->foundersTab_, &QPushButton::clicked, [this] {
        this->setRole("founders");
    });
    QObject::connect(this->subscribersTab_, &QPushButton::clicked, [this] {
        this->setRole("subscribers");
    });

    roleRow->addStretch(1);
    this->mainLayout_->addLayout(roleRow);

    // Search bar
    this->searchInput_ = new QLineEdit(container);
    this->searchInput_->setObjectName("UserRolesSearch");
    this->searchInput_->setPlaceholderText(
        QStringLiteral("Search roles by username or display name..."));
    this->searchInput_->setClearButtonEnabled(true);
    QObject::connect(this->searchInput_, &QLineEdit::textChanged,
                     [this](const QString &text) {
                         this->searchQuery_ = text.trimmed();
                         this->rebuildContent();
                     });
    this->mainLayout_->addWidget(this->searchInput_);

    // Scroll Area & Content
    this->scrollArea_ = new QScrollArea(container);
    this->scrollArea_->setObjectName("UserRolesScrollArea");
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setFrameShape(QFrame::NoFrame);
    this->scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    this->contentWidget_ = new QWidget(this->scrollArea_);
    this->contentWidget_->setObjectName("UserRolesDialogContent");
    this->contentLayout_ = new QVBoxLayout(this->contentWidget_);
    this->contentLayout_->setContentsMargins(0, 0, 0, 0);
    this->contentLayout_->setSpacing(6);

    this->statusLabel_ = new QLabel(this->contentWidget_);
    this->statusLabel_->setObjectName("UserRolesStatus");
    this->statusLabel_->setAlignment(Qt::AlignCenter);
    this->statusLabel_->setWordWrap(true);
    this->contentLayout_->addWidget(this->statusLabel_);
    this->contentLayout_->addStretch(1);

    this->scrollArea_->setWidget(this->contentWidget_);
    this->mainLayout_->addWidget(this->scrollArea_, 1);

    containerLayout->addLayout(this->mainLayout_, 1);
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(container);

    // Infinite scroll connection
    QObject::connect(
        this->scrollArea_->verticalScrollBar(), &QScrollBar::valueChanged,
        [this](int value) {
            auto *bar = this->scrollArea_->verticalScrollBar();
            if (bar && bar->maximum() > 0 && value >= bar->maximum() - 80)
            {
                this->loadNextPage();
            }
        });

    this->applySizeConstraints();
    this->refreshStyle();
    this->loadSummary();
    this->loadRoles(false);
}

void UserRolesDialog::showDialog(const QString &targetLogin,
                                 const QString &displayName,
                                 const QString &channelName, QWidget *parent)
{
    const auto cleaned = targetLogin.trimmed().toLower();
    if (cleaned.isEmpty())
    {
        return;
    }

    UserRolesDialog *dialog = nullptr;
    for (auto it = activeDialogs_.begin(); it != activeDialogs_.end();)
    {
        if (!*it)
        {
            it = activeDialogs_.erase(it);
            continue;
        }

        if ((*it)->targetLogin_ == cleaned)
        {
            dialog = it->data();
            break;
        }
        ++it;
    }

    if (dialog == nullptr)
    {
        dialog =
            new UserRolesDialog(targetLogin, displayName, channelName, parent);
        activeDialogs_.emplace_back(dialog);
    }

    if (parent != nullptr)
    {
        dialog->scheduleUnpinParentOnClose(parent);
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void UserRolesDialog::scheduleUnpinParentOnClose(QWidget *parent)
{
    if (this->parentUnpinScheduled_ || parent == nullptr)
    {
        return;
    }

    this->parentUnpinScheduled_ = true;
    QPointer<QWidget> parentPtr(parent);
    QObject::connect(this, &QObject::destroyed, parent, [parentPtr] {
        if (parentPtr)
        {
            DraggablePopup::unpinParentIfNeeded(parentPtr);
        }
    });
}

void UserRolesDialog::themeChangedEvent()
{
    this->refreshStyle();
}

void UserRolesDialog::scaleChangedEvent(float scale)
{
    (void)scale;
    this->applySizeConstraints();
    this->refreshStyle();
}

void UserRolesDialog::resizeEvent(QResizeEvent *event)
{
    DraggablePopup::resizeEvent(event);
}

void UserRolesDialog::showEvent(QShowEvent *event)
{
    DraggablePopup::showEvent(event);
    if (this->items_.empty() && !this->itemsLoading_)
    {
        this->loadRoles(false);
    }
}

void UserRolesDialog::setMode(const QString &mode)
{
    if (this->activeMode_ == mode)
    {
        return;
    }

    this->activeMode_ = mode;
    this->items_.clear();
    this->nextCursor_.clear();
    this->hasNextPage_ = false;
    this->itemsLoaded_ = false;
    this->itemsLoading_ = false;

    const bool isChannel = (mode == "channel");
    this->channelModeTab_->setChecked(isChannel);
    this->userModeTab_->setChecked(!isChannel);
    this->channelModeTab_->setObjectName(isChannel ? "RoleTabActive"
                                                   : "RoleTabInactive");
    this->userModeTab_->setObjectName(!isChannel ? "RoleTabActive"
                                                 : "RoleTabInactive");

    this->refreshStyle();
    this->loadSummary();
    this->loadRoles(true);
}

void UserRolesDialog::setRole(const QString &role)
{
    if (this->activeRole_ == role)
    {
        return;
    }

    this->activeRole_ = role;
    this->items_.clear();
    this->nextCursor_.clear();
    this->hasNextPage_ = false;
    this->itemsLoaded_ = false;
    this->itemsLoading_ = false;

    this->moderatorsTab_->setObjectName(
        role == "moderators" ? "RoleFilterActive" : "RoleFilterInactive");
    this->vipsTab_->setObjectName(role == "vips" ? "RoleFilterActive"
                                                 : "RoleFilterInactive");
    this->artistsTab_->setObjectName(role == "artists" ? "RoleFilterActive"
                                                       : "RoleFilterInactive");
    this->foundersTab_->setObjectName(
        role == "founders" ? "RoleFilterActive" : "RoleFilterInactive");
    this->subscribersTab_->setObjectName(
        role == "subscribers" ? "RoleFilterActive" : "RoleFilterInactive");

    this->refreshStyle();
    this->loadRoles(true);
}

void UserRolesDialog::updateTabCounters()
{
    if (!this->summaryLoaded_)
    {
        return;
    }

    this->moderatorsTab_->setText(
        QStringLiteral("🛡️ Mods (%1)").arg(this->summary_.moderators));
    this->vipsTab_->setText(
        QStringLiteral("💎 VIPs (%1)").arg(this->summary_.vips));
    this->artistsTab_->setText(
        QStringLiteral("🎨 Artists (%1)").arg(this->summary_.artists));
    this->foundersTab_->setText(
        QStringLiteral("👑 Founders (%1)").arg(this->summary_.founders));
    this->subscribersTab_->setText(
        QStringLiteral("⭐ Subs (%1)").arg(this->summary_.subscribers));
}

void UserRolesDialog::loadSummary()
{
    QPointer<UserRolesDialog> self = this;
    const auto target = this->targetLogin_;
    const bool isChannel = (this->activeMode_ == "channel");

    auto success = [self](const RoleSummary &summary) {
        if (!self)
        {
            return;
        }
        self->summary_ = summary;
        self->summaryLoaded_ = true;
        self->updateTabCounters();
    };

    auto failure = [self](const QString &error) {
        (void)error;
    };

    if (isChannel)
    {
        RolesApi::getChannelSummary(target, success, failure);
    }
    else
    {
        RolesApi::getUserSummary(target, success, failure);
    }
}

void UserRolesDialog::loadRoles(bool force)
{
    if (this->itemsLoading_ || (this->itemsLoaded_ && !force))
    {
        return;
    }

    this->itemsLoading_ = true;
    this->setStatus(QStringLiteral("Loading roles from roles.tv..."));

    QPointer<UserRolesDialog> self = this;
    const auto mode = this->activeMode_;
    const auto role = this->activeRole_;
    const auto login = this->targetLogin_;

    RolesApi::getRoleList(
        mode, role, login, QString{},
        [self, mode, role, login](const RoleListResult &result) {
            if (!self || self->activeMode_ != mode ||
                self->activeRole_ != role || self->targetLogin_ != login)
            {
                return;
            }

            self->itemsLoading_ = false;
            self->itemsLoaded_ = true;
            self->items_ = result.items;
            self->nextCursor_ = result.cursor;
            self->hasNextPage_ =
                !result.cursor.isEmpty() && result.page < result.pages;

            self->rebuildContent();
        },
        [self, mode, role, login](const QString &error) {
            if (!self || self->activeMode_ != mode ||
                self->activeRole_ != role || self->targetLogin_ != login)
            {
                return;
            }

            self->itemsLoading_ = false;
            self->setStatus(
                QStringLiteral("Failed to load roles: %1").arg(error), true);
        });
}

void UserRolesDialog::loadNextPage()
{
    if (this->itemsLoading_ || !this->hasNextPage_ ||
        this->nextCursor_.isEmpty())
    {
        return;
    }

    this->itemsLoading_ = true;

    QPointer<UserRolesDialog> self = this;
    const auto mode = this->activeMode_;
    const auto role = this->activeRole_;
    const auto login = this->targetLogin_;
    const auto cursor = this->nextCursor_;

    RolesApi::getRoleList(
        mode, role, login, cursor,
        [self, mode, role, login](const RoleListResult &result) {
            if (!self || self->activeMode_ != mode ||
                self->activeRole_ != role || self->targetLogin_ != login)
            {
                return;
            }

            self->itemsLoading_ = false;
            self->nextCursor_ = result.cursor;
            self->hasNextPage_ =
                !result.cursor.isEmpty() && result.page < result.pages;

            for (const auto &item : result.items)
            {
                self->items_.push_back(item);
            }

            self->rebuildContent();
        },
        [self, mode, role, login](const QString &error) {
            if (!self || self->activeMode_ != mode ||
                self->activeRole_ != role || self->targetLogin_ != login)
            {
                return;
            }

            self->itemsLoading_ = false;
            (void)error;
        });
}

void UserRolesDialog::rebuildContent()
{
    this->clearContent();

    std::vector<RoleItem> filtered;
    filtered.reserve(this->items_.size());

    const auto query = this->searchQuery_.trimmed().toLower();
    for (const auto &item : this->items_)
    {
        if (query.isEmpty())
        {
            filtered.push_back(item);
            continue;
        }

        if (item.login.toLower().contains(query) ||
            item.displayName.toLower().contains(query))
        {
            filtered.push_back(item);
        }
    }

    if (filtered.empty())
    {
        if (this->items_.empty())
        {
            this->setStatus(
                QStringLiteral("No %1 found for this %2 on roles.tv.")
                    .arg(this->activeRole_, this->activeMode_));
        }
        else
        {
            this->setStatus(
                QStringLiteral("No matching roles found for \"%1\".")
                    .arg(this->searchQuery_));
        }
        return;
    }

    this->statusLabel_->hide();
    const float scale = this->scale();

    for (const auto &item : filtered)
    {
        auto *card = new RoleCardWidget(item, this->activeRole_, scale,
                                        this->contentWidget_);
        this->contentLayout_->addWidget(card);
    }

    this->contentLayout_->addStretch(1);
}

void UserRolesDialog::clearContent()
{
    while (this->contentLayout_->count() > 0)
    {
        auto *item = this->contentLayout_->takeAt(0);
        if (item->widget() != nullptr && item->widget() != this->statusLabel_)
        {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

void UserRolesDialog::setStatus(const QString &text, bool error)
{
    this->clearContent();
    this->statusText_ = text;
    this->statusIsError_ = error;
    this->statusLabel_->setText(text);
    this->statusLabel_->show();
    this->contentLayout_->addWidget(this->statusLabel_);
    this->contentLayout_->addStretch(1);
}

void UserRolesDialog::applySizeConstraints()
{
    const float scale = this->scale();
    const int minW = scaledMetric(scale, DIALOG_MIN_WIDTH, DIALOG_MIN_WIDTH);
    const int maxW = scaledMetric(scale, DIALOG_MAX_WIDTH, DIALOG_MAX_WIDTH);
    const int defaultW =
        scaledMetric(scale, DIALOG_DEFAULT_WIDTH, DIALOG_DEFAULT_WIDTH);
    const int defaultH =
        scaledMetric(scale, DIALOG_DEFAULT_HEIGHT, DIALOG_DEFAULT_HEIGHT);

    this->setMinimumSize(minW, 350);
    this->setMaximumWidth(maxW);
    this->resize(defaultW, defaultH);
}

void UserRolesDialog::refreshStyle()
{
    const float rawScale = this->scale();
    const int radius = std::max(4, static_cast<int>(std::round(4 * rawScale)));
    const int pillRadius =
        std::max(4, static_cast<int>(std::round(14 * rawScale)));
    const int inputPaddingX =
        std::max(8, static_cast<int>(std::round(8 * rawScale)));
    const int scrollbarWidth =
        std::max(8, static_cast<int>(std::round(8 * rawScale)));
    const int scrollbarRadius =
        std::max(4, static_cast<int>(std::round(4 * rawScale)));
    const int scrollbarMinHeight =
        std::max(20, static_cast<int>(std::round(20 * rawScale)));
    const int inputMinHeight =
        std::max(30, static_cast<int>(std::round(30 * rawScale)));
    const int cardRadius =
        std::max(6, static_cast<int>(std::round(6 * rawScale)));

    if (auto *sep = this->findChild<QWidget *>(
            QStringLiteral("UserRolesDialogSeparator")))
    {
        sep->setFixedHeight(scaledSeparatorHeight(rawScale));
    }

    const auto *theme = this->theme;
    auto textColor = theme->window.text;
    auto mutedColor = textColor;
    mutedColor.setAlpha(160);
    const auto bg = theme->window.background.name();
    const auto text = textColor.name(QColor::HexArgb);
    const auto border = theme->splits.header.border.name();
    const auto muted = mutedColor.name(QColor::HexArgb);
    const auto inputBg = theme->splits.input.background.name();
    const auto focusedBorder = theme->splits.header.focusedBorder.name();
    const auto hoverBg =
        theme->isLightTheme()
            ? theme->splits.input.background.darker(104).name()
            : theme->splits.input.background.lighter(108).name();
    const auto cardBg =
        theme->isLightTheme()
            ? theme->splits.header.background.name()
            : theme->splits.header.background.lighter(110).name();
    const auto cardHoverBg =
        theme->isLightTheme()
            ? theme->splits.header.background.darker(105).name()
            : theme->splits.header.background.lighter(125).name();
    const auto accentColor = QStringLiteral("#9147ff");

    this->closeButton_->setColor(textColor);

    this->setStyleSheet(
        QStringLiteral(R"(
        QWidget#UserRolesDialogRoot {
            background: %1;
            color: %2;
        }
        QWidget#UserRolesHeader {
            background: transparent;
        }
        QFrame#UserRolesDialogSeparator,
        QWidget#UserRolesDialogSeparator {
            background: %3;
        }
        QScrollArea#UserRolesScrollArea {
            background: transparent;
            border: 0;
        }
        QWidget#UserRolesDialogContent {
            background: transparent;
            color: %2;
        }
        QLabel#UserRolesTitle {
            color: %2;
            font-weight: 700;
        }
        QLabel#UserRolesStatus {
            color: %4;
            padding: 16px;
        }
        QScrollBar:vertical {
            width: %8px;
            background: transparent;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: %3;
            min-height: %10px;
            border-radius: %9px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical,
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical {
            background: transparent;
            height: 0;
        }
        QLineEdit#UserRolesSearch {
            background: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: %6px;
            padding: 0 %7px;
            min-height: %11px;
            selection-background-color: %12;
        }
        QLineEdit#UserRolesSearch:focus {
            border-color: %13;
        }
        QFrame#RoleCard {
            background: %14;
            border: 1px solid %3;
            border-radius: %15px;
        }
        QFrame#RoleCard:hover {
            background: %16;
            border-color: %13;
        }
        QLabel#RoleCardName {
            color: %2;
            font-weight: 700;
            font-size: 13px;
        }
        QLabel#RoleCardMuted {
            color: %4;
            font-size: 11px;
        }
        QLabel#RoleCardBadge {
            color: %17;
            font-weight: 600;
            font-size: 11px;
        }
        QLabel#RoleCardPartner {
            color: #bf94ff;
            font-weight: 600;
            font-size: 11px;
        }
        QLabel#RoleCardAffiliate {
            color: #00e6cb;
            font-weight: 600;
            font-size: 11px;
        }
        QPushButton#RoleTabActive {
            background: %17;
            color: #ffffff;
            border: none;
            border-radius: %6px;
            padding: 5px 14px;
            font-weight: 600;
        }
        QPushButton#RoleTabInactive {
            background: %14;
            color: %2;
            border: 1px solid %3;
            border-radius: %6px;
            padding: 5px 14px;
        }
        QPushButton#RoleTabInactive:hover {
            background: %16;
        }
        QPushButton#RoleFilterActive {
            background: %17;
            color: #ffffff;
            border: none;
            border-radius: %18px;
            padding: 4px 10px;
            font-weight: 600;
            font-size: 11px;
        }
        QPushButton#RoleFilterInactive {
            background: %14;
            color: %4;
            border: 1px solid %3;
            border-radius: %18px;
            padding: 4px 10px;
            font-size: 11px;
        }
        QPushButton#RoleFilterInactive:hover {
            background: %16;
            color: %2;
        }
    )")
            .arg(bg, text, border, muted, inputBg, QString::number(radius),
                 QString::number(inputPaddingX),
                 QString::number(scrollbarWidth),
                 QString::number(scrollbarRadius),
                 QString::number(scrollbarMinHeight),
                 QString::number(inputMinHeight), hoverBg, focusedBorder,
                 cardBg, QString::number(cardRadius), cardHoverBg, accentColor,
                 QString::number(pillRadius)));
}

}  // namespace chatterino
