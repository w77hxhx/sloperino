// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/SeventvCosmeticsDialog.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/Paint.hpp"
#include "providers/seventv/paints/PaintDropShadow.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/seventv/SeventvPaints.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Settings.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/buttons/Button.hpp"
#include "widgets/buttons/SvgButton.hpp"
#include "widgets/helper/Line.hpp"

#include <QByteArray>
#include <QColor>
#include <QCursor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

#include <cmath>

namespace chatterino {

namespace {

constexpr QSize DEFAULT_DIALOG_SIZE(540, 620);
constexpr int COSMETICS_GRID_SPACING = 6;
constexpr QSize BADGE_ICON_SIZE(22, 22);

int scaledMetric(float scale, int base, int minimum)
{
    if (scale <= 0.0F)
    {
        return minimum;
    }
    return std::max(minimum, int(std::round(base * scale)));
}

int contentHorizontalMargin(float scale)
{
    return scaledMetric(scale, 12, 6);
}

QString decodeJwtUserId(const QString &jwt)
{
    const auto parts = jwt.split('.');
    if (parts.size() < 2)
    {
        return {};
    }

    auto payload = parts[1].toUtf8();
    auto json = QByteArray::fromBase64(
        payload,
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    auto doc = QJsonDocument::fromJson(json);
    if (doc.isObject())
    {
        const auto sub = doc.object().value("sub").toString().trimmed();
        if (!sub.isEmpty())
        {
            return sub;
        }
    }

    // Normalize base64url to base64 fallback
    payload.replace('-', '+').replace('_', '/');
    while (payload.size() % 4 != 0)
    {
        payload.append('=');
    }

    const auto fallbackDoc =
        QJsonDocument::fromJson(QByteArray::fromBase64(payload));
    if (!fallbackDoc.isObject())
    {
        return {};
    }

    return fallbackDoc.object().value("sub").toString().trimmed();
}

QColor rgbaToQColor(const uint32_t color)
{
    auto red = (int)((color >> 24) & 0xFF);
    auto green = (int)((color >> 16) & 0xFF);
    auto blue = (int)((color >> 8) & 0xFF);
    auto alpha = (int)(color & 0xFF);

    return {red, green, blue, alpha};
}

std::optional<QColor> parsePaintColor(const QJsonValue &color)
{
    if (color.isNull() || color.isUndefined())
    {
        return std::nullopt;
    }

    return rgbaToQColor(uint32_t(color.toVariant().toLongLong()));
}

std::shared_ptr<Paint> parsePaintObject(const QJsonObject &obj)
{
    const auto id = obj.value("id").toString();
    const auto name = obj.value("name").toString();
    const auto fn = obj.value("function").toString().toLower();

    const auto color = parsePaintColor(obj.value("color"));
    const bool repeat = obj.value("repeat").toBool();
    const float angle = (float)obj.value("angle").toDouble();

    const auto stopsArray = obj.value("stops").toArray();
    QGradientStops stops;
    double lastStop = -1;
    for (const auto &s : stopsArray)
    {
        const auto sobj = s.toObject();
        const auto rgba =
            uint32_t(sobj.value("color").toVariant().toLongLong());
        auto pos = sobj.value("at").toDouble();
        if (pos <= lastStop)
        {
            pos = lastStop + 0.0000001;
        }
        lastStop = pos;
        stops.append(QGradientStop(pos, rgbaToQColor(rgba)));
    }

    std::vector<PaintDropShadow> shadows;
    const auto shadowsArray = obj.value("shadows").toArray().isEmpty()
                                  ? obj.value("drop_shadows").toArray()
                                  : obj.value("shadows").toArray();
    for (const auto &sh : shadowsArray)
    {
        const auto shobj = sh.toObject();
        const auto rgba =
            uint32_t(shobj.value("color").toVariant().toLongLong());
        shadows.emplace_back(shobj.value("x_offset").toDouble(),
                             shobj.value("y_offset").toDouble(),
                             shobj.value("radius").toDouble(),
                             rgbaToQColor(rgba));
    }

    if (fn == "linear_gradient" || fn == "linear-gradient")
    {
        return std::make_shared<LinearGradientPaint>(
            name, id, color, stops, repeat, angle, std::move(shadows));
    }
    else if (fn == "radial_gradient" || fn == "radial-gradient")
    {
        return std::make_shared<RadialGradientPaint>(
            name, id, stops, repeat, std::move(shadows));
    }
    else if (fn == "url" || fn == "image")
    {
        const QString url = obj.value("image_url").toString();
        const ImagePtr image = Image::fromUrl({url}, 1);
        if (image != nullptr)
        {
            return std::make_shared<UrlPaint>(name, id, image,
                                              std::move(shadows));
        }
    }

    return nullptr;
}

ImageSet makeBadgeImageSet(const QJsonObject &badgeObj)
{
    const auto host = badgeObj.value("host").toObject();
    auto baseUrl = host.value("url").toString();
    if (baseUrl.startsWith("//"))
    {
        baseUrl = QStringLiteral("https:") + baseUrl;
    }
    const auto files = host.value("files").toArray();

    ImagePtr image1;
    ImagePtr image2;
    ImagePtr image3;

    for (const auto &fVal : files)
    {
        const auto fObj = fVal.toObject();
        const auto name = fObj.value("name").toString();
        const auto width = fObj.value("width").toInt();
        const auto height = fObj.value("height").toInt();
        const auto format = fObj.value("format").toString().toUpper();

        if (format != "WEBP" && format != "AVIF" && format != "PNG" &&
            format != "GIF")
        {
            continue;
        }

        const auto fullUrl = baseUrl + "/" + name;
        if (name.startsWith("1x") && !image1)
        {
            image1 = Image::fromUrl(Url{fullUrl}, 1.0, {width, height});
        }
        else if (name.startsWith("2x") && !image2)
        {
            image2 = Image::fromUrl(Url{fullUrl}, 0.5, {width, height});
        }
        else if (name.startsWith("3x") && !image3)
        {
            image3 = Image::fromUrl(Url{fullUrl}, 0.333, {width, height});
        }
        else if (name.startsWith("4x") && !image3)
        {
            image3 = Image::fromUrl(Url{fullUrl}, 0.25, {width, height});
        }
    }

    if (!image2 && image1)
    {
        image2 = image1;
    }
    if (!image3 && image2)
    {
        image3 = image2;
    }
    if (!image1 && image2)
    {
        image1 = image2;
    }

    const auto empty = getEmptyImagePtr();
    return ImageSet{
        image1 ? image1 : empty,
        image2 ? image2 : empty,
        image3 ? image3 : empty,
    };
}

}  // namespace

// ============================================================================
// CosmeticPreviewWidget
// ============================================================================
class CosmeticPreviewWidget final : public QWidget
{
public:
    CosmeticPreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        this->setFixedHeight(64);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setCosmetics(const QString &username, const QColor &userColor,
                      const ImageSet &badgeImages,
                      const std::shared_ptr<Paint> &paint,
                      const QString &badgeName, const QString &paintName)
    {
        this->username_ =
            username.isEmpty() ? QStringLiteral("Username") : username;
        this->userColor_ = userColor.isValid() ? userColor : QColor("#bf94ff");
        this->badgeImages_ = badgeImages;
        this->paint_ = paint;
        this->badgeName_ =
            badgeName.isEmpty() ? QStringLiteral("None") : badgeName;
        this->paintName_ =
            paintName.isEmpty() ? QStringLiteral("None") : paintName;
        this->update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto *theme = getApp()->getThemes();
        auto *fonts = getApp()->getFonts();
        const auto rect = this->rect().adjusted(0, 0, -1, -1);

        // Card background & border
        auto bg = theme->splits.header.background;
        auto border = theme->splits.header.border;
        painter.setPen(QPen(border, 1));
        painter.setBrush(bg);
        painter.drawRoundedRect(rect, 6, 6);

        // Header label: "LIVE PREVIEW"
        auto smallFont = fonts->getFont(FontStyle::UiMedium, 0.85F);
        painter.setFont(smallFont);
        auto mutedColor = theme->window.text;
        mutedColor.setAlpha(160);
        painter.setPen(mutedColor);
        painter.drawText(QRect(12, 8, rect.width() - 24, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("LIVE PREVIEW"));

        // Detail summary on the right: "Badge: XDX • Paint: Tuxedo Cat"
        const QString infoStr = QStringLiteral("Badge: %1  •  Paint: %2")
                                    .arg(this->badgeName_, this->paintName_);
        painter.drawText(QRect(12, 8, rect.width() - 24, 16),
                         Qt::AlignRight | Qt::AlignVCenter, infoStr);

        // Content row (Badge + Painted Name)
        int currentX = 14;
        const int centerY = 38;

        // Badge
        if (!this->badgeImages_.getImage1()->isEmpty())
        {
            const auto &img = this->badgeImages_.getImageOrLoaded(1.0F);
            if (auto pixmap = img->pixmapOrLoad())
            {
                const int badgeSize = 20;
                const QRect badgeRect(currentX, centerY - badgeSize / 2,
                                      badgeSize, badgeSize);
                painter.drawPixmap(badgeRect, *pixmap);
                currentX += badgeSize + 8;
            }
        }

        // Username with Paint or User Color
        auto nameFont = fonts->getFont(FontStyle::ChatMediumBold, 1.1F);
        painter.setFont(nameFont);
        QFontMetrics fm(nameFont);
        const int textW = fm.horizontalAdvance(this->username_);
        const int textH = fm.height();
        const QRect textRect(currentX, centerY - textH / 2, textW + 10, textH);

        if (this->paint_)
        {
            const QSizeF sizeF(textW + 10, textH);
            const auto pix =
                this->paint_->getPixmap(this->username_, nameFont,
                                        this->userColor_, sizeF, 1.0F, 1.0F);
            painter.drawPixmap(currentX, centerY - textH / 2, pix);
        }
        else
        {
            painter.setPen(this->userColor_);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                             this->username_);
        }
    }

private:
    QString username_{QStringLiteral("Username")};
    QColor userColor_{QColor("#bf94ff")};
    ImageSet badgeImages_;
    std::shared_ptr<Paint> paint_;
    QString badgeName_{QStringLiteral("None")};
    QString paintName_{QStringLiteral("None")};
};

// ============================================================================
// BadgeCardWidget
// ============================================================================
class BadgeCardWidget final : public QPushButton
{
public:
    BadgeCardWidget(const QString &id, const QString &name,
                    const QString &description, const ImageSet &images,
                    bool isSelected, QWidget *parent = nullptr)
        : QPushButton(parent)
        , id_(id)
        , name_(name)
        , description_(description)
        , images_(images)
        , isSelected_(isSelected)
    {
        this->setCursor(Qt::PointingHandCursor);
        this->setFocusPolicy(Qt::StrongFocus);
        this->setAttribute(Qt::WA_Hover, true);
        this->setFixedHeight(44);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QString tip = name;
        if (!description.isEmpty() && description != name)
        {
            tip += QStringLiteral("\n") + description;
        }
        this->setToolTip(tip);
    }

