// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoPredictionDialog.hpp"

#include "common/Channel.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "widgets/dialogs/limerino/LimerinoAppearanceWidget.hpp"
#include "widgets/dialogs/limerino/LimerinoResultList.hpp"
#include "widgets/splits/Split.hpp"

#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

constexpr int MAX_OPTIONS = 10;
constexpr int MAX_POLL_OPTIONS = 5;  // Twitch polls cap
constexpr int RESOLVED_HISTORY_COUNT = 10;

// Plugin's generateRandomHexString(32) equivalent for transaction IDs.
QString randomHex(int length)
{
    static const QString chars = QStringLiteral("0123456789abcdef");
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        out += chars[QRandomGenerator::global()->bounded(chars.size())];
    }
    return out;
}

QJsonArray readDrafts()
{
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(
        getSettings()->limerinoPredictionHistory.getValue().toUtf8(), &err);
    return doc.isArray() ? doc.array() : QJsonArray{};
}

void writeDrafts(const QJsonArray &arr)
{
    getSettings()->limerinoPredictionHistory.setValue(QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// Cap dialog size to the screen it opens on (DPI/multi-monitor safe).
void fitDialogToAvailableScreen(QWidget *dialog, int preferredWidth,
                                int preferredHeight)
{
    QScreen *screen = dialog->screen();
    if (screen == nullptr && dialog->parentWidget() != nullptr)
    {
        screen = dialog->parentWidget()->screen();
    }
    if (screen == nullptr)
    {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr)
    {
        dialog->resize(preferredWidth, preferredHeight);
        return;
    }
    const QRect avail = screen->availableGeometry();
    constexpr int margin = 48;
    const int maxW = qMax(320, avail.width() - margin);
    const int maxH = qMax(240, avail.height() - margin);
    dialog->setMaximumSize(maxW, maxH);
    dialog->resize(qMin(preferredWidth, maxW), qMin(preferredHeight, maxH));
}

}  // namespace

LimerinoPredictionDialog::LimerinoPredictionDialog(Split *split)
    : BasePopup({BaseWindow::Flags::Dialog}, split)
    , split_(split)
{
    this->setWindowTitle(QStringLiteral("Limerino Actions"));
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *pageRoot = new QHBoxLayout(this);
    auto *sidebar = new QListWidget(this);
    sidebar->addItem(QStringLiteral("Predictions"));
    sidebar->addItem(QStringLiteral("Rewards"));
    sidebar->addItem(QStringLiteral("Appearance"));
    sidebar->setFixedWidth(132);
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    pageRoot->addWidget(sidebar);
    auto *stacked = new QStackedWidget(this);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setWidget(stacked);
    pageRoot->addWidget(scroll, 1);
    QObject::connect(sidebar, &QListWidget::currentRowChanged, stacked,
                     &QStackedWidget::setCurrentIndex);
    sidebar->setCurrentRow(0);

    auto *predictionsPage = new QWidget;
    auto *root = new QVBoxLayout(predictionsPage);

    // ------------------------- Create (mod) -------------------------
    this->createBox_ = new QGroupBox(QStringLiteral("Create prediction"), this);
    auto *createLayout = new QVBoxLayout(this->createBox_);

    auto *form = new QFormLayout;
    this->titleEdit_ = new QLineEdit(this->createBox_);
    form->addRow(QStringLiteral("Title"), this->titleEdit_);
    this->windowSpin_ = new QSpinBox(this->createBox_);
    this->windowSpin_->setRange(30, 3600);
    this->windowSpin_->setValue(120);
    form->addRow(QStringLiteral("Window (seconds)"), this->windowSpin_);
    createLayout->addLayout(form);

    this->optionsLayout_ = new QVBoxLayout;
    createLayout->addLayout(this->optionsLayout_);

    auto *addRow = new QHBoxLayout;
    this->addOptionButton_ =
        new QPushButton(QStringLiteral("+ Add option"), this->createBox_);
    addRow->addWidget(this->addOptionButton_);
    addRow->addStretch(1);
    createLayout->addLayout(addRow);

    // addOptionRow() drives addOptionButton_->setEnabled(), so the button
    // must exist before the initial rows are added (F1 crash fix).
    this->addOptionRow();
    this->addOptionRow();

    this->createButton_ = new QPushButton(QStringLiteral("Create"),
                                          this->createBox_);
    createLayout->addWidget(this->createButton_);
    root->addWidget(this->createBox_);

    // ------------------------- Create poll (mod) -------------------------
    this->createPollBox_ = new QGroupBox(QStringLiteral("Create poll"), this);
    auto *pollLayout = new QVBoxLayout(this->createPollBox_);

    auto *pollForm = new QFormLayout;
    this->pollTitleEdit_ = new QLineEdit(this->createPollBox_);
    pollForm->addRow(QStringLiteral("Title"), this->pollTitleEdit_);
    this->pollDurationSpin_ = new QSpinBox(this->createPollBox_);
    this->pollDurationSpin_->setRange(60, 1800);  // Twitch poll limits
    this->pollDurationSpin_->setValue(300);
    pollForm->addRow(QStringLiteral("Duration (seconds)"),
                     this->pollDurationSpin_);
    pollLayout->addLayout(pollForm);

    this->pollOptionsLayout_ = new QVBoxLayout;
    pollLayout->addLayout(this->pollOptionsLayout_);

    auto *pollAddRow = new QHBoxLayout;
    this->addPollOptionButton_ =
        new QPushButton(QStringLiteral("+ Add option"), this->createPollBox_);
    pollAddRow->addWidget(this->addPollOptionButton_);
    pollAddRow->addStretch(1);
    pollLayout->addLayout(pollAddRow);

    // addPollOptionRow() drives addPollOptionButton_->setEnabled(), so the
    // button must exist before the initial rows are added (F1 crash fix).
    this->addPollOptionRow();
    this->addPollOptionRow();

    this->createPollButton_ =
        new QPushButton(QStringLiteral("Create poll"), this->createPollBox_);
    pollLayout->addWidget(this->createPollButton_);
    root->addWidget(this->createPollBox_);

    // ------------------- Draft history (mod) -------------------
    this->draftsBox_ =
        new QGroupBox(QStringLiteral("Last 5 created (drafts)"), this);
    auto *draftsLayout = new QVBoxLayout(this->draftsBox_);
    this->draftsCombo_ = new QComboBox(this->draftsBox_);
    draftsLayout->addWidget(this->draftsCombo_);
    this->draftsUseButton_ =
        new QPushButton(QStringLiteral("Use as template"), this->draftsBox_);
    draftsLayout->addWidget(this->draftsUseButton_);
    root->addWidget(this->draftsBox_);

    // --------------------- Active prediction (all) ---------------------
    auto *activeBox = new QGroupBox(QStringLiteral("Prediction"), this);
    auto *activeOuter = new QVBoxLayout(activeBox);
    this->activeLabel_ = new QLabel(activeBox);
    this->activeLabel_->setWordWrap(true);
    activeOuter->addWidget(this->activeLabel_);
    this->activeLayout_ = new QVBoxLayout;
    activeOuter->addLayout(this->activeLayout_);

    // makePrediction (everyone, when active & unlocked)
    auto *makeRow = new QHBoxLayout;
    makeRow->addWidget(new QLabel(QStringLiteral("Points:"), activeBox));
    this->pointsSpin_ = new QSpinBox(activeBox);
    this->pointsSpin_->setRange(1, 100000000);
    makeRow->addWidget(this->pointsSpin_);
    this->outcomeCombo_ = new QComboBox(activeBox);
    makeRow->addWidget(this->outcomeCombo_, 1);
    this->makeButton_ = new QPushButton(QStringLiteral("Make prediction"),
                                        activeBox);
    makeRow->addWidget(this->makeButton_);
    activeOuter->addLayout(makeRow);

    auto *manageRow = new QHBoxLayout;
    this->lockButton_ = new QPushButton(QStringLiteral("Lock"), activeBox);
    this->refundButton_ =
        new QPushButton(QStringLiteral("Cancel & refund"), activeBox);
    this->refreshButton_ = new QPushButton(QStringLiteral("Refresh"), activeBox);
    manageRow->addWidget(this->lockButton_);
    manageRow->addWidget(this->refundButton_);
    manageRow->addWidget(this->refreshButton_);
    manageRow->addStretch(1);
    activeOuter->addLayout(manageRow);
    root->addWidget(activeBox);

    // ------------------- Past predictions (all) -------------------
    auto *pastBox = new QGroupBox(QStringLiteral("Past predictions"), this);
    auto *pastLayout = new QVBoxLayout(pastBox);
    this->pastList_ = new LimerinoResultList(pastBox);
    this->pastList_->setColumns({QStringLiteral("prediction"),
                                 QStringLiteral("by"),
                                 QStringLiteral("outcome")});
    pastLayout->addWidget(this->pastList_);
    root->addWidget(pastBox, 1);

    // ------------------- Channel point rewards (all) -------------------
    auto *rewardsBox = new QGroupBox(QStringLiteral("Channel point rewards"),
                                     this);
    auto *rewardsLayout = new QVBoxLayout(rewardsBox);
    this->balanceLabel_ = new QLabel(rewardsBox);
    rewardsLayout->addWidget(this->balanceLabel_);
    this->rewardsList_ = new LimerinoResultList(rewardsBox);
    this->rewardsList_->setColumns({QStringLiteral("reward"),
                                    QStringLiteral("cost"),
                                    QStringLiteral("prompt")});
    rewardsLayout->addWidget(this->rewardsList_);
    this->rewardsRefreshButton_ =
        new QPushButton(QStringLiteral("Refresh rewards"), rewardsBox);
    rewardsLayout->addWidget(this->rewardsRefreshButton_);

    // ---------------- shared (both pages) + appearance page ----------------
    auto *rewardsPage = new QWidget;
    auto *rewardsPageLayout = new QVBoxLayout(rewardsPage);
    rewardsBox->setParent(rewardsPage);
    rewardsPageLayout->addWidget(rewardsBox);
    rewardsPageLayout->addStretch(1);

    auto *appearancePage = new LimerinoAppearanceWidget(this->split_);
    stacked->addWidget(predictionsPage);
    stacked->addWidget(rewardsPage);
    stacked->addWidget(appearancePage);

    // setWidgetResizable would otherwise squash pages into the viewport and
    // hide scrollbars; pin the stacked minimum to the tallest page's hint.
    predictionsPage->adjustSize();
    rewardsPage->adjustSize();
    appearancePage->adjustSize();
    const int contentMinW =
        qMax(predictionsPage->sizeHint().width(),
             qMax(rewardsPage->sizeHint().width(),
                  appearancePage->sizeHint().width()));
    const int contentMinH =
        qMax(predictionsPage->sizeHint().height(),
             qMax(rewardsPage->sizeHint().height(),
                  appearancePage->sizeHint().height()));
    stacked->setMinimumSize(contentMinW, contentMinH);

    this->setMinimumSize(400, 300);
    fitDialogToAvailableScreen(this, 460, 640);

    // ------------------------------ data ------------------------------
    this->draftsCombo_->addItem(QStringLiteral("(select a previous prediction)"));
    for (const QJsonValue &v : readDrafts())
    {
        const QJsonObject o = v.toObject();
        QStringList options;
        for (const QJsonValue &ov : o[QStringLiteral("options")].toArray())
        {
            options.append(ov.toString());
        }
        this->draftsCombo_->addItem(
            QStringLiteral("%1 (%2)")
                .arg(o[QStringLiteral("title")].toString(),
                     options.join(QStringLiteral(" / "))));
    }

    QObject::connect(this->addOptionButton_, &QPushButton::clicked, this,
                     [this] { this->addOptionRow(); });
    QObject::connect(this->createButton_, &QPushButton::clicked, this,
                     [this] { this->submitCreate(); });
    QObject::connect(this->addPollOptionButton_, &QPushButton::clicked, this,
                     [this] { this->addPollOptionRow(); });
    QObject::connect(this->createPollButton_, &QPushButton::clicked, this,
                     [this] { this->submitCreatePoll(); });
    QObject::connect(this->draftsUseButton_, &QPushButton::clicked, this,
                     [this] {
                         const int index = this->draftsCombo_->currentIndex();
                         if (index > 0)
                         {
                             this->refillFromDraft(index - 1);
                         }
                     });
    QObject::connect(this->lockButton_, &QPushButton::clicked, this, [this] {
        if (!this->activeEventId_.isEmpty())
        {
            this->lockPrediction(this->activeEventId_);
        }
    });
    QObject::connect(this->refundButton_, &QPushButton::clicked, this, [this] {
        if (this->activeEventId_.isEmpty())
        {
            return;
        }
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Cancel prediction"),
            QStringLiteral("Cancel the active prediction and refund all "
                           "points? This cannot be undone."));
        if (answer == QMessageBox::Yes)
        {
            this->refundPrediction(this->activeEventId_);
        }
    });
    QObject::connect(this->refreshButton_, &QPushButton::clicked, this,
                     [this] { this->refreshContext(); });
    QObject::connect(this->rewardsRefreshButton_, &QPushButton::clicked, this,
                     [this] { this->refreshRewards(); });
    QObject::connect(this->makeButton_, &QPushButton::clicked, this,
                     [this] { this->makePrediction(); });

    this->applyModGating();
    this->refreshContext();
    this->refreshRewards();

    // Live pane (P4): refresh the dialog whenever a relevant Hermes event
    // lands on this channel/user instead of waiting for manual refresh.
    if (auto *controller = limerino::getPubSubController(); controller != nullptr)
    {
        this->signalHolder_.managedConnect(
            controller->eventProduced,
            [this](const limerino::PubSubEvent &event) {
                auto *tchan = dynamic_cast<TwitchChannel *>(
                    this->split_->getSelectedChannel().get());
                if (tchan == nullptr)
                {
                    return;
                }
                const bool ours = event.channelId == tchan->roomId();
                if (!ours)
                {
                    return;
                }
                if (event.topic.startsWith(
                        QStringLiteral("predictions-channel-v1.")))
                {
                    this->refreshContext();
                }
                else if (event.topic.startsWith(
                             QStringLiteral("community-points-user-v1.")) &&
                         event.eventType == QLatin1String("points-spent"))
                {
                    this->refreshRewards();
                }
            });
    }
}

