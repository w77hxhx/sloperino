// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoUserCardWidget.hpp"

#include "providers/limerino/gql/LimerinoUserCardExtras.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/Label.hpp"

#include <QHBoxLayout>
#include <QPointer>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

// Match the neighbour (ui_.userIDLabel) exactly: dim-grey WindowText.
constexpr const char *DIM_TEXT_COLOR = "#aaa";

}  // namespace

LimerinoUserCardWidget::LimerinoUserCardWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto box = LayoutCreator<LimerinoUserCardWidget>(this)
                   .setLayoutType<QVBoxLayout>()
                   .withoutMargin();
    box->setSpacing(0);

    // Tag on the first row; team directly underneath; both this column's
    // right edge stays flush with the name/vbox column via the parent box.
    box.emplace<Label>(QString(), FontStyle::UiMedium)
        .assign(&this->languageTagLabel_);
    QPalette tagPalette;
    tagPalette.setColor(QPalette::WindowText,
                        QColor(QString::fromLatin1(DIM_TEXT_COLOR)));
    this->languageTagLabel_->setPalette(tagPalette);

    // G3: primary team - same right-aligned column, one row under the tag.
    box.emplace<Label>(QString(), FontStyle::UiMedium)
        .assign(&this->teamLabel_);
    QPalette teamPalette;
    teamPalette.setColor(QPalette::WindowText,
                         QColor(QString::fromLatin1(DIM_TEXT_COLOR)));
    this->teamLabel_->setPalette(teamPalette);

    // G4: no own label - the detail rides on the popup's existing subageLabel

    this->rebuild();
}

void LimerinoUserCardWidget::setTarget(const QString &userId,
                                       const QString &channelId,
                                       const QString &loginForSubRow)
{
    const bool changed = (userId != this->userId_) ||
                         (channelId != this->channelId_);

    this->userId_ = userId;
    this->channelId_ = channelId;
    this->loginForSubRow_ = loginForSubRow;

    // Any in-flight reply for a *previous* target is silently discarded via
    // the generation check (rule 8). No abort plumbing is needed - the
    // network layer outlives this widget harmlessly.
    ++this->requestGeneration_;

    if (!changed && !this->userId_.isEmpty())
    {
        return;  // same target - keep the current result/cache hit
    }

    if (userId.isEmpty() || userId.startsWith(QStringLiteral("kick:")))
    {
        this->extras_ = {};
        this->rebuild();
        return;
    }

    this->refetch();
}

void LimerinoUserCardWidget::refetch()
{
    const quint64 generation = this->requestGeneration_;
    QPointer<LimerinoUserCardWidget> self(this);
    const QString userId = this->userId_;
    const QString channelId = this->channelId_;

    this->extras_ = {};
    this->rebuild();

    gql::fetchUserCardExtras(
        userId, channelId,
        [self, generation](std::optional<gql::LimerinoUserCardExtras> extras) {
            if (!self || generation != self->requestGeneration_ ||
                !extras.has_value())
            {
                return;
            }
            self->extras_ = std::move(*extras);
            self->rebuild();
            Q_EMIT self->extrasChanged();
        });
}

void LimerinoUserCardWidget::rebuild()
{
    // Ruling: bare tag exactly as the API returns it; hidden (zero footprint)
    // when absent. Field-level failure is a dim placeholder + tooltip so it is
    // distinguishable from "no preferred language" without an error chip.
    //
    // Do not use QWidget::isVisible() for showLang. After refetch() clears
    // extras_, this widget is hidden; a child's isVisible() stays false while
    // the parent is hidden, so a language-only result never re-showed the
    // widget. Team already keyed off the string (and worked); language did
    // not — hence the tag only appeared when a team also forced show.
    const bool showLang =
        this->extras_.settingsFailed ||
        !this->extras_.preferredLanguageTag.isEmpty();
    if (this->extras_.settingsFailed)
    {
        this->languageTagLabel_->setText(QStringLiteral("—"));
        this->languageTagLabel_->setToolTip(
            QStringLiteral("Could not load preferred language"));
    }
    else
    {
        this->languageTagLabel_->setText(this->extras_.preferredLanguageTag);
        this->languageTagLabel_->setToolTip(QString());
    }
    this->languageTagLabel_->setVisible(showLang);

    // Ruling 1: show the team's `name` only; nothing rendered (and no layout
    // space claimed) when the user is on no team or the field was null.
    this->teamLabel_->setText(this->extras_.primaryTeamName);
    const bool showTeam = !this->extras_.primaryTeamName.isEmpty();
    this->teamLabel_->setVisible(showTeam);

    // G4: subscription detail is appended by the caller to its own sub-age row.
    this->setVisible(showLang || showTeam);
    this->layout()->invalidate();
}

QString LimerinoUserCardWidget::subscriptionSuffix() const
{
    const auto &sub = this->extras_.subscription;
    if (!sub.has_value())
    {
        return {};
    }

    QStringList parts;

    // Item 5: known wire values get readable strings; any OTHER non-empty
    // platform string still renders (as its raw wire value) rather than being
    // silently dropped. A null platform simply renders nothing.
    const QString &platform = sub->platform;
    if (platform == QLatin1String("web"))
    {
        parts << QStringLiteral("web");
    }
    else if (platform == QLatin1String("android"))
    {
        parts << QStringLiteral("Android");
    }
    else if (platform == QLatin1String("ios"))
    {
        parts << QStringLiteral("iOS");
    }
    else if (platform == QLatin1String("prime") || sub->purchasedWithPrime)
    {
        parts << QStringLiteral("Prime");
    }
    else if (!platform.isEmpty())
    {
        parts << platform;
    }

    if (sub->isGift)
    {
        if (!sub->gifterDisplayName.isEmpty())
        {
            parts << QStringLiteral("gift from %1").arg(sub->gifterDisplayName);
        }
        else
        {
            parts << QStringLiteral("gift");
        }
    }
    if (sub->tier == QLatin1String("2000"))
    {
        parts << QStringLiteral("Tier 2");
    }
    else if (sub->tier == QLatin1String("3000"))
    {
        parts << QStringLiteral("Tier 3");
    }
    // "1000" / unknown -> existing sub-age row already shows tier; no duplicate

    if (!sub->thirdPartySKU.isEmpty())
    {
        parts << sub->thirdPartySKU;
    }

    if (parts.isEmpty())
    {
        return {};
    }
    return QStringLiteral(" · ") + parts.join(QStringLiteral(", "));
}

}  // namespace chatterino::limerino