    void setSelected(bool selected)
    {
        if (this->isSelected_ != selected)
        {
            this->isSelected_ = selected;
            this->update();
        }
    }

    [[nodiscard]] const QString &id() const
    {
        return this->id_;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto *theme = getApp()->getThemes();
        auto *fonts = getApp()->getFonts();
        const auto rect = this->rect().adjusted(0, 0, -1, -1);

        // Card background and border
        QColor bg = theme->splits.input.background;
        QColor border = theme->splits.header.border;

        if (this->underMouse())
        {
            bg = theme->isLightTheme() ? bg.darker(104) : bg.lighter(112);
            border = theme->splits.header.focusedBorder;
        }
        if (this->isSelected_)
        {
            border = QColor("#9146ff");  // 7TV accent purple
            bg = theme->isLightTheme() ? QColor("#f3edff") : QColor("#221533");
        }

        painter.setPen(QPen(border, this->isSelected_ ? 2 : 1));
        painter.setBrush(bg);
        painter.drawRoundedRect(rect, 6, 6);

        // Radio Indicator Circle
        const int radioX = 14;
        const int radioCenterY = rect.height() / 2;
        const int radioRadius = 7;
        painter.setPen(QPen(
            this->isSelected_ ? QColor("#9146ff") : theme->splits.header.border,
            1.5));
        painter.setBrush(this->isSelected_ ? QColor("#9146ff") : Qt::NoBrush);
        painter.drawEllipse(QPoint(radioX, radioCenterY), radioRadius,
                            radioRadius);
        if (this->isSelected_)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPoint(radioX, radioCenterY), 3, 3);
        }

