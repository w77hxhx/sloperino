// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/UserClipsDialog.hpp"

#include "Application.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/Clipboard.hpp"
#include "util/IncognitoBrowser.hpp"
#include "widgets/buttons/Button.hpp"
#include "widgets/buttons/LabelButton.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/helper/Line.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace chatterino {

namespace {

constexpr QSize DEFAULT_DIALOG_SIZE(480, 580);
constexpr int HEADER_SEPARATOR_HEIGHT = 8;
constexpr int CLIP_CARD_SPACING = 8;
constexpr int CLIP_CARD_PADDING = 8;
constexpr int THUMBNAIL_WIDTH = 96;
constexpr int THUMBNAIL_HEIGHT = 54;
constexpr int CLIPS_PER_PAGE = 100;

int scaledSeparatorHeight(float scale)
{
    return std::max(1, int(HEADER_SEPARATOR_HEIGHT * scale));
}

int scaledMetric(float scale, int base, int minimum)
{
    return std::max(minimum, int(std::round(base * scale)));
}

int contentHorizontalMargin(float scale)
{
    return scaledMetric(scale, 12, 6);
}

bool clipMatchesSearch(const GqlClip &clip, const QString &needle)
{
    if (needle.isEmpty())
    {
        return true;
    }

    return clip.title.contains(needle, Qt::CaseInsensitive) ||
           clip.gameName.contains(needle, Qt::CaseInsensitive) ||
           clip.curatorDisplayName.contains(needle, Qt::CaseInsensitive) ||
           clip.curatorLogin.contains(needle, Qt::CaseInsensitive) ||
           clip.broadcasterDisplayName.contains(needle, Qt::CaseInsensitive) ||
           clip.broadcasterLogin.contains(needle, Qt::CaseInsensitive);
}

QString formatDuration(double seconds)
{
    int totalSecs = static_cast<int>(seconds);
    int mins = totalSecs / 60;
    int secs = totalSecs % 60;
    return QStringLiteral("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

QString formatViewCount(int views)
{
    if (views >= 1000000)
    {
        return QStringLiteral("%1M").arg(views / 1000000.0, 0, 'f', 1);
    }
    if (views >= 1000)
    {
        return QStringLiteral("%1K").arg(views / 1000.0, 0, 'f', 1);
    }
    return QString::number(views);
}

QString formatDate(const QString &isoDate)
{
    auto dt = QDateTime::fromString(isoDate, Qt::ISODate);
    if (!dt.isValid())
    {
        return isoDate;
    }
    return dt.toLocalTime().toString("MMM d, yyyy");
}

class ClipThumbnailWidget final : public QWidget
{
public:
    ClipThumbnailWidget(const GqlClip &clip, float scale, QWidget *parent)
        : QWidget(parent)
        , clip_(clip)
        , scale_(scale)
    {
        const int w = scaledMetric(scale, THUMBNAIL_WIDTH, 48);
        const int h = scaledMetric(scale, THUMBNAIL_HEIGHT, 27);
        this->setFixedSize(w, h);

        if (!clip.thumbnailURL.isEmpty())
        {
            this->image_ = Image::fromUrl(Url{clip.thumbnailURL}, 1, {w, h});
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
        const int radius = std::max(2, int(3 * this->scale_));

        // Background placeholder
        auto placeholderColor = getApp()->getThemes()->splits.input.background;
        painter.setBrush(placeholderColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect, radius, radius);

        if (this->image_)
        {
            if (auto pixmap = this->image_->pixmapOrLoad())
            {
                this->attemptRefresh_ = false;
                painter.save();
                QPainterPath path;
                path.addRoundedRect(rect, radius, radius);
                painter.setClipPath(path);
                painter.drawPixmap(rect, *pixmap, pixmap->rect());
                painter.restore();
            }
            else
            {
                this->attemptRefresh_ = true;
            }
        }

        // Draw duration overlay badge at bottom-right
        if (this->clip_.durationSeconds > 0.0)
        {
            const auto durationText =
                formatDuration(this->clip_.durationSeconds);
            auto font = getApp()->getFonts()->getFont(FontStyle::UiMedium,
                                                      this->scale_ * 0.75F);
            painter.setFont(font);
            QFontMetrics fm(font);
            const int textW = fm.horizontalAdvance(durationText);
            const int textH = fm.height();
            const int badgePadX = 3;
            const int badgePadY = 1;
            const int badgeW = textW + badgePadX * 2;
            const int badgeH = textH + badgePadY * 2;
            const QRect badgeRect(rect.width() - badgeW - 3,
                                  rect.height() - badgeH - 3, badgeW, badgeH);

            painter.setBrush(QColor(0, 0, 0, 190));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(badgeRect, 2, 2);

            painter.setPen(QColor(255, 255, 255));
            painter.drawText(badgeRect, Qt::AlignCenter, durationText);
        }
    }

private:
    void refreshImageIfNeeded()
    {
        if (!this->attemptRefresh_)
        {
            return;
        }

        if (this->image_ && this->image_->pixmapOrLoad())
        {
            this->attemptRefresh_ = false;
            this->update();
        }
    }

    GqlClip clip_;
    float scale_ = 1.0F;
    ImagePtr image_;
    bool attemptRefresh_ = false;
    pajlada::Signals::SignalHolder connections_;
};

class ClipCardWidget final : public QFrame
{
public:
    ClipCardWidget(const GqlClip &clip, float scale, QWidget *parent)
        : QFrame(parent)
        , clip_(clip)
    {
        this->setCursor(Qt::PointingHandCursor);
        this->setObjectName("ClipCard");
        this->setFrameShape(QFrame::StyledPanel);

        auto *mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(CLIP_CARD_PADDING, CLIP_CARD_PADDING,
                                       CLIP_CARD_PADDING, CLIP_CARD_PADDING);
        mainLayout->setSpacing(8);

        // Thumbnail on the left
        auto *thumb = new ClipThumbnailWidget(clip, scale, this);
        mainLayout->addWidget(thumb, 0, Qt::AlignVCenter);

        // Right details layout
        auto *detailsLayout = new QVBoxLayout();
        detailsLayout->setContentsMargins(0, 0, 0, 0);
        detailsLayout->setSpacing(3);

        // Title row
        auto *titleLabel = new QLabel(clip.title, this);
        titleLabel->setObjectName("ClipCardTitle");
        titleLabel->setWordWrap(true);
        titleLabel->setToolTip(clip.title);
        detailsLayout->addWidget(titleLabel);

        // Info row: Game tag & Curator / Broadcaster
        auto *infoLayout = new QHBoxLayout();
        infoLayout->setSpacing(6);
        infoLayout->setContentsMargins(0, 0, 0, 0);

        if (!clip.gameName.isEmpty())
        {
            auto *gameLabel = new QLabel(clip.gameName, this);
            gameLabel->setObjectName("ClipCardGame");
            gameLabel->setToolTip(clip.gameName);
            infoLayout->addWidget(gameLabel);
        }

        if (!clip.curatorDisplayName.isEmpty())
        {
            auto *curatorLabel = new QLabel(
                QStringLiteral("Clipped by %1").arg(clip.curatorDisplayName),
                this);
            curatorLabel->setObjectName("ClipCardInfo");
            curatorLabel->setToolTip(
                QStringLiteral("Clipped by %1 (%2)")
                    .arg(clip.curatorDisplayName, clip.curatorLogin));
            infoLayout->addWidget(curatorLabel);
        }
        else if (!clip.broadcasterDisplayName.isEmpty())
        {
            auto *broadcasterLabel =
                new QLabel(clip.broadcasterDisplayName, this);
            broadcasterLabel->setObjectName("ClipCardInfo");
            broadcasterLabel->setToolTip(
                QStringLiteral("Streamer: %1 (%2)")
                    .arg(clip.broadcasterDisplayName, clip.broadcasterLogin));
            infoLayout->addWidget(broadcasterLabel);
        }

        infoLayout->addStretch(1);
        detailsLayout->addLayout(infoLayout);

        // Meta row: Views & Date
        auto *metaLayout = new QHBoxLayout();
        metaLayout->setSpacing(8);
        metaLayout->setContentsMargins(0, 0, 0, 0);

        auto *viewsLabel = new QLabel(
            QStringLiteral("%1 views").arg(formatViewCount(clip.viewCount)),
            this);
        viewsLabel->setObjectName("ClipCardMeta");
        metaLayout->addWidget(viewsLabel);

        auto *dateLabel = new QLabel(formatDate(clip.createdAt), this);
        dateLabel->setObjectName("ClipCardMeta");
        metaLayout->addWidget(dateLabel);

        metaLayout->addStretch(1);
        detailsLayout->addLayout(metaLayout);

        mainLayout->addLayout(detailsLayout, 1);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        QFrame::mousePressEvent(event);
        if (event->button() == Qt::LeftButton && !this->clip_.url.isEmpty())
        {
            QDesktopServices::openUrl(QUrl(this->clip_.url));
        }
        else if (event->button() == Qt::RightButton)
        {
            this->showContextMenu(event->globalPosition().toPoint());
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        QFrame::enterEvent(event);
        this->setProperty("hovered", true);
        this->style()->unpolish(this);
        this->style()->polish(this);
    }

    void leaveEvent(QEvent *event) override
    {
        QFrame::leaveEvent(event);
        this->setProperty("hovered", false);
        this->style()->unpolish(this);
        this->style()->polish(this);
    }

private:
    void showContextMenu(const QPoint &pos)
    {
        auto *menu = new QMenu(this);

        if (!this->clip_.url.isEmpty())
        {
            menu->addAction("Open in Browser", this, [this] {
                QDesktopServices::openUrl(QUrl(this->clip_.url));
            });

            menu->addAction("Open in Incognito Browser", this, [this] {
                openLinkIncognito(this->clip_.url);
            });

            menu->addSeparator();

            menu->addAction("Copy Clip Link", this, [this] {
                crossPlatformCopy(this->clip_.url);
            });
        }

        if (!this->clip_.title.isEmpty())
        {
            menu->addAction("Copy Title", this, [this] {
                crossPlatformCopy(this->clip_.title);
            });
        }

        if (!this->clip_.gameName.isEmpty())
        {
            menu->addAction("Copy Game Name", this, [this] {
                crossPlatformCopy(this->clip_.gameName);
            });
        }

        if (!this->clip_.slug.isEmpty())
        {
            menu->addAction("Copy Clip ID", this, [this] {
                crossPlatformCopy(this->clip_.slug);
            });
        }

        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(pos);
    }

    GqlClip clip_;
};

}  // namespace

std::vector<QPointer<UserClipsDialog>> UserClipsDialog::activeDialogs_;

UserClipsDialog::UserClipsDialog(const QString &userLogin,
                                 const QString &displayName, QWidget *parent)
    : DraggablePopup(false, parent)
    , userLogin_(userLogin)
    , displayName_(displayName.isEmpty() ? userLogin : displayName)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setObjectName("UserClipsDialog");
    const auto title = QStringLiteral("Clips — %1").arg(this->displayName_);
    this->setWindowTitle(title);
    this->setScaleIndependentSize(DEFAULT_DIALOG_SIZE);

    auto *container = this->getLayoutContainer();
    container->setObjectName("UserClipsDialogRoot");
    container->setMouseTracking(true);
    this->mainLayout_ = new QVBoxLayout(container);
    this->mainLayout_->setSpacing(0);

    // Header
    this->headerWidget_ = new QWidget(container);
    this->headerWidget_->setObjectName("UserClipsHeader");
    auto *headerLayout = new QHBoxLayout(this->headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    this->headerTitleLabel_ = new QLabel(title, this->headerWidget_);
    this->headerTitleLabel_->setObjectName("UserClipsTitle");
    headerLayout->addWidget(this->headerTitleLabel_);
    headerLayout->addStretch(1);

    this->pinButton_ = this->createPinButton();
    headerLayout->addWidget(this->pinButton_);

    this->closeButton_ = new SvgButton(
        {.dark = ":/buttons/cancel.svg", .light = ":/buttons/cancelDark.svg"},
        this, QSize{3, 3});
    this->closeButton_->setScaleIndependentSize(18, 18);
    this->closeButton_->setToolTip("Close");
    this->closeButton_->setCursor(Qt::PointingHandCursor);
    this->closeButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(this->closeButton_, &Button::leftClicked, this,
                     &QWidget::close);
    headerLayout->addWidget(this->closeButton_);
    this->mainLayout_->addWidget(this->headerWidget_);

    auto *separator = new Line(false);
    separator->setObjectName("UserClipsDialogSeparator");
    separator->setFixedHeight(scaledSeparatorHeight(this->scale()));
    this->mainLayout_->addWidget(separator);

    // Tab row (Broadcaster / Curator)
    auto *tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(0, scaledMetric(this->scale(), 6, 3), 0, 0);
    tabRow->setSpacing(6);

    this->broadcasterTab_ = new LabelButton("Broadcaster", this, QSize{8, 4});
    this->broadcasterTab_->setToolTip("Clips created on this channel");
    tabRow->addWidget(this->broadcasterTab_);

    this->curatorTab_ = new LabelButton("Curator", this, QSize{8, 4});
    this->curatorTab_->setToolTip("Clips created by this user");
    tabRow->addWidget(this->curatorTab_);

    tabRow->addStretch(1);
    this->mainLayout_->addLayout(tabRow);

    QObject::connect(this->broadcasterTab_, &Button::leftClicked, this, [this] {
        this->setActiveRole(QStringLiteral("BROADCASTER"));
    });
    QObject::connect(this->curatorTab_, &Button::leftClicked, this, [this] {
        this->setActiveRole(QStringLiteral("CURATOR"));
    });

    // Search input
    this->searchInput_ = new QLineEdit(container);
    this->searchInput_->setObjectName("UserClipsSearch");
    this->searchInput_->setPlaceholderText(
        "Search clips by title, game, curator...");
    this->searchInput_->setClearButtonEnabled(true);
    QObject::connect(this->searchInput_, &QLineEdit::textChanged, this,
                     [this](const QString &text) {
                         this->searchQuery_ = text;
                         this->rebuildContent();
                         if (this->searchInput_ != nullptr)
                         {
                             this->searchInput_->setFocus(Qt::OtherFocusReason);
                             this->searchInput_->setCursorPosition(
                                 this->searchQuery_.size());
                         }
                     });
    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, scaledMetric(this->scale(), 6, 3), 0,
                                  scaledMetric(this->scale(), 6, 3));
    searchRow->addWidget(this->searchInput_);
    this->mainLayout_->addLayout(searchRow);

    // Scroll area
    this->scrollArea_ = new QScrollArea(container);
    this->scrollArea_->setObjectName("UserClipsScrollArea");
    this->scrollArea_->setFrameShape(QFrame::NoFrame);
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->scrollArea_->viewport()->setAutoFillBackground(false);
    this->mainLayout_->addWidget(this->scrollArea_, 1);

    // Auto-pagination on scroll
    QObject::connect(this->scrollArea_->verticalScrollBar(),
                     &QScrollBar::valueChanged, this, [this](int value) {
                         auto *bar = this->scrollArea_->verticalScrollBar();
                         if (bar == nullptr)
                         {
                             return;
                         }
                         // Load more when within 150px of the bottom
                         if (bar->maximum() - value < 150 &&
                             this->hasNextPage_ && !this->clipsLoading_)
                         {
                             this->loadNextPage();
                         }
                     });

    this->contentWidget_ = new QWidget();
    this->contentWidget_->setObjectName("UserClipsDialogContent");
    this->contentWidget_->setMinimumWidth(0);
    this->contentWidget_->setSizePolicy(QSizePolicy::Ignored,
                                        QSizePolicy::Preferred);
    this->contentLayout_ = new QVBoxLayout(this->contentWidget_);
    this->contentLayout_->setContentsMargins(
        0, scaledMetric(this->scale(), 4, 2), 0,
        scaledMetric(this->scale(), 8, 4));
    this->contentLayout_->setSpacing(CLIP_CARD_SPACING);
    this->scrollArea_->setWidget(this->contentWidget_);

    this->refreshStyle();
    this->applySizeConstraints();
    this->rebuildContent();
}

void UserClipsDialog::showDialog(const QString &userLogin,
                                 const QString &displayName, QWidget *parent)
{
    if (userLogin.isEmpty())
    {
        return;
    }

    const bool wasAutoPinned = DraggablePopup::pinParentIfNeeded(parent);

    UserClipsDialog *dialog = nullptr;

    for (auto it = activeDialogs_.begin(); it != activeDialogs_.end();)
    {
        if (it->isNull())
        {
            it = activeDialogs_.erase(it);
            continue;
        }
        if ((*it)->userLogin_.compare(userLogin, Qt::CaseInsensitive) == 0)
        {
            dialog = *it;
            dialog->raise();
            dialog->activateWindow();
            dialog->loadClips(true);
            break;
        }
        ++it;
    }

    if (dialog == nullptr)
    {
        QWidget *ownershipParent = parent;
        if (qobject_cast<DraggablePopup *>(parent) != nullptr)
        {
            ownershipParent = nullptr;
        }

        dialog = new UserClipsDialog(userLogin, displayName, ownershipParent);
        activeDialogs_.push_back(dialog);

        QPoint center = QCursor::pos();
        if (parent != nullptr && parent->window() != nullptr)
        {
            center = parent->window()->geometry().center();
        }

        dialog->show();
        const auto size = dialog->size();
        dialog->showAndMoveTo(
            center - QPoint(size.width() / 2, size.height() / 2),
            widgets::BoundsChecking::DesiredPosition);
        dialog->raise();
        dialog->activateWindow();
        dialog->loadClips(false);
    }

    if (wasAutoPinned)
    {
        dialog->scheduleUnpinParentOnClose(parent);
    }
}

void UserClipsDialog::scheduleUnpinParentOnClose(QWidget *parent)
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

void UserClipsDialog::themeChangedEvent()
{
    DraggablePopup::themeChangedEvent();
    this->refreshStyle();
}

void UserClipsDialog::scaleChangedEvent(float scale)
{
    DraggablePopup::scaleChangedEvent(scale);
    this->refreshStyle();
    this->applySizeConstraints();
    this->rebuildContent();
}

void UserClipsDialog::resizeEvent(QResizeEvent *event)
{
    DraggablePopup::resizeEvent(event);

    const int minW = this->minimumWidth();
    const int minH = this->minimumHeight();
    if (this->width() < minW || this->height() < minH)
    {
        this->resize(std::max(this->width(), minW),
                     std::max(this->height(), minH));
    }
}

void UserClipsDialog::showEvent(QShowEvent *event)
{
    DraggablePopup::showEvent(event);
    if (!this->initialFetchDone_)
    {
        this->loadClips(false);
    }
}

void UserClipsDialog::setActiveRole(const QString &role)
{
    if (this->activeRole_ == role)
    {
        return;
    }

    this->activeRole_ = role;
    this->clips_.clear();
    this->nextCursor_.clear();
    this->hasNextPage_ = false;
    this->clipsLoaded_ = false;
    this->clipsLoading_ = false;

    this->refreshStyle();
    this->loadClips(true);
}

void UserClipsDialog::loadClips(bool force)
{
    if (this->clipsLoading_ && !force)
    {
        return;
    }

    const auto token = this->authTokenOrMessage();
    if (token.isEmpty())
    {
        this->initialFetchDone_ = true;
        this->rebuildContent();
        return;
    }

    this->initialFetchDone_ = true;
    this->clipsLoading_ = true;

    this->clips_.clear();
    this->nextCursor_.clear();
    this->hasNextPage_ = false;
    this->clipsLoaded_ = false;

    this->setStatus("Loading clips...");
    this->rebuildContent();

    QPointer<UserClipsDialog> self = this;

    TwitchGql::getUserClips(
        this->userLogin_, this->activeRole_, {}, CLIPS_PER_PAGE, token,
        [self](GqlClipPage page) {
            if (!self)
            {
                return;
            }
            self->clipsLoading_ = false;
            self->clipsLoaded_ = true;
            self->clips_ = std::move(page.clips);
            self->nextCursor_ = page.nextCursor;
            self->hasNextPage_ = page.hasNextPage;
            self->setStatus({});
            self->rebuildContent();
        },
        [self](const QString &error) {
            if (!self)
            {
                return;
            }
            self->clipsLoading_ = false;
            self->setStatus(
                MoltorinoAuth::normalizeAuthError("loading clips", error),
                true);
            self->rebuildContent();
        });
}

void UserClipsDialog::loadNextPage()
{
    if (this->clipsLoading_ || !this->hasNextPage_ ||
        this->nextCursor_.isEmpty())
    {
        return;
    }

    const auto token = this->authTokenOrMessage();
    if (token.isEmpty())
    {
        return;
    }

    this->clipsLoading_ = true;

    QPointer<UserClipsDialog> self = this;

    TwitchGql::getUserClips(
        this->userLogin_, this->activeRole_, this->nextCursor_, CLIPS_PER_PAGE,
        token,
        [self](GqlClipPage page) {
            if (!self)
            {
                return;
            }
            self->clipsLoading_ = false;
            self->clips_.append(page.clips);
            self->nextCursor_ = page.nextCursor;
            self->hasNextPage_ = page.hasNextPage;
            self->rebuildContent();
        },
        [self](const QString &error) {
            if (!self)
            {
                return;
            }
            self->clipsLoading_ = false;
            self->setStatus(
                MoltorinoAuth::normalizeAuthError("loading clips", error),
                true);
        });
}

void UserClipsDialog::rebuildContent()
{
    this->clearContent();

    this->statusLabel_ = new QLabel(this->contentWidget_);
    this->statusLabel_->setObjectName("UserClipsStatus");
    this->statusLabel_->setWordWrap(true);
    this->statusLabel_->setAlignment(Qt::AlignCenter);
    this->statusLabel_->hide();
    this->contentLayout_->addWidget(this->statusLabel_);
    this->setStatus(this->statusText_, this->statusIsError_);

    if (this->clipsLoading_ && this->clips_.isEmpty())
    {
        this->setStatus("Loading clips...");
        this->contentLayout_->addStretch(1);
        return;
    }

    if (!this->clipsLoaded_ && this->statusIsError_ &&
        !this->statusText_.isEmpty())
    {
        this->contentLayout_->addStretch(1);
        return;
    }

    const auto needle = this->searchQuery_.trimmed();
    int matchCount = 0;

    for (const auto &clip : this->clips_)
    {
        if (!clipMatchesSearch(clip, needle))
        {
            continue;
        }

        auto *card =
            new ClipCardWidget(clip, this->scale(), this->contentWidget_);
        this->contentLayout_->addWidget(card);
        matchCount++;
    }

    if (matchCount == 0 && this->clipsLoaded_)
    {
        auto *emptyLabel = new QLabel(this->contentWidget_);
        emptyLabel->setObjectName("UserClipsEmpty");
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setWordWrap(true);

        if (needle.isEmpty())
        {
            emptyLabel->setText(
                QStringLiteral("No clips found for %1 (%2).")
                    .arg(this->displayName_, this->activeRole_.toLower()));
        }
        else
        {
            emptyLabel->setText(
                QStringLiteral("No clips matching \"%1\".").arg(needle));
        }
        this->contentLayout_->addWidget(emptyLabel);
    }

    if (this->clipsLoading_)
    {
        auto *loadingLabel =
            new QLabel("Loading more clips...", this->contentWidget_);
        loadingLabel->setObjectName("UserClipsStatus");
        loadingLabel->setAlignment(Qt::AlignCenter);
        this->contentLayout_->addWidget(loadingLabel);
    }

    this->contentLayout_->addStretch(1);
}

void UserClipsDialog::clearContent()
{
    if (this->contentLayout_ == nullptr)
    {
        return;
    }

    QLayoutItem *child = nullptr;
    while ((child = this->contentLayout_->takeAt(0)) != nullptr)
    {
        if (auto *widget = child->widget())
        {
            widget->deleteLater();
        }
        delete child;
    }
    this->statusLabel_ = nullptr;
}

void UserClipsDialog::setStatus(const QString &text, bool error)
{
    this->statusText_ = text;
    this->statusIsError_ = error;
    if (this->statusLabel_ == nullptr)
    {
        return;
    }

    if (text.isEmpty())
    {
        this->statusLabel_->hide();
        return;
    }

    this->statusLabel_->setText(text);
    this->statusLabel_->show();
}

void UserClipsDialog::applySizeConstraints()
{
    const int minW = std::max(320, int(DEFAULT_DIALOG_SIZE.width() * 0.7));
    const int minH = std::max(320, int(DEFAULT_DIALOG_SIZE.height() * 0.7));
    this->setMinimumSize(minW, minH);

    if (this->width() < minW || this->height() < minH)
    {
        this->resize(std::max(this->width(), minW),
                     std::max(this->height(), minH));
    }
}

void UserClipsDialog::refreshStyle()
{
    auto *fonts = getApp()->getFonts();
    const auto rawScale = this->scale();
    const auto effectiveScale = rawScale;
    const int radius = std::max(1, int(3 * rawScale));
    const int inputPaddingX = std::max(4, int(6 * effectiveScale));
    const int inputMinHeight = std::max(16, int(24 * effectiveScale));
    const int scrollbarWidth = std::max(3, int(5 * effectiveScale));
    const int scrollbarRadius = std::max(1, int(2 * effectiveScale));
    const int scrollbarMinHeight = std::max(12, int(16 * effectiveScale));
    const int cardRadius = std::max(2, int(4 * rawScale));

    this->headerTitleLabel_->setFont(
        fonts->getFont(FontStyle::UiMediumBold, rawScale * 1.15F));
    if (this->searchInput_ != nullptr)
    {
        this->searchInput_->setFont(
            fonts->getFont(FontStyle::UiMedium, effectiveScale));
    }

    const int hMargin = contentHorizontalMargin(rawScale);
    const int vMargin = std::max(3, int(6 * rawScale));
    this->headerWidget_->layout()->setContentsMargins(0, 0, 0, 0);
    this->mainLayout_->setContentsMargins(hMargin, vMargin, hMargin, vMargin);
    this->contentLayout_->setContentsMargins(
        0, scaledMetric(effectiveScale, 4, 2), 0,
        scaledMetric(effectiveScale, 8, 4));
    this->contentLayout_->setSpacing(CLIP_CARD_SPACING);
    if (auto *sep = this->findChild<QWidget *>(
            QStringLiteral("UserClipsDialogSeparator")))
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

    const bool isBroadcaster =
        (this->activeRole_ == QStringLiteral("BROADCASTER"));
    if (this->broadcasterTab_ != nullptr)
    {
        this->broadcasterTab_->setFont(fonts->getFont(
            isBroadcaster ? FontStyle::UiMediumBold : FontStyle::UiMedium,
            effectiveScale));
        this->broadcasterTab_->setBorderColor(
            isBroadcaster ? theme->splits.header.focusedBorder
                          : theme->splits.header.border);
        this->broadcasterTab_->setMouseEffectColor(
            isBroadcaster
                ? std::optional<QColor>(theme->splits.header.focusedBorder)
                : std::nullopt);
    }
    if (this->curatorTab_ != nullptr)
    {
        this->curatorTab_->setFont(fonts->getFont(
            !isBroadcaster ? FontStyle::UiMediumBold : FontStyle::UiMedium,
            effectiveScale));
        this->curatorTab_->setBorderColor(
            !isBroadcaster ? theme->splits.header.focusedBorder
                           : theme->splits.header.border);
        this->curatorTab_->setMouseEffectColor(
            !isBroadcaster
                ? std::optional<QColor>(theme->splits.header.focusedBorder)
                : std::nullopt);
    }

    this->closeButton_->setColor(textColor);

    this->setStyleSheet(
        QStringLiteral(R"(
        QWidget#UserClipsDialogRoot {
            background: %1;
            color: %2;
        }
        QWidget#UserClipsHeader {
            background: transparent;
        }
        QFrame#UserClipsDialogSeparator,
        QWidget#UserClipsDialogSeparator {
            background: %3;
        }
        QScrollArea#UserClipsScrollArea {
            background: transparent;
            border: 0;
        }
        QWidget#UserClipsDialogContent {
            background: transparent;
            color: %2;
        }
        QLabel#UserClipsTitle {
            color: %2;
            font-weight: 700;
        }
        QLabel#UserClipsStatus,
        QLabel#UserClipsEmpty {
            color: %4;
            padding: 12px;
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
        QLineEdit#UserClipsSearch {
            background: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: %6px;
            padding: 0 %7px;
            min-height: %11px;
            selection-background-color: %12;
        }
        QLineEdit#UserClipsSearch:focus {
            border-color: %13;
        }
        QFrame#ClipCard {
            background: %14;
            border: 1px solid %3;
            border-radius: %15px;
        }
        QFrame#ClipCard[hovered="true"] {
            background: %17;
            border-color: %13;
        }
        QLabel#ClipCardTitle {
            color: %2;
            font-weight: 600;
            font-size: 12px;
        }
        QLabel#ClipCardGame {
            color: %13;
            font-size: 11px;
            font-weight: 500;
        }
        QLabel#ClipCardMeta {
            color: %4;
            font-size: 11px;
        }
        QLabel#ClipCardInfo {
            color: %4;
            font-size: 11px;
        }
    )")
            .arg(bg, text, border, muted, inputBg, QString::number(radius),
                 QString::number(inputPaddingX),
                 QString::number(scrollbarWidth),
                 QString::number(scrollbarRadius),
                 QString::number(scrollbarMinHeight),
                 QString::number(inputMinHeight), hoverBg, focusedBorder,
                 cardBg, QString::number(cardRadius),
                 QString::number(CLIP_CARD_PADDING), cardHoverBg));
}

QString UserClipsDialog::authTokenOrMessage()
{
    QString authError;
    const auto auth = MoltorinoAuth::resolveCurrentUserToken(&authError);
    if (auth.hasToken())
    {
        return auth.token;
    }

    const auto message =
        authError.isEmpty()
            ? MoltorinoAuth::authRequiredMessage("viewing clips")
            : authError;
    this->setStatus(message, true);
    return {};
}

}  // namespace chatterino
