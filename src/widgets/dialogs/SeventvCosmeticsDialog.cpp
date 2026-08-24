// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/SeventvCosmeticsDialog.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "messages/layouts/MessageLayout.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/Paint.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"
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
#include "widgets/helper/MessageView.hpp"

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
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QVBoxLayout>

namespace chatterino {

namespace {

constexpr QSize DEFAULT_DIALOG_SIZE(340, 460);
constexpr int COSMETICS_SPACING = 6;
constexpr char SEVENTV_SELECTED_COLOR[] = "#9146ff";

int scaledMetric(float scale, int base, int minimum)
{
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
    // Normalize base64url to base64
    payload.replace('-', '+').replace('_', '/');
    while (payload.size() % 4 != 0)
    {
        payload.append('=');
    }

    const auto json = QByteArray::fromBase64(payload);
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return {};
    }

    return doc.object().value("sub").toString().trimmed();
}

QColor rgbaToQColor(const uint32_t color)
{
    auto red = (int)((color >> 24) & 0xFF);
    auto green = (int)((color >> 16) & 0xFF);
    auto blue = (int)((color >> 8) & 0xFF);
    auto alpha = (int)(color & 0xFF);

    return {red, green, blue, alpha};
}

std::shared_ptr<Paint> parsePaintObject(const QJsonObject &obj)
{
    const auto id = obj.value("id").toString();
    const auto name = obj.value("name").toString();
    const auto fn = obj.value("function").toString();

    if (fn == "linear-gradient")
    {
        const auto angle = obj.value("angle").toInt();
        const auto repeat = obj.value("repeat").toBool();
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
                pos = lastStop + 0.00001;
            }
            lastStop = pos;
            stops.append(QGradientStop(pos, rgbaToQColor(rgba)));
        }

        std::vector<PaintDropShadow> shadows;
        const auto shadowsArray = obj.value("drop_shadows").toArray();
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

        return std::make_shared<LinearGradientPaint>(
            name, id, std::nullopt, stops, repeat, angle, std::move(shadows));
    }
    else if (fn == "radial-gradient")
    {
        const auto shape = obj.value("shape").toString();
        const auto repeat = obj.value("repeat").toBool();
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
                pos = lastStop + 0.00001;
            }
            lastStop = pos;
            stops.append(QGradientStop(pos, rgbaToQColor(rgba)));
        }

        std::vector<PaintDropShadow> shadows;
        const auto shadowsArray = obj.value("drop_shadows").toArray();
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

        return std::make_shared<RadialGradientPaint>(name, id, stops, repeat,
                                                     std::move(shadows));
    }

    return nullptr;
}

}  // namespace