        int textStartX = radioX + radioRadius + 12;

        // Badge Image
        if (!this->images_.getImage1()->isEmpty())
        {
            const auto &img = this->images_.getImageOrLoaded(1.0F);
            if (auto pixmap = img->pixmapOrLoad())
            {
                const int badgeSize = 20;
                const QRect badgeRect(textStartX, radioCenterY - badgeSize / 2,
                                      badgeSize, badgeSize);
                painter.drawPixmap(badgeRect, *pixmap);
                textStartX += badgeSize + 8;
            }
        }
        else if (this->id_ == "none")
        {
            // Draw None icon "Ø"
            painter.setFont(fonts->getFont(FontStyle::UiMediumBold, 1.0F));
            painter.setPen(theme->window.text);
            painter.drawText(QRect(textStartX, 0, 16, rect.height()),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             QStringLiteral("Ø"));
            textStartX += 18;
        }

        // Badge Name
        painter.setFont(fonts->getFont(FontStyle::UiMediumBold, 0.95F));
        painter.setPen(this->isSelected_ ? QColor(theme->isLightTheme()
                                                      ? "#6f2dbd"
                                                      : "#caa9ff")
                                         : theme->window.text);
        const QRect textRect(textStartX, 0, rect.width() - textStartX - 10,
                             rect.height());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                         this->name_);
    }

private:
    QString id_;
    QString name_;
    QString description_;
    ImageSet images_;
    bool isSelected_{false};
};

// ============================================================================
// PaintCardWidget
// ============================================================================
class PaintCardWidget final : public QPushButton
{
public:
    PaintCardWidget(const QString &id, const QString &name,
                    const std::shared_ptr<Paint> &paint, bool isSelected,
                    QWidget *parent = nullptr)
        : QPushButton(parent)
        , id_(id)
        , name_(name)
        , paint_(paint)
        , isSelected_(isSelected)
    {
        this->setCursor(Qt::PointingHandCursor);
        this->setFocusPolicy(Qt::StrongFocus);
        this->setAttribute(Qt::WA_Hover, true);
        this->setFixedHeight(44);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        this->setToolTip(name);
    }

    void setSelected(bool selected)
    {
        if (this->isSelected_ != selected)
        {
            this->isSelected_ = selected;
            this->update();
        }
    }

    [[nodiscard]] const QString &id() const
    {
        return this->id_;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto *theme = getApp()->getThemes();
        auto *fonts = getApp()->getFonts();
        const auto rect = this->rect().adjusted(0, 0, -1, -1);

        // Card background and border
        QColor bg = theme->splits.input.background;
        QColor border = theme->splits.header.border;

        if (this->underMouse())
        {
            bg = theme->isLightTheme() ? bg.darker(104) : bg.lighter(112);
            border = theme->splits.header.focusedBorder;
        }
        if (this->isSelected_)
        {
            border = QColor("#9146ff");  // 7TV accent purple
            bg = theme->isLightTheme() ? QColor("#f3edff") : QColor("#221533");
        }

        painter.setPen(QPen(border, this->isSelected_ ? 2 : 1));
        painter.setBrush(bg);
        painter.drawRoundedRect(rect, 6, 6);

        // Radio Indicator Circle
        const int radioX = 14;
        const int radioCenterY = rect.height() / 2;
        const int radioRadius = 7;
        painter.setPen(QPen(
            this->isSelected_ ? QColor("#9146ff") : theme->splits.header.border,
            1.5));
        painter.setBrush(this->isSelected_ ? QColor("#9146ff") : Qt::NoBrush);
        painter.drawEllipse(QPoint(radioX, radioCenterY), radioRadius,
                            radioRadius);
        if (this->isSelected_)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPoint(radioX, radioCenterY), 3, 3);
        }

        const int textStartX = radioX + radioRadius + 12;
        auto font = fonts->getFont(FontStyle::UiMediumBold, 1.0F);
        painter.setFont(font);

        if (this->paint_)
        {
            QFontMetrics fm(font);
            const int textW = fm.horizontalAdvance(this->name_);
            const int textH = fm.height();
            const QSizeF sizeF(textW + 10, textH);
            const auto pix = this->paint_->getPixmap(
                this->name_, font, Qt::white, sizeF, 1.0F, 1.0F);
            painter.drawPixmap(textStartX, radioCenterY - textH / 2, pix);
        }
        else
        {
            painter.setPen(this->isSelected_ ? QColor(theme->isLightTheme()
                                                          ? "#6f2dbd"
                                                          : "#caa9ff")
                                             : theme->window.text);
            const QRect textRect(textStartX, 0, rect.width() - textStartX - 10,
                                 rect.height());
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                             this->name_);
        }
    }

private:
    QString id_;
    QString name_;
    std::shared_ptr<Paint> paint_;
    bool isSelected_{false};
};