void LimerinoPredictionDialog::applyModGating()
{
    const auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    const bool mod = tchan != nullptr && tchan->hasModRights();
    this->createBox_->setVisible(mod);
    this->createPollBox_->setVisible(mod);
    this->draftsBox_->setVisible(mod);
    this->lockButton_->setVisible(mod);
    this->refundButton_->setVisible(mod);
}

void LimerinoPredictionDialog::addOptionRow(const QString &text)
{
    if (this->optionsLayout_->count() >= MAX_OPTIONS)
    {
        return;
    }
    auto *row = new QHBoxLayout;
    auto *edit = new QLineEdit(this);
    edit->setText(text);
    edit->setPlaceholderText(
        QStringLiteral("Option %1").arg(this->optionsLayout_->count() + 1));
    row->addWidget(edit, 1);
    auto *remove = new QPushButton(QStringLiteral("x"), this);
    remove->setFixedWidth(26);
    remove->setVisible(this->optionsLayout_->count() >= 2);
    row->addWidget(remove);
    this->optionsLayout_->addLayout(row);
    this->addOptionButton_->setEnabled(this->optionsLayout_->count() <
                                       MAX_OPTIONS);
    QObject::connect(remove, &QPushButton::clicked, this, [this, row] {
        if (this->optionsLayout_->count() <= 2)
        {
            return;
        }
        while (auto *item = row->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
        this->optionsLayout_->removeItem(row);
        delete row;
        this->addOptionButton_->setEnabled(this->optionsLayout_->count() <
                                           MAX_OPTIONS);
    });
}

void LimerinoPredictionDialog::addPollOptionRow(const QString &text)
{
    if (this->pollOptionsLayout_->count() >= MAX_POLL_OPTIONS)
    {
        return;
    }
    auto *row = new QHBoxLayout;
    auto *edit = new QLineEdit(this);
    edit->setText(text);
    edit->setPlaceholderText(
        QStringLiteral("Option %1").arg(this->pollOptionsLayout_->count() + 1));
    row->addWidget(edit, 1);
    auto *remove = new QPushButton(QStringLiteral("x"), this);
    remove->setFixedWidth(26);
    remove->setVisible(this->pollOptionsLayout_->count() >= 2);
    row->addWidget(remove);
    this->pollOptionsLayout_->addLayout(row);
    this->addPollOptionButton_->setEnabled(this->pollOptionsLayout_->count() <
                                           MAX_POLL_OPTIONS);
    QObject::connect(remove, &QPushButton::clicked, this, [this, row] {
        if (this->pollOptionsLayout_->count() <= 2)
        {
            return;
        }
        while (auto *item = row->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
        this->pollOptionsLayout_->removeItem(row);
        delete row;
        this->addPollOptionButton_->setEnabled(
            this->pollOptionsLayout_->count() < MAX_POLL_OPTIONS);
    });
}

void LimerinoPredictionDialog::submitCreatePoll()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr)
    {
        return;
    }
    const QString title = this->pollTitleEdit_->text().trimmed();
    QStringList options;
    for (int i = 0; i < this->pollOptionsLayout_->count(); ++i)
    {
        auto *row = qobject_cast<QHBoxLayout *>(
            this->pollOptionsLayout_->itemAt(i)->layout());
        if (row == nullptr)
        {
            continue;
        }
        auto *edit = qobject_cast<QLineEdit *>(row->itemAt(0)->widget());
        const QString t = edit != nullptr ? edit->text().trimmed() : QString();
        if (!t.isEmpty())
        {
            options.append(t);
        }
    }
    if (title.isEmpty() || options.size() < 2)
    {
        this->activeLabel_->setText(
            QStringLiteral("poll needs a title and at least 2 options"));
        return;
    }

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err.isEmpty()
                                        ? LimerinoAuth::errors::tokenRequiredMessage(
                                              QStringLiteral("create polls"))
                                        : err);
        return;
    }

    // Plugin's CreatePoll variables, verbatim.
    QJsonArray choices;
    for (const QString &o : options)
    {
        choices.append(QJsonObject{{QStringLiteral("title"), o}});
    }
    gql::executePersisted(
        gql::PQ_CREATE_POLL,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("title"), title},
                                 {QStringLiteral("choices"), choices},
                                 {QStringLiteral("durationSeconds"),
                                  this->pollDurationSpin_->value()},
                                 {QStringLiteral("communityPointsCost"), 1},
                                 {QStringLiteral("isCommunityPointsVotingEnabled"),
                                  false},
                                 {QStringLiteral("ownedBy"), tchan->roomId()}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QString code =
                data[QStringLiteral("createPoll")]
                    .toObject()[QStringLiteral("error")]
                    .toObject()[QStringLiteral("code")]
                    .toString();
            g->activeLabel_->setText(
                code.isEmpty() ? QStringLiteral("Successfully created poll!")
                               : QStringLiteral("Unable to create poll! Status: %1")
                                     .arg(code));
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to create poll! Status: %1")
                        .arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::refillFromDraft(int index)
{
    const QJsonArray drafts = readDrafts();
    if (index < 0 || index >= drafts.size())
    {
        return;
    }
    const QJsonObject o = drafts[index].toObject();
    this->titleEdit_->setText(o[QStringLiteral("title")].toString());
    this->windowSpin_->setValue(o[QStringLiteral("windowSeconds")].toInt(120));
    while (this->optionsLayout_->count() > 0)
    {
        auto *item = this->optionsLayout_->takeAt(0);
        if (auto *rowLayout = item->layout())
        {
            while (auto *sub = rowLayout->takeAt(0))
            {
                delete sub->widget();
                delete sub;
            }
        }
        delete item;
    }
    for (const QJsonValue &ov : o[QStringLiteral("options")].toArray())
    {
        this->addOptionRow(ov.toString());
    }
    this->createButton_->setFocus();
}