SeventvCosmeticsDialog::SeventvCosmeticsDialog(TwitchChannel *channel,
                                               QWidget *parent)
    : DraggablePopup(true, parent)
    , channel_(channel)
{
    this->setMinimumSize(DEFAULT_DIALOG_SIZE);
    this->resize(DEFAULT_DIALOG_SIZE);
    this->setWindowTitle(QStringLiteral("7TV Cosmetics"));

    auto *container = this->getLayoutContainer();
    container->setObjectName("SeventvCosmeticsRoot");
    this->mainLayout_ = new QVBoxLayout(container);
    this->mainLayout_->setContentsMargins(0, 0, 0, 0);
    this->mainLayout_->setSpacing(0);

    // Header
    this->headerWidget_ = new QWidget(container);
    this->headerWidget_->setObjectName("SeventvCosmeticsHeader");
    auto *headerLayout = new QHBoxLayout(this->headerWidget_);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(scaledMetric(this->scale(), 6, 3));

    this->headerTitleLabel_ =
        new QLabel(QStringLiteral("7TV Cosmetics"), this->headerWidget_);
    this->headerTitleLabel_->setObjectName("SeventvCosmeticsTitle");
    headerLayout->addWidget(this->headerTitleLabel_);

    this->paintsTabButton_ =
        new QPushButton(QStringLiteral("Paints"), this->headerWidget_);
    this->badgesTabButton_ =
        new QPushButton(QStringLiteral("Badges"), this->headerWidget_);
    this->paintsTabButton_->setCheckable(true);
    this->badgesTabButton_->setCheckable(true);
    this->paintsTabButton_->setChecked(true);

    QObject::connect(this->paintsTabButton_, &QPushButton::clicked, this,
                     [this] {
                         this->switchView(View::Paints);
                     });
    QObject::connect(this->badgesTabButton_, &QPushButton::clicked, this,
                     [this] {
                         this->switchView(View::Badges);
                     });

    headerLayout->addWidget(this->paintsTabButton_);
    headerLayout->addWidget(this->badgesTabButton_);
    headerLayout->addStretch(1);

    auto *pinButton = this->createPinButton();
    headerLayout->addWidget(pinButton);
    auto *closeBtn = new SvgButton(
        {.dark = ":/buttons/cancel.svg", .light = ":/buttons/cancelDark.svg"},
        this, QSize{3, 3});
    closeBtn->setScaleIndependentSize(18, 18);
    closeBtn->setToolTip(QStringLiteral("Close"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QObject::connect(closeBtn, &Button::leftClicked, this, &QWidget::close);
    headerLayout->addWidget(closeBtn);

    this->mainLayout_->addWidget(this->headerWidget_);

    // Preview view
    this->previewView_ = new MessageView(container);
    this->previewView_->setObjectName("SeventvCosmeticsPreview");
    this->previewView_->setFixedHeight(scaledMetric(this->scale(), 36, 28));
    this->previewView_->setCursor(QCursor(Qt::ArrowCursor));
    this->mainLayout_->addWidget(this->previewView_);

    // Search bar
    this->searchRowWidget_ = new QWidget(container);
    auto *searchLayout = new QHBoxLayout(this->searchRowWidget_);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    this->searchInput_ = new QLineEdit(this->searchRowWidget_);
    this->searchInput_->setObjectName("SeventvCosmeticsSearch");
    this->searchInput_->setPlaceholderText(QStringLiteral("Search paints..."));
    this->searchInput_->setClearButtonEnabled(true);
    QObject::connect(this->searchInput_, &QLineEdit::textChanged, this,
                     [this](const QString &text) {
                         this->searchQuery_ = text;
                         this->rebuildContent();
                     });
    searchLayout->addWidget(this->searchInput_);
    this->mainLayout_->addWidget(this->searchRowWidget_);

    // Scroll Area & Content
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
    this->contentLayout_->setSpacing(COSMETICS_SPACING);
    this->scrollArea_->setWidget(this->contentWidget_);
    this->mainLayout_->addWidget(this->scrollArea_, 1);

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
    const int minW = std::max(280, int(DEFAULT_DIALOG_SIZE.width() * 0.7));
    const int minH = std::max(300, int(DEFAULT_DIALOG_SIZE.height() * 0.7));
    this->setMinimumSize(minW, minH);
}

void SeventvCosmeticsDialog::switchView(View view)
{
    if (this->currentView_ == view)
    {
        return;
    }

    this->currentView_ = view;
    this->paintsTabButton_->setChecked(view == View::Paints);
    this->badgesTabButton_->setChecked(view == View::Badges);
    this->searchInput_->setPlaceholderText(
        view == View::Paints ? QStringLiteral("Search paints...")
                             : QStringLiteral("Search badges..."));
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
    auto userId = this->getSeventvUserId();

    const auto currentTwitchUser = getApp()->getAccounts()->twitch.getCurrent();
    const auto twitchUserId =
        currentTwitchUser ? currentTwitchUser->getUserId() : QString();

    if (userId.isEmpty() && twitchUserId.isEmpty())
    {
        this->setStatus(
            QStringLiteral(
                "Please sign in or link your 7TV account in Settings."),
            true);
        return;
    }

    this->loading_ = true;
    this->setStatus(QStringLiteral("Loading 7TV cosmetics..."));

    QPointer<SeventvCosmeticsDialog> self = this;

    const auto urlStr =
        !userId.isEmpty()
            ? QStringLiteral("https://7tv.io/v3/users/%1").arg(userId)
            : QStringLiteral("https://7tv.io/v3/users/twitch/%1")
                  .arg(twitchUserId);

    auto req = NetworkRequest(QUrl(urlStr), NetworkRequestType::Get);
    if (!token.isEmpty())
    {
        req = std::move(req).header(
            "Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    }

    std::move(req)
        .onSuccess([self](const auto &result) {
            if (!self)
            {
                return;
            }
            self->loading_ = false;
            self->loaded_ = true;

            const auto doc = result.parseJson();
            if (!doc.isObject())
            {
                self->setStatus(
                    QStringLiteral("Failed to parse 7TV user data."), true);
                return;
            }

            const auto obj = doc.object();
            self->seventvUserId_ = obj.value("id").toString();
            if (!self->seventvUserId_.isEmpty())
            {
                getSettings()->seventvUserId.setValue(self->seventvUserId_);
            }

            const auto styleObj = obj.value("style").toObject();
            self->activePaintId_ = styleObj.value("active_paint_id").toString();
            self->activeBadgeId_ = styleObj.value("active_badge_id").toString();

            self->paints_.clear();
            const auto paintsArr = obj.value("paints").toArray();
            for (const auto &p : paintsArr)
            {
                const auto pobj = p.toObject();
                SeventvPaintItem item;
                item.id = pobj.value("id").toString();
                item.name = pobj.value("name").toString();
                item.rawJson = pobj;
                item.paint = parsePaintObject(pobj);
                self->paints_.push_back(std::move(item));
            }

            self->badges_.clear();
            const auto badgesArr = obj.value("badges").toArray();
            for (const auto &b : badgesArr)
            {
                const auto bobj = b.toObject();
                SeventvBadgeItem item;
                item.id = bobj.value("id").toString();
                item.name = bobj.value("name").toString();
                item.description = bobj.value("description").toString();
                if (item.description.isEmpty())
                {
                    item.description = bobj.value("tooltip").toString();
                }
                const auto urlsArr = bobj.value("urls").toArray();
                if (!urlsArr.isEmpty())
                {
                    item.imageUrl = urlsArr.last().toArray().last().toString();
                }
                self->badges_.push_back(std::move(item));
            }

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
            self->setStatus(QStringLiteral("Error loading 7TV cosmetics: %1")
                                .arg(result.formatError()),
                            true);
        })
        .execute();
}

void SeventvCosmeticsDialog::selectPaint(const QString &paintId)
{
    const auto token = this->getSeventvToken();
    const auto userId = this->getSeventvUserId();
    if (token.isEmpty() || userId.isEmpty())
    {
        this->setStatus(
            QStringLiteral(
                "Please set your 7TV token in Settings -> Manage Accounts."),
            true);
        return;
    }

    this->setStatus(QStringLiteral("Setting active paint..."));

    QJsonObject vars;
    vars["id"] = userId;
    if (!paintId.isEmpty() && paintId != "none")
    {
        vars["paintId"] = paintId;
    }
    else
    {
        vars["paintId"] = QJsonValue(QJsonValue::Null);
    }

    QJsonObject root;
    root["query"] =
        QStringLiteral("mutation SetActivePaint($id: Id!, $paintId: Id) { "
                       "  users { "
                       "    user(id: $id) { "
                       "      activePaint(paintId: $paintId) { id } "
                       "    } "
                       "  } "
                       "}");
    root["variables"] = vars;

    QPointer<SeventvCosmeticsDialog> self = this;
    NetworkRequest(QUrl("https://api.7tv.app/v4/gql"), NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(token).toUtf8())
        .json(root)
        .onSuccess([self, paintId](const auto & /*res*/) {
            if (!self)
            {
                return;
            }
            self->activePaintId_ = (paintId == "none" ? QString() : paintId);
            self->setStatus({});
            self->rebuildContent();
            self->updatePreview();
            self->sendPresence();
        })
        .onError([self](const auto &res) {
            if (!self)
            {
                return;
            }
            self->setStatus(QStringLiteral("Failed to set paint: %1")
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
                "Please set your 7TV token in Settings -> Manage Accounts."),
            true);
        return;
    }

    this->setStatus(QStringLiteral("Setting active badge..."));

    QJsonObject vars;
    vars["id"] = userId;
    if (!badgeId.isEmpty() && badgeId != "none")
    {
        vars["badgeId"] = badgeId;
    }
    else
    {
        vars["badgeId"] = QJsonValue(QJsonValue::Null);
    }

    QJsonObject root;
    root["query"] =
        QStringLiteral("mutation SetActiveBadge($id: Id!, $badgeId: Id) { "
                       "  users { "
                       "    user(id: $id) { "
                       "      activeBadge(badgeId: $badgeId) { id } "
                       "    } "
                       "  } "
                       "}");
    root["variables"] = vars;

    QPointer<SeventvCosmeticsDialog> self = this;
    NetworkRequest(QUrl("https://api.7tv.app/v4/gql"), NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(token).toUtf8())
        .json(root)
        .onSuccess([self, badgeId](const auto & /*res*/) {
            if (!self)
            {
                return;
            }
            self->activeBadgeId_ = (badgeId == "none" ? QString() : badgeId);
            self->setStatus({});
            self->rebuildContent();
            self->updatePreview();
            self->sendPresence();
        })
        .onError([self](const auto &res) {
            if (!self)
            {
                return;
            }
            self->setStatus(QStringLiteral("Failed to set badge: %1")
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

    if (this->currentView_ == View::Paints)
    {
        this->rebuildPaints();
    }
    else
    {
        this->rebuildBadges();
    }

    this->contentLayout_->addStretch(1);
}

void SeventvCosmeticsDialog::rebuildPaints()
{
    // 1. None Option
    auto *noneButton =
        new QPushButton(QStringLiteral("None (Default)"), this->contentWidget_);
    noneButton->setObjectName("SeventvCard");
    noneButton->setCheckable(true);
    noneButton->setChecked(this->activePaintId_.isEmpty());
    QObject::connect(noneButton, &QPushButton::clicked, this, [this] {
        this->selectPaint("none");
    });
    this->contentLayout_->addWidget(noneButton);

    const auto needle = this->searchQuery_.trimmed();
    int count = 0;

    for (const auto &p : this->paints_)
    {
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
        {
            continue;
        }

        auto *btn = new QPushButton(p.name, this->contentWidget_);
        btn->setObjectName("SeventvCard");
        btn->setCheckable(true);
        btn->setChecked(p.id == this->activePaintId_);
        const auto pid = p.id;
        QObject::connect(btn, &QPushButton::clicked, this, [this, pid] {
            this->selectPaint(pid);
        });
        this->contentLayout_->addWidget(btn);
        count++;
    }

    if (count == 0 && this->paints_.empty() && this->loaded_)
    {
        auto *emptyLabel =
            new QLabel(QStringLiteral("No 7TV paints found for this account."),
                       this->contentWidget_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        this->contentLayout_->addWidget(emptyLabel);
    }
}

void SeventvCosmeticsDialog::rebuildBadges()
{
    // 1. None Option
    auto *noneButton =
        new QPushButton(QStringLiteral("None (Default)"), this->contentWidget_);
    noneButton->setObjectName("SeventvCard");
    noneButton->setCheckable(true);
    noneButton->setChecked(this->activeBadgeId_.isEmpty());
    QObject::connect(noneButton, &QPushButton::clicked, this, [this] {
        this->selectBadge("none");
    });
    this->contentLayout_->addWidget(noneButton);

    const auto needle = this->searchQuery_.trimmed();
    int count = 0;

    for (const auto &b : this->badges_)
    {
        if (!needle.isEmpty() &&
            !b.name.contains(needle, Qt::CaseInsensitive) &&
            !b.description.contains(needle, Qt::CaseInsensitive))
        {
            continue;
        }

        auto *btn = new QPushButton(b.name, this->contentWidget_);
        btn->setObjectName("SeventvCard");
        btn->setCheckable(true);
        btn->setChecked(b.id == this->activeBadgeId_);
        if (!b.description.isEmpty())
        {
            btn->setToolTip(b.description);
        }
        const auto bid = b.id;
        QObject::connect(btn, &QPushButton::clicked, this, [this, bid] {
            this->selectBadge(bid);
        });
        this->contentLayout_->addWidget(btn);
        count++;
    }

    if (count == 0 && this->badges_.empty() && this->loaded_)
    {
        auto *emptyLabel =
            new QLabel(QStringLiteral("No 7TV badges found for this account."),
                       this->contentWidget_);
        emptyLabel->setAlignment(Qt::AlignCenter);
        this->contentLayout_->addWidget(emptyLabel);
    }
}

MessagePtr SeventvCosmeticsDialog::buildPreviewMessage() const
{
    MessageBuilder builder;
    const auto currentTwitchUser = getApp()->getAccounts()->twitch.getCurrent();
    const auto userName = currentTwitchUser ? currentTwitchUser->getUserName()
                                            : QStringLiteral("username");

    builder.emplace<TextElement>(
        QStringLiteral("Preview: "), MessageElementFlag::None,
        MessageColor(MessageColor::System), FontStyle::ChatMedium);

    auto color =
        currentTwitchUser ? currentTwitchUser->color() : QColor("#bf94ff");
    builder.emplace<TextElement>(userName, MessageElementFlag::Username,
                                 MessageColor(color),
                                 FontStyle::ChatMediumBold);

    return builder.release();
}

void SeventvCosmeticsDialog::updatePreview()
{
    if (this->previewView_ != nullptr)
    {
        this->previewView_->setMessage(this->buildPreviewMessage());
    }
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
    const int vMargin = std::max(3, int(6 * rawScale));
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
    const auto cardBg = theme->isLightTheme() ? QStringLiteral("#f7f7f8")
                                              : QStringLiteral("#18181b");
    const auto cardBorder = theme->isLightTheme() ? QStringLiteral("#e5e5e9")
                                                  : QStringLiteral("#303036");
    const auto cardHoverBg = theme->isLightTheme() ? QStringLiteral("#ebebef")
                                                   : QStringLiteral("#26262c");

    this->setStyleSheet(QStringLiteral(R"(
        QWidget#SeventvCosmeticsRoot {
            background: %1;
            color: %2;
        }
        QLabel#SeventvCosmeticsTitle {
            color: %2;
            font-weight: 700;
        }
        QLabel#SeventvCosmeticsStatus {
            color: %4;
            padding: 8px;
        }
        QLineEdit#SeventvCosmeticsSearch {
            background: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QPushButton#SeventvCard {
            background: %7;
            color: %2;
            border: 1px solid %8;
            border-radius: 4px;
            padding: 8px 12px;
            text-align: left;
            font-weight: 600;
        }
        QPushButton#SeventvCard:hover {
            background: %9;
            border-color: %6;
        }
        QPushButton#SeventvCard:checked {
            background: %9;
            border: 2px solid %6;
            color: %6;
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
                                 focusedBorder, cardBg, cardBorder,
                                 cardHoverBg));
}

}  // namespace chatterino
