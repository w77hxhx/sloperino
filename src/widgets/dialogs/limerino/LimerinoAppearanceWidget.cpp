// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoAppearanceWidget.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/limerino/appearance/LimerinoChatColor.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchBadges.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/DisplayBadge.hpp"
#include "util/Twitch.hpp"
#include "widgets/dialogs/limerino/LimerinoColorField.hpp"
#include "widgets/helper/color/ColorButton.hpp"
#include "widgets/splits/Split.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

void clearLayout(QLayout *layout)
{
    if (layout == nullptr)
    {
        return;
    }
    while (auto *item = layout->takeAt(0))
    {
        if (auto *w = item->widget())
        {
            w->deleteLater();
        }
        delete item;
    }
}

}  // namespace

LimerinoAppearanceWidget::LimerinoAppearanceWidget(Split *split)
    : BaseWidget(split)
    , split_(split)
{
    auto *root = new QVBoxLayout(this);

    // ---- badge ----
    auto *badgeBox = new QGroupBox(QStringLiteral("Badge"), this);
    auto *badgeForm = new QFormLayout(badgeBox);

    this->badgeCombo_ = new QComboBox(badgeBox);
    badgeForm->addRow(QStringLiteral("Badge"), this->badgeCombo_);

    this->scopeCombo_ = new QComboBox(badgeBox);
    this->scopeCombo_->addItem(QStringLiteral("Global (all channels)"));
    this->scopeCombo_->addItem(QStringLiteral("This channel (not available)"));
    this->scopeCombo_->setItemData(
        1, QStringLiteral("Twitch doesn't offer a per-channel badge API"),
        Qt::ToolTipRole);
    this->scopeCombo_->model()->setData(
        this->scopeCombo_->model()->index(1, 0), QVariant(false),
        Qt::UserRole - 1);  // disable the channel row
    badgeForm->addRow(QStringLiteral("Apply to"), this->scopeCombo_);

    this->previewLabel_ = new QLabel(badgeBox);
    badgeForm->addRow(QStringLiteral("Preview"), this->previewLabel_);

    this->applyBadgeButton_ =
        new QPushButton(QStringLiteral("Apply badge"), badgeBox);
    badgeForm->addRow(this->applyBadgeButton_);
    root->addWidget(badgeBox);

    // ---- chat color ----
    auto *colorBox = new QGroupBox(QStringLiteral("Chat color"), this);
    auto *colorLayout = new QVBoxLayout(colorBox);

    colorLayout->addWidget(new QLabel(QStringLiteral("Twitch named colours")));
    this->namedSwatchRow_ = new QWidget(colorBox);
    auto *namedGrid = new QGridLayout(this->namedSwatchRow_);
    namedGrid->setContentsMargins(0, 0, 0, 0);
    namedGrid->setSpacing(4);
    int namedIndex = 0;
    for (const auto &name : VALID_HELIX_COLORS)
    {
        const QColor color = parseChatColor(name);
        auto *btn = new ColorButton(color, this->namedSwatchRow_);
        btn->setFixedSize(36, 28);
        btn->setMinimumSize(36, 28);
        btn->setToolTip(name);
        QObject::connect(btn, &QAbstractButton::clicked, this,
                         [this, name] {
                             this->selectColorValue(name, false);
                         });
        namedGrid->addWidget(btn, namedIndex / 5, namedIndex % 5);
        ++namedIndex;
    }
    colorLayout->addWidget(this->namedSwatchRow_);

    colorLayout->addWidget(new QLabel(QStringLiteral("Recent custom colours")));
    this->recentSwatchRow_ = new QWidget(colorBox);
    auto *recentLayout = new QHBoxLayout(this->recentSwatchRow_);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(4);
    colorLayout->addWidget(this->recentSwatchRow_);

    colorLayout->addWidget(
        new QLabel(QStringLiteral("Custom (Turbo/Prime hex)")));
    this->customColorField_ = new LimerinoColorField(colorBox);
    colorLayout->addWidget(this->customColorField_);
    QObject::connect(this->customColorField_, &LimerinoColorField::colorChanged,
                     this, [this](QColor color) {
                         if (!color.isValid())
                         {
                             return;
                         }
                         this->selectColorValue(chatColorHexForDisplay(color),
                                                true);
                     });

    this->colorPreviewLabel_ = new QLabel(colorBox);
    colorLayout->addWidget(this->colorPreviewLabel_);

    this->applyColorButton_ =
        new QPushButton(QStringLiteral("Apply color"), colorBox);
    colorLayout->addWidget(this->applyColorButton_);
    root->addWidget(colorBox);

    this->statusLabel_ = new QLabel(this);
    this->statusLabel_->setWordWrap(true);
    root->addWidget(this->statusLabel_);
    root->addStretch(1);

    QObject::connect(this->badgeCombo_,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, [this] { this->refreshPreview(); });
    QObject::connect(this->applyBadgeButton_, &QPushButton::clicked, this,
                     [this] { this->applyBadge(); });
    QObject::connect(this->applyColorButton_, &QPushButton::clicked, this,
                     [this] { this->applyColor(); });

    const QString last = loadLastChatColor();
    if (!last.isEmpty())
    {
        this->selectColorValue(last, isHelixNamedColor(last) ? false : true);
    }
    else
    {
        this->selectColorValue(VALID_HELIX_COLORS.constFirst(), false);
    }
    this->rebuildRecentSwatches();

    this->refreshBadges();
    this->refreshPreview();
}