void LimerinoPredictionDialog::appendDraft(const QString &title,
                                           const QStringList &options,
                                           int windowSeconds)
{
    QJsonArray drafts = readDrafts();
    QJsonArray optionsArr;
    for (const QString &o : options)
    {
        optionsArr.append(o);
    }
    drafts.prepend(QJsonObject{
        {QStringLiteral("title"), title},
        {QStringLiteral("options"), optionsArr},
        {QStringLiteral("windowSeconds"), windowSeconds},
    });
    while (drafts.size() > 5)
    {
        drafts.removeLast();
    }
    writeDrafts(drafts);

    this->draftsCombo_->insertItem(
        1, QStringLiteral("%1 (%2)").arg(title, options.join(QStringLiteral(" / "))));
    while (this->draftsCombo_->count() > 6)
    {
        this->draftsCombo_->removeItem(this->draftsCombo_->count() - 1);
    }
}

void LimerinoPredictionDialog::refreshContext()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr)
    {
        return;
    }
    this->applyModGating();

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->activeLabel_->setText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("view predictions"))
                          : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_CHANNEL_POINTS_PREDICTION_CONTEXT,
        QJsonObject{{QStringLiteral("count"), RESOLVED_HISTORY_COUNT},
                    {QStringLiteral("channelLogin"), tchan->getName()}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this), this,
         tchan](const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QJsonObject channel =
                data[QStringLiteral("community")].toObject()[
                    QStringLiteral("channel")].toObject();
            const QJsonArray active =
                channel[QStringLiteral("activePredictionEvents")].toArray();
            const QJsonArray locked =
                channel[QStringLiteral("lockedPredictionEvents")].toArray();
            const QJsonObject event =
                (!active.isEmpty() ? active.first()
                                   : (!locked.isEmpty() ? locked.first()
                                                        : QJsonObject{}))
                    .toObject();

            this->outcomeCombo_->clear();
            if (event.isEmpty())
            {
                this->activeEventId_.clear();
                this->predictionLocked_ = true;
                this->activeLabel_->setText(
                    QStringLiteral("There is currently no live prediction."));
                this->makeButton_->setEnabled(false);
                this->lockButton_->setEnabled(false);
                this->refundButton_->setEnabled(false);
            }
            else
            {
                this->activeEventId_ = event[QStringLiteral("id")].toString();
                this->predictionLocked_ = locked.contains(event);

                this->activeLabel_->setText(
                    (this->predictionLocked_ ? QStringLiteral("[locked] ")
                                             : QStringLiteral("[active] ")) +
                    event[QStringLiteral("title")].toString() +
                    QStringLiteral(" (by ") +
                    event[QStringLiteral("createdBy")].toObject()[
                        QStringLiteral("displayName")].toString() +
                    QStringLiteral(", window %1s)")
                        .arg(event[QStringLiteral("predictionWindowSeconds")]
                                 .toInt()));

                // Replace outcome rows
                while (auto *item = this->activeLayout_->takeAt(0))
                {
                    if (auto *rowLayout = item->layout())
                    {
                        while (auto *sub = rowLayout->takeAt(0))
                        {
                            delete sub->widget();
                            delete sub;
                        }
                    }
                    delete item;
                }
                const QJsonArray outcomes =
                    event[QStringLiteral("outcomes")].toArray();
                const QString eventId = this->activeEventId_;
                const bool mod = tchan->hasModRights();
                for (const QJsonValue &ov : outcomes)
                {
                    const QJsonObject outcome = ov.toObject();
                    const QString outcomeId =
                        outcome[QStringLiteral("id")].toString();
                    const QString outcomeTitle =
                        outcome[QStringLiteral("title")].toString();
                    this->outcomeCombo_->addItem(outcomeTitle, outcomeId);

                    auto *row = new QHBoxLayout;
                    row->addWidget(
                        new QLabel(QStringLiteral("%1 (%2 points)")
                                       .arg(outcomeTitle)
                                       .arg(outcome[QStringLiteral("totalPoints")]
                                                .toInt())));
                    if (mod)
                    {
                        auto *payButton = new QPushButton(
                            QStringLiteral("Payout"), this);
                        QObject::connect(payButton, &QPushButton::clicked, this,
                                         [this, eventId, outcomeId, outcomeTitle] {
                                             const auto answer =
                                                 QMessageBox::question(
                                                     this,
                                                     QStringLiteral("Pay out"),
                                                     QStringLiteral("Resolve this "
                                                                    "prediction paying "
                                                                    "outcome \"%1\"?")
                                                         .arg(outcomeTitle));
                                             if (answer == QMessageBox::Yes)
                                             {
                                                 this->payoutPrediction(
                                                     eventId, outcomeId);
                                             }
                                         });
                        row->addWidget(payButton);
                    }
                    row->addStretch(1);
                    this->activeLayout_->addLayout(row);
                }
                this->makeButton_->setEnabled(!this->predictionLocked_);
                this->lockButton_->setEnabled(!this->predictionLocked_);
                this->refundButton_->setEnabled(true);
            }

            // ------- past predictions (resolved events) -------
            QVector<QStringList> rows;
            for (const QJsonValue &v :
                 channel[QStringLiteral("resolvedPredictionEvents")]
                     .toObject()[QStringLiteral("edges")]
                     .toArray())
            {
                const QJsonObject node = v.toObject()[QStringLiteral("node")]
                                             .toObject();
                rows.append(
                    {node[QStringLiteral("title")].toString(),
                     node[QStringLiteral("createdBy")].toObject()[
                         QStringLiteral("displayName")].toString(),
                     node[QStringLiteral("winningOutcome")].toObject()[
                         QStringLiteral("title")].toString()});
            }
            this->pastList_->setRows(rows);
            this->pastList_->setStatusText(QStringLiteral("%1 recent predictions")
                                               .arg(rows.size()));
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(e.message);
            }
        });
}

