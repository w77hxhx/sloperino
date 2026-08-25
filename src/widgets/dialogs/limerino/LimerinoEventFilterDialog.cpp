// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoEventFilterDialog.hpp"

#include "limerino/PubSubEventsChannel.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace chatterino::limerino {

LimerinoEventFilterDialog::LimerinoEventFilterDialog(QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
{
    this->setWindowTitle(QStringLiteral("Filter events"));

    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        QStringLiteral(
            "Choose which event types appear in the %1 channel. "
            "Filters apply to newly arriving events.")
            .arg(pubSubEventsChannelName()),
        this));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    this->listLayout_ = new QVBoxLayout(inner);
    inner->setLayout(this->listLayout_);
    scroll->setWidget(inner);
    root->addWidget(scroll);

    this->resize(440, 380);
    this->rebuild();
}

void LimerinoEventFilterDialog::rebuild()
{
    const QStringList types = getPubSubController()->knownEventTypes();
    if (types.isEmpty())
    {
        this->listLayout_->addWidget(new QLabel(QStringLiteral(
            "No event types are known yet. They appear here after the "
            "first live event of that type (or when a topic batch registers "
            "them).")));
        return;
    }

    for (const QString &type : types)
    {
        auto *box = new QCheckBox(type, this);
        box->setChecked(!pubSubEventTypeHidden(type));
        QObject::connect(box, &QCheckBox::toggled, this,
                         [type](bool checked) {
                             setPubSubEventTypeHidden(type, !checked);
                         });
        this->listLayout_->addWidget(box);
    }
    this->listLayout_->addStretch();
}

}  // namespace chatterino::limerino