// ============================================================================
// SeventvCosmeticsDialog
// ============================================================================
SeventvCosmeticsDialog::SeventvCosmeticsDialog(TwitchChannel *channel,
                                               QWidget *parent)
    : DraggablePopup(true, parent)
    , channel_(channel)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setMinimumSize(DEFAULT_DIALOG_SIZE);
    this->resize(DEFAULT_DIALOG_SIZE);
    this->setWindowTitle(QStringLiteral("7TV Cosmetics"));

    auto *container = this->getLayoutContainer();
    container->setObjectName("SeventvCosmeticsRoot");
    this->mainLayout_ = new QVBoxLayout(container);
    this->mainLayout_->setContentsMargins(14, 12, 14, 14);
    this->mainLayout_->setSpacing(10);

    // 1. Header (Title, Pin, Close)
    this->headerWidget_ = new QWidget(container);
    this->headerWidget_->setObjectName("SeventvCosmeticsHeader");
    auto *headerLayout = new QHBoxLayout(this->headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(scaledMetric(this->scale(), 6, 3));

    this->headerTitleLabel_ =
        new QLabel(QStringLiteral("7TV Cosmetics"), this->headerWidget_);
    this->headerTitleLabel_->setObjectName("SeventvCosmeticsTitle");
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
    this->mainLayout_->addWidget(this->headerWidget_);

    // 2. Separator
    auto *headerSep = new QFrame(container);
    headerSep->setFrameShape(QFrame::HLine);
    headerSep->setFrameShadow(QFrame::Plain);
    headerSep->setObjectName("SeventvCosmeticsSeparator");
    this->mainLayout_->addWidget(headerSep);

    // 3. Live Preview Widget
    this->previewWidget_ = new CosmeticPreviewWidget(container);
    this->mainLayout_->addWidget(this->previewWidget_);

    // 4. Tab Switcher & Search Row
    this->controlsRowWidget_ = new QWidget(container);
    auto *controlsLayout = new QHBoxLayout(this->controlsRowWidget_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);

    this->badgesTabButton_ =
        new QPushButton(QStringLiteral("Badges"), this->controlsRowWidget_);
    this->badgesTabButton_->setObjectName("SeventvTabButton");
    this->badgesTabButton_->setCheckable(true);
    this->badgesTabButton_->setChecked(true);
    this->badgesTabButton_->setCursor(Qt::PointingHandCursor);

    this->paintsTabButton_ =
        new QPushButton(QStringLiteral("Paints"), this->controlsRowWidget_);
    this->paintsTabButton_->setObjectName("SeventvTabButton");
    this->paintsTabButton_->setCheckable(true);
    this->paintsTabButton_->setChecked(false);
    this->paintsTabButton_->setCursor(Qt::PointingHandCursor);

    QObject::connect(this->badgesTabButton_, &QPushButton::clicked, this,
                     [this] {
                         this->switchView(View::Badges);
                     });
    QObject::connect(this->paintsTabButton_, &QPushButton::clicked, this,
                     [this] {
                         this->switchView(View::Paints);
                     });

    controlsLayout->addWidget(this->badgesTabButton_);
    controlsLayout->addWidget(this->paintsTabButton_);
    controlsLayout->addSpacing(10);

    this->searchInput_ = new QLineEdit(this->controlsRowWidget_);
    this->searchInput_->setObjectName("SeventvCosmeticsSearch");
    this->searchInput_->setPlaceholderText(QStringLiteral("Search badges..."));
    this->searchInput_->setClearButtonEnabled(true);
    QObject::connect(this->searchInput_, &QLineEdit::textChanged, this,
                     [this](const QString &text) {
                         this->searchQuery_ = text;
                         this->rebuildContent();
                     });
    controlsLayout->addWidget(this->searchInput_, 1);
    this->mainLayout_->addWidget(this->controlsRowWidget_);

    // 5. Scroll Area & Content Layout
    this->scrollArea_ = new QScrollArea(container);
    this->scrollArea_->setObjectName("SeventvCosmeticsScrollArea");
    this->scrollArea_->setFrameShape(QFrame::NoFrame);
    this->scrollArea_->setWidgetResizable(true);
    this->scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    this->contentWidget_ = new QWidget();
    this->contentWidget_->setObjectName("SeventvCosmeticsContent");
    this->contentLayout_ = new QVBoxLayout(this->contentWidget_);
    this->contentLayout_->setContentsMargins(0, 4, 0, 8);
    this->contentLayout_->setSpacing(COSMETICS_GRID_SPACING);
    this->scrollArea_->setWidget(this->contentWidget_);
    this->mainLayout_->addWidget(this->scrollArea_, 1);

    // Repaint on GIF animation tick
    this->signalHolder_.managedConnect(
        getApp()->getWindows()->gifRepaintRequested, [this] {
            if (this->previewWidget_ != nullptr)
            {
                this->previewWidget_->update();
            }
            if (this->contentWidget_ != nullptr)
            {
                this->contentWidget_->update();
            }
        });

    this->refreshStyle();
    this->applySizeConstraints();
    this->updatePreview();
}

