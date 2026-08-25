// SPDX-License-Identifier: MIT

#include "limerino/LimerinoWikiWidget.hpp"

#include "limerino/LimerinoFeatures.hpp"
#include "singletons/Theme.hpp"

#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace {

QString accessPathText(const FeatureAccess &access)
{
    switch (access.kind)
    {
        case AccessKind::Command:
            return {};  // commands render as chips instead
        case AccessKind::ContextMenu:
        case AccessKind::Dialog:
        case AccessKind::ToolbarButton:
        case AccessKind::SettingsToggle:
        case AccessKind::Automatic:
            return access.detail;
    }
    return access.detail;
}

/// Theme-safe status color ("theme colours, never hardcoded ones").
QColor statusColor(FeatureStatus status)
{
    auto *theme = getTheme();
    switch (status)
    {
        case FeatureStatus::Implemented:
            return theme->accent;
        case FeatureStatus::Partial:
            return theme->messages.textColors.link;
        case FeatureStatus::Planned:
        case FeatureStatus::Unavailable:
        default:
            return theme->messages.textColors.chatPlaceholder;
    }
}

QString statusText(FeatureStatus status)
{
    switch (status)
    {
        case FeatureStatus::Implemented:
            return QStringLiteral("Implemented");
        case FeatureStatus::Partial:
            return QStringLiteral("Partial");
        case FeatureStatus::Planned:
            return QStringLiteral("Planned");
        case FeatureStatus::Unavailable:
            return QStringLiteral("Unavailable");
    }
    return {};
}

// One catalog entry: a clickable header (chevron + name + badge + requires),
// a second row of access paths (commands as monospace chips, other kinds as
// breadcrumbs), and a word-wrapped description shown only when expanded.
class FeatureRow final : public QFrame
{
public:
    FeatureRow(const LimerinoFeature &feature, bool expanded, QWidget *parent)
        : QFrame(parent)
        , baseName_(feature.name)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        auto *header = new QHBoxLayout;
        this->nameButton_ = new QPushButton(this);
        this->nameButton_->setFlat(true);
        this->nameButton_->setCursor(Qt::PointingHandCursor);
        this->nameButton_->setStyleSheet(
            QStringLiteral("QPushButton { text-align: left; font-weight: "
                           "bold; border: none; padding: 2px; }"));
        header->addWidget(this->nameButton_);

        header->addStretch(1);

        for (const QString &req : feature.requirements)
        {
            auto *reqLabel = new QLabel(req, this);
            QPalette p = reqLabel->palette();
            p.setColor(QPalette::WindowText,
                       getTheme()->messages.textColors.chatPlaceholder);
            reqLabel->setPalette(p);
            header->addWidget(reqLabel);
        }

        auto *badge = new QLabel(statusText(feature.status), this);
        QPalette bp = badge->palette();
        bp.setColor(QPalette::WindowText, statusColor(feature.status));
        badge->setPalette(bp);
        badge->setStyleSheet(QStringLiteral("font-weight: bold;"));
        header->addWidget(badge);
        root->addLayout(header);

        // Access paths: commands as monospace chips; non-command kinds joined
        // as readable breadcrumbs on the same row.
        auto *paths = new QHBoxLayout;
        const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        for (const QString &cmd : feature.commands)
        {
            auto *chip = new QLabel(cmd, this);
            chip->setFont(mono);
            chip->setFrameShape(QFrame::StyledPanel);
            chip->setFrameShadow(QFrame::Sunken);
            chip->setMargin(1);
            paths->addWidget(chip);
        }
        QStringList breadcrumbs;
        for (const auto &a : feature.access)
        {
            const QString t = accessPathText(a);
            if (!t.isEmpty())
            {
                breadcrumbs.append(t);
            }
        }
        if (!breadcrumbs.isEmpty())
        {
            auto *crumbLabel =
                new QLabel(breadcrumbs.join(QStringLiteral("  ·  ")), this);
            crumbLabel->setWordWrap(true);
            QPalette cp = crumbLabel->palette();
            cp.setColor(QPalette::WindowText,
                        getTheme()->messages.textColors.chatPlaceholder);
            crumbLabel->setPalette(cp);
            paths->addWidget(crumbLabel, 1);
        }
        else
        {
            paths->addStretch(1);
        }
        root->addLayout(paths);

        this->description_ = new QLabel(feature.description, this);
        this->description_->setWordWrap(true);
        root->addWidget(this->description_);

        this->setExpanded(expanded);
    }

    void setExpanded(bool expanded)
    {
        this->description_->setVisible(expanded);
        const int row = this->isExpanded_ ? 1 : 0;
        (void)row;
        const QString chevron = expanded ? QStringLiteral("▾ ")
                                         : QStringLiteral("▸ ");
        this->nameButton_->setText(chevron + this->baseName_);
        this->isExpanded_ = expanded;
    }

    QPushButton *nameButton_ = nullptr;
    QLabel *description_ = nullptr;
    QString baseName_;
    bool isExpanded_ = false;
};

}  // namespace

LimerinoWikiWidget::LimerinoWikiWidget(QWidget *parent)
    : BaseWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    this->filterEdit_ = new QLineEdit(this);
    this->filterEdit_->setPlaceholderText(QStringLiteral("filter features..."));
    root->addWidget(this->filterEdit_);

    this->listLayout_ = new QVBoxLayout;
    this->listLayout_->setContentsMargins(0, 0, 0, 0);
    root->addLayout(this->listLayout_);
    root->addStretch(1);

    QObject::connect(this->filterEdit_, &QLineEdit::textChanged, this,
                     [this] { this->rebuild(); });

    this->rebuild();
}

void LimerinoWikiWidget::rebuild()
{
    while (QLayoutItem *item = this->listLayout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    const QString query = this->filterEdit_->text().trimmed();
    auto matches = [&](const LimerinoFeature &f) {
        if (query.isEmpty())
        {
            return true;
        }
        return f.name.contains(query, Qt::CaseInsensitive) ||
               f.summary.contains(query, Qt::CaseInsensitive) ||
               f.description.contains(query, Qt::CaseInsensitive) ||
               f.category.contains(query, Qt::CaseInsensitive) ||
               f.commands.join(QStringLiteral(" "))
                   .contains(query, Qt::CaseInsensitive);
    };

    QString lastCategory;
    for (const auto &f : limerinoFeatures())
    {
        // The wiki shows Implemented entries only (decision: honest badges for
        // other states stay reserved for when they are wanted).
        if (f.status != FeatureStatus::Implemented)
        {
            continue;
        }
        if (!matches(f))
        {
            continue;
        }

        if (f.category != lastCategory)
        {
            lastCategory = f.category;
            auto *catLabel = new QLabel(f.category, this);
            catLabel->setStyleSheet(
                QStringLiteral("font-weight: bold; border-bottom: 1px solid "
                               "%1; padding-top: 6px;")
                    .arg(getTheme()->messages.textColors.chatPlaceholder.name()));
            this->listLayout_->addWidget(catLabel);
        }

        auto *row = new FeatureRow(f, this->expandedIds_.contains(f.id), this);
        QObject::connect(row->nameButton_, &QPushButton::clicked, this,
                         [this, row, id = f.id] {
                             const bool nowExpanded = !row->isExpanded_;
                             row->setExpanded(nowExpanded);
                             if (nowExpanded)
                             {
                                 this->expandedIds_.insert(id);
                             }
                             else
                             {
                                 this->expandedIds_.remove(id);
                             }
                         });
        this->listLayout_->addWidget(row);
    }
    this->listLayout_->addStretch(1);
}

}  // namespace chatterino::limerino