void LimerinoPredictionDialog::submitCreate()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr)
    {
        this->activeLabel_->setText(QStringLiteral("not a twitch channel"));
        return;
    }
    const QString title = this->titleEdit_->text().trimmed();
    QStringList options;
    for (int i = 0; i < this->optionsLayout_->count(); ++i)
    {
        auto *row = qobject_cast<QHBoxLayout *>(
            this->optionsLayout_->itemAt(i)->layout());
        if (row == nullptr)
        {
            continue;
        }
        auto *edit = qobject_cast<QLineEdit *>(row->itemAt(0)->widget());
        const QString t = edit != nullptr ? edit->text().trimmed() : QString();
        if (!t.isEmpty())
        {
            options.append(t);
        }
    }
    if (title.isEmpty() || options.size() < 2)
    {
        this->activeLabel_->setText(
            QStringLiteral("need a title and at least 2 options"));
        return;
    }

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err.isEmpty()
                                        ? LimerinoAuth::errors::tokenRequiredMessage(
                                              QStringLiteral("create predictions"))
                                        : err);
        return;
    }

    QJsonArray outcomes;
    for (int i = 0; i < options.size(); ++i)
    {
        outcomes.append(QJsonObject{
            {QStringLiteral("title"), options.at(i)},
            {QStringLiteral("color"),
             options.size() == 2 && i == 1 ? QStringLiteral("PINK")
                                           : QStringLiteral("BLUE")},
        });
    }
    const int windowSeconds = this->windowSpin_->value();
    gql::executePersisted(
        gql::PQ_CREATE_PREDICTION_EVENT,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("title"), title},
                                 {QStringLiteral("channelID"), tchan->roomId()},
                                 {QStringLiteral("outcomes"), outcomes},
                                 {QStringLiteral("predictionWindowSeconds"),
                                  windowSeconds}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this), title, options,
         windowSeconds](const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QString code =
                data[QStringLiteral("createPredictionEvent")]
                    .toObject()[QStringLiteral("error")]
                    .toObject()[QStringLiteral("code")]
                    .toString();
            g->activeLabel_->setText(
                code.isEmpty() ? QStringLiteral("Successfully created prediction!")
                               : QStringLiteral("Unable to create prediction! Status: %1")
                                     .arg(code));
            if (code.isEmpty())
            {
                g->appendDraft(title, options, windowSeconds);
                g->refreshContext();
            }
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to create prediction! Status: %1")
                        .arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::makePrediction()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr || this->activeEventId_.isEmpty())
    {
        return;
    }
    const QString outcomeId = this->outcomeCombo_->currentData().toString();
    const QString outcomeTitle = this->outcomeCombo_->currentText();
    const int points = this->pointsSpin_->value();
    if (outcomeId.isEmpty() || points <= 0)
    {
        return;
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Make prediction"),
        QStringLiteral("Spend %1 channel points on \"%2\"?")
            .arg(points)
            .arg(outcomeTitle));
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err.isEmpty()
                                        ? LimerinoAuth::errors::tokenRequiredMessage(
                                              QStringLiteral("place a prediction"))
                                        : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_MAKE_PREDICTION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("eventID"), this->activeEventId_},
                                 {QStringLiteral("outcomeID"), outcomeId},
                                 {QStringLiteral("points"), points},
                                 {QStringLiteral("transactionID"),
                                  randomHex(32)}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QString code =
                data[QStringLiteral("makePrediction")]
                    .toObject()[QStringLiteral("error")]
                    .toObject()[QStringLiteral("code")]
                    .toString();
            g->activeLabel_->setText(
                code.isEmpty()
                    ? QStringLiteral("Successfully made prediction!")
                    : QStringLiteral("Unable to make prediction! Status: %1")
                          .arg(code));
            if (code.isEmpty())
            {
                g->refreshContext();
            }
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to make prediction! Status: %1")
                        .arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::lockPrediction(const QString &eventId)
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    QString err;
    auto token = tchan ? LimerinoAuth::resolveModerationToken(
                             tchan->roomId(), tchan->getName(), &err)
                       : LimerinoAuth::LimerinoAuthToken{};
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err);
        return;
    }
    gql::executePersisted(
        gql::PQ_LOCK_PREDICTION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("id"), eventId}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject & /*data*/) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Successfully locked prediction!"));
                g->refreshContext();
            }
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to lock prediction! %1").arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::refundPrediction(const QString &eventId)
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    QString err;
    auto token = tchan ? LimerinoAuth::resolveModerationToken(
                             tchan->roomId(), tchan->getName(), &err)
                       : LimerinoAuth::LimerinoAuthToken{};
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err);
        return;
    }
    gql::executePersisted(
        gql::PQ_DELETE_PREDICTION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("id"), eventId}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject & /*data*/) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Successfully deleted prediction!"));
                g->refreshContext();
            }
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to delete prediction! %1").arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::payoutPrediction(const QString &eventId,
                                                const QString &outcomeId)
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    QString err;
    auto token = tchan ? LimerinoAuth::resolveModerationToken(
                             tchan->roomId(), tchan->getName(), &err)
                       : LimerinoAuth::LimerinoAuthToken{};
    if (!token.hasToken())
    {
        this->activeLabel_->setText(err);
        return;
    }
    gql::executePersisted(
        gql::PQ_RESOLVE_PREDICTION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("eventID"), eventId},
                                 {QStringLiteral("outcomeID"), outcomeId}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject & /*data*/) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Successfully resolved prediction!"));
                g->refreshContext();
            }
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->activeLabel_->setText(
                    QStringLiteral("Unable to resolve prediction! %1").arg(e.message));
            }
        });
}