void SeventvCosmeticsDialog::showDialog(TwitchChannel *channel, QWidget *parent)
{
    static QPointer<SeventvCosmeticsDialog> activeDialog;
    if (activeDialog != nullptr)
    {
        activeDialog->close();
        activeDialog->deleteLater();
    }

    auto *dialog = new SeventvCosmeticsDialog(channel, parent);
    activeDialog = dialog;
    dialog->ensurePinned();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

QString SeventvCosmeticsDialog::getSeventvToken() const
{
    return getSettings()->seventvToken.getValue().trimmed();
}

QString SeventvCosmeticsDialog::getSeventvUserId() const
{
    if (!this->seventvUserId_.isEmpty())
    {
        return this->seventvUserId_;
    }

    const auto savedId = getSettings()->seventvUserId.getValue().trimmed();
    if (!savedId.isEmpty())
    {
        return savedId;
    }

    const auto token = this->getSeventvToken();
    if (!token.isEmpty())
    {
        const auto jwtId = decodeJwtUserId(token);
        if (!jwtId.isEmpty())
        {
            return jwtId;
        }
    }

    return {};
}

void SeventvCosmeticsDialog::showEvent(QShowEvent *event)
{
    DraggablePopup::showEvent(event);
    if (!this->loaded_)
    {
        this->loadCosmetics();
    }
    this->sendPresence();
}

void SeventvCosmeticsDialog::resizeEvent(QResizeEvent *event)
{
    DraggablePopup::resizeEvent(event);
}

void SeventvCosmeticsDialog::scaleChangedEvent(float /*scale*/)
{
    this->refreshStyle();
    this->applySizeConstraints();
    this->updatePreview();
    this->rebuildContent();
}

void SeventvCosmeticsDialog::themeChangedEvent()
{
    this->refreshStyle();
    this->updatePreview();
    this->rebuildContent();
}

void SeventvCosmeticsDialog::applySizeConstraints()
{
    const int minW = std::max(460, int(DEFAULT_DIALOG_SIZE.width() * 0.8));
    const int minH = std::max(480, int(DEFAULT_DIALOG_SIZE.height() * 0.8));
    this->setMinimumSize(minW, minH);
}

void SeventvCosmeticsDialog::switchView(View view)
{
    if (this->currentView_ == view)
    {
        return;
    }

    this->currentView_ = view;
    this->badgesTabButton_->setChecked(view == View::Badges);
    this->paintsTabButton_->setChecked(view == View::Paints);
    this->searchInput_->setPlaceholderText(
        view == View::Badges ? QStringLiteral("Search badges...")
                             : QStringLiteral("Search paints..."));
    this->searchInput_->clear();
    this->searchQuery_.clear();
    this->rebuildContent();
}

void SeventvCosmeticsDialog::setStatus(const QString &text, bool error)
{
    this->statusText_ = text;
    this->statusIsError_ = error;
    if (this->statusLabel_ != nullptr)
    {
        this->statusLabel_->setText(text);
        this->statusLabel_->setVisible(!text.isEmpty());
    }
}

void SeventvCosmeticsDialog::sendPresence()
{
    const auto token = this->getSeventvToken();
    const auto userId = this->getSeventvUserId();
    if (userId.isEmpty() || !this->channel_)
    {
        return;
    }

    const auto targetTwitchId = this->channel_->roomId();
    if (targetTwitchId.isEmpty())
    {
        return;
    }

    QJsonObject data;
    data["platform"] = QStringLiteral("TWITCH");
    data["id"] = targetTwitchId;

    QJsonObject root;
    root["kind"] = 1;
    root["passive"] = true;
    root["data"] = data;

    const auto urlStr =
        QStringLiteral("https://7tv.io/v3/users/%1/presences").arg(userId);
    auto req =
        NetworkRequest(QUrl(urlStr), NetworkRequestType::Post).json(root);
    if (!token.isEmpty())
    {
        req = std::move(req).header(
            "Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    }
    std::move(req).execute();
}

void SeventvCosmeticsDialog::loadCosmetics(bool /*force*/)
{
    if (this->loading_)
    {
        return;
    }

    const auto token = this->getSeventvToken();
    if (token.isEmpty())
    {
        this->setStatus(
            QStringLiteral(
                "Please add your 7TV token in Settings -> Sloperino to view "
                "and change your 7TV cosmetics."),
            true);
        return;
    }

    this->loading_ = true;
    this->setStatus(QStringLiteral("Loading 7TV cosmetics from 7TV API..."));

    QPointer<SeventvCosmeticsDialog> self = this;

    QJsonObject gqlQuery;
    gqlQuery.insert(
        QStringLiteral("query"),
        QStringLiteral(
            "query GetCosmeticsAndUser { "
            "  cosmetics { "
            "    paints { "
            "      id name color repeat angle "
            "      stops { at color } "
            "      shadows { x_offset y_offset radius color } "
            "      function shape image_url "
            "    } "
            "    badges { "
            "      id name tooltip tag "
            "      host { "
            "        url "
            "        files { name format width height } "
            "      } "
            "    } "
            "  } "
            "  user: actor { "
            "    id username display_name avatar_url "
            "    connections { id platform username display_name } "
            "    style { color paint_id badge_id } "
            "    cosmetics { id kind selected } "
            "  } "
            "}"));

    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/gql")),
                   NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(token).toUtf8())
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("User-Agent", "Chatterino")
        .json(gqlQuery)
        .timeout(20000)
        .onSuccess([self](const auto &result) {
            if (!self)
            {
                return;
            }
            self->loading_ = false;
            self->loaded_ = true;

            const auto rootObj = result.parseJson();
            const auto dataObj = rootObj.value("data").toObject();
            const auto cosmeticsObj = dataObj.value("cosmetics").toObject();
            const auto userObj = dataObj.value("user").toObject();

            if (dataObj.isEmpty())
            {
                self->setStatus(
                    QStringLiteral("Failed to load 7TV cosmetics response."),
                    true);
                return;
            }

            // 1. Parse all catalog paints
            self->allPaintsMap_.clear();
            const auto paintsArr = cosmeticsObj.value("paints").toArray();
            for (const auto &pVal : paintsArr)
            {
                const auto pObj = pVal.toObject();
                const auto pId = pObj.value("id").toString();
                if (pId.isEmpty())
                {
                    continue;
                }
                SeventvPaintItem item;
                item.id = pId;
                item.name = pObj.value("name").toString();
                item.rawJson = pObj;
                item.paint = parsePaintObject(pObj);
                self->allPaintsMap_[pId] = std::move(item);
            }

            // 2. Parse all catalog badges
            self->allBadgesMap_.clear();
            const auto badgesArr = cosmeticsObj.value("badges").toArray();
            for (const auto &bVal : badgesArr)
            {
                const auto bObj = bVal.toObject();
                const auto bId = bObj.value("id").toString();
                if (bId.isEmpty())
                {
                    continue;
                }
                SeventvBadgeItem item;
                item.id = bId;
                item.name = bObj.value("name").toString();
                item.description = bObj.value("tooltip").toString();
                item.images = makeBadgeImageSet(bObj);
                self->allBadgesMap_[bId] = std::move(item);
            }

            // 3. Parse user info & owned cosmetics
            self->seventvUserId_ = userObj.value("id").toString().trimmed();
            self->seventvUsername_ =
                userObj.value("username").toString().trimmed();
            self->seventvDisplayName_ =
                userObj.value("display_name").toString().trimmed();

            if (!self->seventvUserId_.isEmpty())
            {
                getSettings()->seventvUserId.setValue(self->seventvUserId_);
            }
            if (!self->seventvUsername_.isEmpty())
            {
                getSettings()->seventvUsername.setValue(self->seventvUsername_);
            }

            // Parse 7TV user connections
            self->connections_.clear();
            const auto connArr = userObj.value("connections").toArray();
            for (const auto &cVal : connArr)
            {
                const auto cObj = cVal.toObject();
                SeventvConnection conn;
                conn.id = cObj.value("id").toString();
                conn.platform = cObj.value("platform").toString();
                conn.username = cObj.value("username").toString();
                conn.displayName = cObj.value("display_name").toString();
                self->connections_.push_back(std::move(conn));
            }

            const auto styleObj = userObj.value("style").toObject();
            self->activePaintId_ = styleObj.value("paint_id").toString();
            self->activeBadgeId_ = styleObj.value("badge_id").toString();
            const auto colVal = styleObj.value("color");
            if (!colVal.isNull() && !colVal.isUndefined())
            {
                self->seventvColor_ = rgbaToQColor(
                    uint32_t(colVal.toVariant().toLongLong()));
            }

            // Populate user's owned cosmetics
            self->paints_.clear();
            self->badges_.clear();

            const auto userCosmeticsArr =
                userObj.value("cosmetics").toArray();
            for (const auto &cVal : userCosmeticsArr)
            {
                const auto cObj = cVal.toObject();
                const auto cId = cObj.value("id").toString();
                const auto kind = cObj.value("kind").toString().toUpper();
                const bool selected = cObj.value("selected").toBool();

                if (kind == "PAINT")
                {
                    if (selected && self->activePaintId_.isEmpty())
                    {
                        self->activePaintId_ = cId;
                    }
                    auto it = self->allPaintsMap_.find(cId);
                    if (it != self->allPaintsMap_.end())
                    {
                        self->paints_.push_back(it->second);
                    }
                    else
                    {
                        SeventvPaintItem fallback;
                        fallback.id = cId;
                        fallback.name = QStringLiteral("Paint %1").arg(cId);
                        self->paints_.push_back(std::move(fallback));
                    }
                }
                else if (kind == "BADGE")
                {
                    if (selected && self->activeBadgeId_.isEmpty())
                    {
                        self->activeBadgeId_ = cId;
                    }
                    auto it = self->allBadgesMap_.find(cId);
                    if (it != self->allBadgesMap_.end())
                    {
                        self->badges_.push_back(it->second);
                    }
                    else
                    {
                        SeventvBadgeItem fallback;
                        fallback.id = cId;
                        fallback.name = QStringLiteral("Badge %1").arg(cId);
                        self->badges_.push_back(std::move(fallback));
                    }
                }
            }

            // If user has no owned cosmetics listed, fallback to all catalog
            if (self->paints_.empty() && !self->allPaintsMap_.empty())
            {
                for (const auto &pair : self->allPaintsMap_)
                {
                    self->paints_.push_back(pair.second);
                }
            }
            if (self->badges_.empty() && !self->allBadgesMap_.empty())
            {
                for (const auto &pair : self->allBadgesMap_)
                {
                    self->badges_.push_back(pair.second);
                }
            }

            // Apply cosmetics to Chatterino for 7TV account & connections
            self->applyCosmeticsToChatterino();

            // Update Tab Button Labels
            self->badgesTabButton_->setText(
                QStringLiteral("Badges (%1)").arg(self->badges_.size()));
            self->paintsTabButton_->setText(
                QStringLiteral("Paints (%1)").arg(self->paints_.size()));

            self->setStatus({});
            self->rebuildContent();
            self->updatePreview();
            self->sendPresence();
        })
        .onError([self](const auto &result) {
            if (!self)
            {
                return;
            }
            self->loading_ = false;
            self->setStatus(
                QStringLiteral("Error loading 7TV cosmetics: %1")
                    .arg(result.formatError()),
                true);
        })
        .execute();
}

void SeventvCosmeticsDialog::applyCosmeticsToChatterino()
{
    // 1. Paint assignment to 7TV username & connected accounts
    if (!this->activePaintId_.isEmpty())
    {
        auto it = this->allPaintsMap_.find(this->activePaintId_);
        if (it != this->allPaintsMap_.end() && !it->second.rawJson.isEmpty())
        {
            getApp()->getSeventvPaints()->addPaint(it->second.rawJson);
        }

        if (!this->seventvUsername_.isEmpty())
        {
            getApp()->getSeventvPaints()->assignPaintToUser(
                this->activePaintId_, this->seventvUsername_, false);
        }

        for (const auto &conn : this->connections_)
        {
            const bool isKick = (conn.platform.toUpper() == "KICK");
            getApp()->getSeventvPaints()->assignPaintToUser(
                this->activePaintId_, conn.username, isKick);
        }
    }
    else
    {
        if (!this->seventvUsername_.isEmpty())
        {
            getApp()->getSeventvPaints()->clearPaintFromUser(
                this->seventvUsername_, false);
        }

        for (const auto &conn : this->connections_)
        {
            const bool isKick = (conn.platform.toUpper() == "KICK");
            getApp()->getSeventvPaints()->clearPaintFromUser(
                conn.username, isKick);
        }
    }

    // 2. Badge assignment to 7TV connected Twitch accounts
    for (const auto &conn : this->connections_)
    {
        if (conn.platform.toUpper() == "TWITCH")
        {
            if (!this->activeBadgeId_.isEmpty())
            {
                getApp()->getSeventvBadges()->assignBadgeToUser(
                    this->activeBadgeId_, UserId{conn.id});
            }
            else
            {
                getApp()->getSeventvBadges()->clearBadgeFromUser(
                    this->activeBadgeId_, UserId{conn.id});
            }
        }
    }

    postToThread([] {
        getApp()->getWindows()->invalidateChannelViewBuffers();
    });
}

void SeventvCosmeticsDialog::selectPaint(const QString &paintId)
{
    const auto token = this->getSeventvToken();
    const auto userId = this->getSeventvUserId();
    if (token.isEmpty() || userId.isEmpty())
    {
        this->setStatus(
            QStringLiteral(
                "Please add your 7TV token in Settings -> Sloperino."),
            true);
        return;
    }

    const auto previousPaintId = this->activePaintId_;
    const bool isNone = (paintId.isEmpty() || paintId == "none");

    if (isNone && previousPaintId.isEmpty())
    {
        return;
    }
    if (!isNone && previousPaintId == paintId)
    {
        return;
    }

    // Optimistic local update so UI and chat reflect the change immediately
    this->activePaintId_ = (isNone ? QString() : paintId);
    this->applyCosmeticsToChatterino();
    this->updatePreview();
    this->rebuildContent();

    this->setStatus(QStringLiteral("Updating 7TV paint..."));

    QPointer<SeventvCosmeticsDialog> self = this;

    QJsonObject vars;
    vars["userId"] = userId;

    QString mutationQuery;
    if (isNone)
    {
        vars["paintId"] = previousPaintId;
        mutationQuery =
            QStringLiteral("mutation UnselectPaint($userId: ObjectID!, $paintId: "
                           "ObjectID!) { "
                           "  user(id: $userId) { "
                           "    cosmetics(update: { id: $paintId, kind: PAINT, "
                           "selected: false }) "
                           "  } "
                           "}");
    }
    else
    {
        vars["paintId"] = paintId;
        mutationQuery =
            QStringLiteral("mutation SelectPaint($userId: ObjectID!, $paintId: "
                           "ObjectID!) { "
                           "  user(id: $userId) { "
                           "    cosmetics(update: { id: $paintId, kind: PAINT, "
                           "selected: true }) "
                           "  } "
                           "}");
    }

    QJsonObject root;
    root["query"] = mutationQuery;
    root["variables"] = vars;

    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/gql")),
                   NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(token).toUtf8())
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("User-Agent", "Chatterino")
        .json(root)
        .timeout(15000)
        .onSuccess([self, previousPaintId](const auto &res) {
            if (!self)
            {
                return;
            }
            const auto json = res.parseJson();
            if (json.contains("errors"))
            {
                self->activePaintId_ = previousPaintId;
                self->applyCosmeticsToChatterino();
                self->updatePreview();
                self->rebuildContent();
                self->setStatus(
                    QStringLiteral("Failed to update paint on 7TV."), true);
                return;
            }
            self->setStatus({});
            self->sendPresence();
        })
        .onError([self, previousPaintId](const auto &res) {
            if (!self)
            {
                return;
            }
            self->activePaintId_ = previousPaintId;
            self->applyCosmeticsToChatterino();
            self->updatePreview();
            self->rebuildContent();
            self->setStatus(
                QStringLiteral("Failed to update paint: %1")
                    .arg(res.formatError()),
                true);
        })
        .execute();
}

void SeventvCosmeticsDialog::selectBadge(const QString &badgeId)
{
    const auto token = this->getSeventvToken();
    const auto userId = this->getSeventvUserId();
    if (token.isEmpty() || userId.isEmpty())
    {
        this->setStatus(
            QStringLiteral(
                "Please add your 7TV token in Settings -> Sloperino."),
            true);
        return;
    }

    const auto previousBadgeId = this->activeBadgeId_;
    const bool isNone = (badgeId.isEmpty() || badgeId == "none");

    if (isNone && previousBadgeId.isEmpty())
    {
        return;
    }
    if (!isNone && previousBadgeId == badgeId)
    {
        return;
    }

    // Optimistic local update so UI and chat reflect the change immediately
    this->activeBadgeId_ = (isNone ? QString() : badgeId);
    this->applyCosmeticsToChatterino();
    this->updatePreview();
    this->rebuildContent();

    this->setStatus(QStringLiteral("Updating 7TV badge..."));

    QPointer<SeventvCosmeticsDialog> self = this;

    QJsonObject vars;
    vars["userId"] = userId;

    QString mutationQuery;
    if (isNone)
    {
        vars["badgeId"] = previousBadgeId;
        mutationQuery =
            QStringLiteral("mutation UnselectBadge($userId: ObjectID!, $badgeId: "
                           "ObjectID!) { "
                           "  user(id: $userId) { "
                           "    cosmetics(update: { id: $badgeId, kind: BADGE, "
                           "selected: false }) "
                           "  } "
                           "}");
    }
    else
    {
        vars["badgeId"] = badgeId;
        mutationQuery =
            QStringLiteral("mutation SelectBadge($userId: ObjectID!, $badgeId: "
                           "ObjectID!) { "
                           "  user(id: $userId) { "
                           "    cosmetics(update: { id: $badgeId, kind: BADGE, "
                           "selected: true }) "
                           "  } "
                           "}");
    }

    QJsonObject root;
    root["query"] = mutationQuery;
    root["variables"] = vars;

    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/gql")),
                   NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(token).toUtf8())
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("User-Agent", "Chatterino")
        .json(root)
        .timeout(15000)
        .onSuccess([self, previousBadgeId](const auto &res) {
            if (!self)
            {
                return;
            }
            const auto json = res.parseJson();
            if (json.contains("errors"))
            {
                self->activeBadgeId_ = previousBadgeId;
                self->applyCosmeticsToChatterino();
                self->updatePreview();
                self->rebuildContent();
                self->setStatus(
                    QStringLiteral("Failed to update badge on 7TV."), true);
                return;
            }
            self->setStatus({});
            self->sendPresence();
        })
        .onError([self, previousBadgeId](const auto &res) {
            if (!self)
            {
                return;
            }
            self->activeBadgeId_ = previousBadgeId;
            self->applyCosmeticsToChatterino();
            self->updatePreview();
            self->rebuildContent();
            self->setStatus(
                QStringLiteral("Failed to update badge: %1")
                    .arg(res.formatError()),
                true);
        })
        .execute();
}