void LimerinoAppearanceWidget::rebuildRecentSwatches()
{
    auto *layout =
        qobject_cast<QHBoxLayout *>(this->recentSwatchRow_->layout());
    if (layout == nullptr)
    {
        return;
    }
    clearLayout(layout);

    const auto recents = loadChatColorRecents();
    for (const auto &value : recents)
    {
        const QColor color = parseChatColor(value);
        if (!color.isValid())
        {
            continue;
        }
        auto *btn = new ColorButton(color, this->recentSwatchRow_);
        btn->setFixedSize(36, 28);
        btn->setMinimumSize(36, 28);
        btn->setToolTip(value);
        QObject::connect(btn, &QAbstractButton::clicked, this,
                         [this, value] {
                             this->selectColorValue(value, true);
                         });
        layout->addWidget(btn);
    }
    layout->addStretch(1);
}

void LimerinoAppearanceWidget::selectColorValue(const QString &value,
                                                bool fromCustomField)
{
    const QString normalized = normalizeChatColorValue(value);
    if (normalized.isEmpty())
    {
        return;
    }
    this->selectedColorValue_ = normalized;
    saveLastChatColor(normalized);

    const QColor color = parseChatColor(normalized);
    if (color.isValid() && !fromCustomField)
    {
        this->customColorField_->setColor(color);
    }
    else if (color.isValid() && fromCustomField)
    {
        // Keep hex field in sync when picking a recent custom swatch.
        this->customColorField_->setColor(color);
    }

    this->refreshPreview();
}

void LimerinoAppearanceWidget::refreshBadges()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr)
    {
        this->statusLabel_->setText(QStringLiteral("not a twitch channel"));
        return;
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->statusLabel_->setText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("pick your badges"))
                          : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_CHAT_SETTINGS_BADGES,
        QJsonObject{{QStringLiteral("channelLogin"), tchan->getName()}},
        token.token,
        [g = QPointer<LimerinoAppearanceWidget>(this), this](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            this->badgeCombo_->clear();

            const QJsonArray badges =
                data[QStringLiteral("currentUser")].toObject()[
                    QStringLiteral("availableBadges")].toArray();
            QList<DisplayBadge> items;
            for (const QJsonValue v : badges)
            {
                const QString setId =
                    v.toObject()[QStringLiteral("setID")].toString();
                if (setId.isEmpty())
                {
                    continue;
                }
                items.append(DisplayBadge(setId, setId));
            }
            for (const DisplayBadge &item : items)
            {
                this->badgeCombo_->addItem(item.displayName(),
                                           item.badgeName());
            }
            getApp()->getTwitchBadges()->getBadgeIcons(
                items, [combo = QPointer<QComboBox>(this->badgeCombo_)](
                             QString identifier, const auto &icon) {
                    if (!combo)
                    {
                        return;
                    }
                    const int idx = combo->findData(identifier);
                    if (idx >= 0 && icon)
                    {
                        combo->setItemIcon(idx, *icon);
                    }
                });
            if (items.isEmpty())
            {
                this->statusLabel_->setText(
                    QStringLiteral("No badges available in this channel."));
            }
            this->refreshPreview();
        },
        [g = QPointer<LimerinoAppearanceWidget>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->statusLabel_->setText(e.message);
            }
        });
}