void LimerinoPredictionDialog::refreshRewards()
{
    auto *tchan = dynamic_cast<TwitchChannel *>(
        this->split_->getSelectedChannel().get());
    if (tchan == nullptr)
    {
        return;
    }
    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->balanceLabel_->setText(
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("see channel point rewards"))
                : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_CHANNEL_POINTS_CONTEXT,
        QJsonObject{{QStringLiteral("channelLogin"), tchan->getName()},
                    {QStringLiteral("includeGoalTypes"),
                     QJsonArray{QStringLiteral("CREATOR")}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this), this,
         tchan](const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QJsonObject channel =
                data[QStringLiteral("community")].toObject()[
                    QStringLiteral("channel")].toObject();
            const int balance =
                channel[QStringLiteral("self")].toObject()[
                    QStringLiteral("communityPoints")].toObject()[
                    QStringLiteral("balance")].toInt();
            this->balanceLabel_->setText(
                QStringLiteral("Rewards menu (%1 points available)").arg(balance));

            const QJsonArray rewards =
                channel[QStringLiteral("communityPointsSettings")]
                    .toObject()[QStringLiteral("customRewards")]
                    .toArray();
            QVector<QStringList> rows;
            QVector<QJsonObject> store;
            for (const QJsonValue &v : rewards)
            {
                const QJsonObject r = v.toObject();
                rows.append({
                    r[QStringLiteral("title")].toString() +
                        (r[QStringLiteral("isEnabled")].toBool()
                             ? QString()
                             : QStringLiteral(" -DISABLED")),
                    QString::number(r[QStringLiteral("cost")].toInt()),
                    r[QStringLiteral("prompt")].toString(),
                });
                store.append(r);
            }
            this->rewardsList_->setRows(rows);
            this->rewardsList_->setStatusText(
                QStringLiteral("%1 rewards").arg(rows.size()));

            const QString channelId = channel[QStringLiteral("id")].toString();
            this->rewardsList_->setRowMenuProvider(
                [g, channelId, store = std::move(store)](
                    const QStringList &row, QMenu *menu) {
                    // match the visible row back to its reward object by title
                    QString title = row.value(0);
                    title.remove(QStringLiteral(" -DISABLED"));
                    const QJsonObject *reward = nullptr;
                    for (const QJsonObject &r : store)
                    {
                        if (r[QStringLiteral("title")].toString() == title)
                        {
                            reward = &r;
                            break;
                        }
                    }
                    if (reward == nullptr ||
                        !(*reward)[QStringLiteral("isEnabled")].toBool())
                    {
                        return;
                    }
                    const QJsonObject rewardCopy = *reward;
                    menu->addAction(
                        QStringLiteral("Redeem \"%1\" (%2 points)")
                            .arg(title)
                            .arg(rewardCopy[QStringLiteral("cost")].toInt()),
                        [g, channelId, rewardCopy] {
                            if (g)
                            {
                                g->promptRedeem(channelId, rewardCopy);
                            }
                        });
                });
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->balanceLabel_->setText(e.message);
            }
        });
}