void SeventvCosmeticsDialog::clearContent()
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
        else if (auto *layout = child->layout())
        {
            QLayoutItem *subChild = nullptr;
            while ((subChild = layout->takeAt(0)) != nullptr)
            {
                if (auto *subWidget = subChild->widget())
                {
                    subWidget->deleteLater();
                }
                delete subChild;
            }
            layout->deleteLater();
        }
        delete child;
    }
    this->statusLabel_ = nullptr;
}

void SeventvCosmeticsDialog::rebuildContent()
{
    this->clearContent();

    this->statusLabel_ = new QLabel(this->contentWidget_);
    this->statusLabel_->setObjectName("SeventvCosmeticsStatus");
    this->statusLabel_->setWordWrap(true);
    this->statusLabel_->setAlignment(Qt::AlignCenter);
    this->statusLabel_->setVisible(!this->statusText_.isEmpty());
    this->statusLabel_->setText(this->statusText_);
    this->contentLayout_->addWidget(this->statusLabel_);

    if (this->currentView_ == View::Badges)
    {
        this->rebuildBadges();
    }
    else
    {
        this->rebuildPaints();
    }

    this->contentLayout_->addStretch(1);
}

void SeventvCosmeticsDialog::rebuildBadges()
{
    auto *gridLayout = new QGridLayout();
    gridLayout->setSpacing(COSMETICS_GRID_SPACING);
    gridLayout->setContentsMargins(0, 0, 0, 0);

    // 1. None Option Card
    auto *noneCard = new BadgeCardWidget(
        QStringLiteral("none"), QStringLiteral("None"),
        QStringLiteral("Default (No 7TV badge)"), ImageSet{},
        this->activeBadgeId_.isEmpty(), this->contentWidget_);
    QObject::connect(noneCard, &QPushButton::clicked, this, [this] {
        this->selectBadge("none");
    });
    gridLayout->addWidget(noneCard, 0, 0);

    const auto needle = this->searchQuery_.trimmed();
    int count = 1;
    int row = 0;
    int col = 1;

    for (const auto &b : this->badges_)
    {
        if (!needle.isEmpty() &&
            !b.name.contains(needle, Qt::CaseInsensitive) &&
            !b.description.contains(needle, Qt::CaseInsensitive))
        {
            continue;
        }

        auto *card = new BadgeCardWidget(
            b.id, b.name, b.description, b.images,
            b.id == this->activeBadgeId_, this->contentWidget_);
        const auto bid = b.id;
        QObject::connect(card, &QPushButton::clicked, this, [this, bid] {
            this->selectBadge(bid);
        });

        gridLayout->addWidget(card, row, col);
        col++;
        if (col >= 2)
        {
            col = 0;
            row++;
        }
        count++;
    }

    this->contentLayout_->addLayout(gridLayout);

    if (count == 1 && !needle.isEmpty())
    {
        auto *emptyLabel =
            new QLabel(QStringLiteral("No matching 7TV badges found."),
                       this->contentWidget_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        this->contentLayout_->addWidget(emptyLabel);
    }
}

void SeventvCosmeticsDialog::rebuildPaints()
{
    auto *gridLayout = new QGridLayout();
    gridLayout->setSpacing(COSMETICS_GRID_SPACING);
    gridLayout->setContentsMargins(0, 0, 0, 0);

    // 1. None Option Card
    auto *noneCard = new PaintCardWidget(
        QStringLiteral("none"), QStringLiteral("None"), nullptr,
        this->activePaintId_.isEmpty(), this->contentWidget_);
    QObject::connect(noneCard, &QPushButton::clicked, this, [this] {
        this->selectPaint("none");
    });
    gridLayout->addWidget(noneCard, 0, 0);

    const auto needle = this->searchQuery_.trimmed();
    int count = 1;
    int row = 0;
    int col = 1;

    for (const auto &p : this->paints_)
    {
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
        {
            continue;
        }

        auto *card = new PaintCardWidget(
            p.id, p.name, p.paint, p.id == this->activePaintId_,
            this->contentWidget_);
        const auto pid = p.id;
        QObject::connect(card, &QPushButton::clicked, this, [this, pid] {
            this->selectPaint(pid);
        });

        gridLayout->addWidget(card, row, col);
        col++;
        if (col >= 2)
        {
            col = 0;
            row++;
        }
        count++;
    }

    this->contentLayout_->addLayout(gridLayout);

    if (count == 1 && !needle.isEmpty())
    {
        auto *emptyLabel =
            new QLabel(QStringLiteral("No matching 7TV paints found."),
                       this->contentWidget_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        this->contentLayout_->addWidget(emptyLabel);
    }
}

void SeventvCosmeticsDialog::updatePreview()
{
    if (this->previewWidget_ == nullptr)
    {
        return;
    }

    QString displayName = this->seventvDisplayName_;
    if (displayName.isEmpty())
    {
        displayName = this->seventvUsername_;
    }
    if (displayName.isEmpty())
    {
        displayName = QStringLiteral("7TV User");
    }

    QColor userColor = this->seventvColor_.isValid() ? this->seventvColor_
                                                     : QColor("#bf94ff");

    // Resolve active badge
    ImageSet badgeImages;
    QString badgeName = QStringLiteral("None");
    if (!this->activeBadgeId_.isEmpty())
    {
        auto it = this->allBadgesMap_.find(this->activeBadgeId_);
        if (it != this->allBadgesMap_.end())
        {
            badgeImages = it->second.images;
            badgeName = it->second.name;
        }
    }

    // Resolve active paint
    std::shared_ptr<Paint> paint;
    QString paintName = QStringLiteral("None");
    if (!this->activePaintId_.isEmpty())
    {
        auto it = this->allPaintsMap_.find(this->activePaintId_);
        if (it != this->allPaintsMap_.end())
        {
            paint = it->second.paint;
            paintName = it->second.name;
        }
    }

    this->previewWidget_->setCosmetics(displayName, userColor, badgeImages,
                                       paint, badgeName, paintName);
}

void SeventvCosmeticsDialog::refreshStyle()
{
    auto *fonts = getApp()->getFonts();
    const auto rawScale = this->scale();
    const auto effectiveScale = rawScale;

    this->headerTitleLabel_->setFont(
        fonts->getFont(FontStyle::UiMediumBold, rawScale * 1.15F));
    if (this->searchInput_ != nullptr)
    {
        this->searchInput_->setFont(
            fonts->getFont(FontStyle::UiMedium, effectiveScale));
    }

    const int hMargin = contentHorizontalMargin(rawScale);
    const int vMargin = std::max(8, int(10 * rawScale));
    this->mainLayout_->setContentsMargins(hMargin, vMargin, hMargin, vMargin);

    const auto *theme = this->theme;
    auto textColor = theme->window.text;
    auto mutedColor = textColor;
    mutedColor.setAlpha(theme->isLightTheme() ? 190 : 215);
    const auto bg = theme->window.background.name();
    const auto text = textColor.name(QColor::HexArgb);
    const auto border = theme->splits.header.border.name();
    const auto muted = mutedColor.name(QColor::HexArgb);
    const auto inputBg = theme->splits.input.background.name();
    const auto focusedBorder = theme->splits.header.focusedBorder.name();
    const auto tabSelectedBg = theme->tabs.selected.backgrounds.regular.name();
    const auto tabSelectedText = theme->tabs.selected.text.name();

    this->setStyleSheet(QStringLiteral(R"(
        QWidget#SeventvCosmeticsRoot {
            background: %1;
            color: %2;
        }
        QLabel#SeventvCosmeticsTitle {
            color: %2;
            font-weight: 700;
        }
        QFrame#SeventvCosmeticsSeparator {
            background: %3;
            color: %3;
            max-height: 1px;
            margin: 2px 0px;
        }
        QLabel#SeventvCosmeticsStatus {
            color: %4;
            padding: 8px;
            font-size: 11px;
        }
        QPushButton#SeventvTabButton {
            background: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: 5px;
            padding: 5px 14px;
            font-weight: 700;
            font-size: 11px;
        }
        QPushButton#SeventvTabButton:hover {
            border-color: %6;
        }
        QPushButton#SeventvTabButton:checked {
            background: #9146ff;
            color: #ffffff;
            border: 1px solid #9146ff;
        }
        QLineEdit#SeventvCosmeticsSearch {
            background: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: 5px;
            padding: 4px 8px;
            font-size: 11px;
        }
        QLineEdit#SeventvCosmeticsSearch:focus {
            border-color: #9146ff;
        }
        QScrollArea#SeventvCosmeticsScrollArea {
            background: transparent;
            border: 0;
        }
        QWidget#SeventvCosmeticsContent {
            background: transparent;
        }
    )")
                            .arg(bg, text, border, muted, inputBg,
                                 focusedBorder));
}

}  // namespace chatterino