void LimerinoAppearanceWidget::refreshPreview()
{
    const auto self = getApp()->getAccounts()->twitch.getCurrent();
    const QString name =
        self && !self->isAnon() ? self->getUserName() : QStringLiteral("you");

    this->previewLabel_->setText(
        QStringLiteral("%1's messages will show the selected badge")
            .arg(name));

    const QColor color = parseChatColor(this->selectedColorValue_);
    if (color.isValid())
    {
        this->colorPreviewLabel_->setText(
            QStringLiteral(
                "<span style=\"color:%1\">%2: hello this is a preview</span>")
                .arg(color.name(QColor::HexRgb), name));
    }
    else
    {
        this->colorPreviewLabel_->setText(QStringLiteral("(invalid color)"));
    }
}

void LimerinoAppearanceWidget::applyBadge()
{
    const QString setId = this->badgeCombo_->currentData().toString();
    if (setId.isEmpty())
    {
        this->statusLabel_->setText(QStringLiteral("pick a badge first"));
        return;
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->statusLabel_->setText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("select your badges"))
                          : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_SELECT_GLOBAL_BADGE,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("badgeSetID"), setId},
                                 {QStringLiteral("badgeSetVersion"),
                                  QStringLiteral("1")}}}},
        token.token,
        [g = QPointer<LimerinoAppearanceWidget>(this)](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QString code =
                data[QStringLiteral("ChatSettings_SelectGlobalBadge")]
                    .toObject()[QStringLiteral("error")]
                    .toObject()[QStringLiteral("code")]
                    .toString();
            g->statusLabel_->setText(
                code.isEmpty()
                    ? QStringLiteral("Successfully selected global badge!")
                    : QStringLiteral("Unable to select global badge! Status: %1")
                          .arg(code));
        },
        [g = QPointer<LimerinoAppearanceWidget>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->statusLabel_->setText(
                    QStringLiteral("Unable to select global badge! Status: %1")
                        .arg(e.message));
            }
        });
}

void LimerinoAppearanceWidget::applyColor()
{
    auto self = getApp()->getAccounts()->twitch.getCurrent();
    if (!self || self->isAnon())
    {
        this->statusLabel_->setText(QStringLiteral(
            "You must be logged in (main account) to change your chat color."));
        return;
    }

    QString colorString = this->selectedColorValue_;
    if (colorString.isEmpty())
    {
        this->statusLabel_->setText(QStringLiteral("pick a colour first"));
        return;
    }
    cleanHelixColorName(colorString);

    const bool custom = !isHelixNamedColor(colorString);
    const QPointer<LimerinoAppearanceWidget> g(this);
    getHelix()->updateUserChatColor(
        self->getUserId(), colorString,
        [g, colorString, custom] {
            if (!g)
            {
                return;
            }
            if (custom)
            {
                auto recents = loadChatColorRecents();
                recents = pushChatColorRecent(std::move(recents), colorString);
                saveChatColorRecents(recents);
                g->rebuildRecentSwatches();
            }
            saveLastChatColor(colorString);
            g->statusLabel_->setText(
                QStringLiteral("Your color has been changed to %1.")
                    .arg(colorString));
        },
        [g, colorString](auto error, auto /*message*/) {
            if (!g)
            {
                return;
            }
            QString note;
            if (error == HelixUpdateUserChatColorError::UserMissingScope)
            {
                note = QStringLiteral(
                    " (missing scope - re-login with your main account)");
            }
            else if (error == HelixUpdateUserChatColorError::InvalidColor)
            {
                note = QStringLiteral(
                    " (invalid color - custom hex codes require Turbo/Prime)");
            }
            g->statusLabel_->setText(
                QStringLiteral("Failed to change color to %1%2.")
                    .arg(colorString, note));
        });
}

}  // namespace chatterino::limerino