void LimerinoPredictionDialog::promptRedeem(const QString &channelId,
                                            const QJsonObject &reward)
{
    const QString title = reward[QStringLiteral("title")].toString();
    const int cost = reward[QStringLiteral("cost")].toInt();
    const QString prompt = reward[QStringLiteral("prompt")].toString();

    QString textInput;
    if (!prompt.isEmpty())
    {
        bool ok = false;
        textInput = QInputDialog::getMultiLineText(
            this, QStringLiteral("Redeem \"%1\"").arg(title), prompt, {}, &ok);
        if (!ok)
        {
            return;
        }
    }

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Redeem reward"),
        QStringLiteral("Spend %1 channel points on \"%2\"?")
            .arg(cost)
            .arg(title));
    if (answer != QMessageBox::Yes)
    {
        return;
    }
    this->redeemReward(channelId, reward, textInput);
}

void LimerinoPredictionDialog::redeemReward(const QString &channelId,
                                            const QJsonObject &reward,
                                            const QString &textInput)
{
    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        this->balanceLabel_->setText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("redeem rewards"))
                          : err);
        return;
    }

    // Plugin's RedeemCustomReward input, verbatim.
    gql::executePersisted(
        gql::PQ_REDEEM_CUSTOM_REWARD,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), channelId},
                                 {QStringLiteral("cost"),
                                  reward[QStringLiteral("cost")].toInt()},
                                 {QStringLiteral("prompt"),
                                  reward[QStringLiteral("prompt")].toString()},
                                 {QStringLiteral("textInput"), textInput},
                                 {QStringLiteral("rewardID"),
                                  reward[QStringLiteral("id")].toString()},
                                 {QStringLiteral("title"),
                                  reward[QStringLiteral("title")].toString()},
                                 {QStringLiteral("transactionID"),
                                  randomHex(32)}}}},
        token.token,
        [g = QPointer<LimerinoPredictionDialog>(this)](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QString code =
                data[QStringLiteral("redeemCommunityPointsCustomReward")]
                    .toObject()[QStringLiteral("error")]
                    .toObject()[QStringLiteral("code")]
                    .toString();
            g->balanceLabel_->setText(
                code.isEmpty()
                    ? QStringLiteral("Successfully redeemed reward!")
                    : QStringLiteral("Unable to redeem reward! Error: %1")
                          .arg(code));
            g->refreshRewards();
        },
        [g = QPointer<LimerinoPredictionDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->balanceLabel_->setText(
                    QStringLiteral("Unable to redeem reward! Error: %1")
                        .arg(e.message));
            }
        });
}

}  // namespace chatterino::limerino
