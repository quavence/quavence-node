// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "airegistry.h"
#include "main.h"
#include <QSettings>
#include <QJsonObject>
#include <QJsonDocument>
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "wallet/wallet.h"

#include "chainparams.h"

#include <QAbstractItemDelegate>
#include <QPainter>

#define ROW_HEIGHT 44
#define ROW_PADDING_H 0
#define NUM_ITEMS 5

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC)
    {
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QRect mainRect = option.rect;
        int xspace = ROW_PADDING_H;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace - ROW_PADDING_H, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace - ROW_PADDING_H, halfheight);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else if(amount > 0)
        {
            foreground = COLOR_BRAND_PRIMARY;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(COLOR_BRAND_MUTED);
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(0, ROW_HEIGHT);
    }

    int unit;
};
#include "overviewpage.moc"

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentStake(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    currentWatchOnlyStake(-1),
    currentDonationPercentage(0),
    txdelegate(new TxViewDelegate(this))
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions (miniapp-style: text only, no large type icons)
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(4);
    ui->horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(0, 0));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (ROW_HEIGHT + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listTransactions->setStyleSheet(
        "QListView { background: transparent; border: none; padding: 0px; margin: 0px; }"
        "QListView::item { padding: 0px; margin: 0px; }");

    // Dev-fee row: hidden until network enables treasury in chainparams
    ui->labelDonations->setVisible(false);
    ui->labelDonationsText->setVisible(false);

    // AI Worker & DePIN Overview Poller
    aiNetworkManager = new QNetworkAccessManager(this);
    aiWorkerTimer = new QTimer(this);
    connect(aiWorkerTimer, &QTimer::timeout, this, &OverviewPage::updateAiWorkerOverview);
    aiWorkerTimer->start(15000); // 15s
    updateAiWorkerOverview();

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& stake, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchOnlyStake, const unsigned int& nDonationPercentage)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentStake = stake;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentWatchOnlyStake = watchOnlyStake;
    currentDonationPercentage = nDonationPercentage;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelStake->setText(BitcoinUnits::formatWithUnit(unit, stake, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance + stake, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchStake->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyStake, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + watchOnlyStake, false, BitcoinUnits::separatorAlways));
    ui->labelDonations->setText((QString::number(currentDonationPercentage) + "% of stake rewards"));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showStake = stake != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;
    bool showWatchOnlyStake = watchOnlyStake != 0;
    bool showDonations = nDonationPercentage != 0 && !Params().GetDevFundAddress().empty();

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
    ui->labelStake->setVisible(showStake || showWatchOnlyStake);
    ui->labelStakeText->setVisible(showStake || showWatchOnlyStake);
    ui->labelWatchStake->setVisible(showWatchOnlyStake); // show watch-only stake balance
    ui->labelDonations->setVisible(showDonations);
    ui->labelDonationsText->setVisible(showDonations);
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
    {
        ui->labelWatchImmature->hide();
        ui->labelWatchStake->hide();
    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getStake(),
                           model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(), model->getWatchStake(),
                           model->getDonationPercentage());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,unsigned int)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,unsigned int)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
        	 setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentStake,
        	 currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance, currentWatchOnlyStake,
             currentDonationPercentage);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updateAiWorkerOverview()
{
    QSettings settings;
    settings.beginGroup("AIWorker");
    bool isWorkerEnabled = settings.value("workerEnabled", false).toBool();
    QString workerToken = settings.value("workerToken").toString().trimmed();
    settings.endGroup();

    // 1. On-Chain PoUS Consensus Status (Personal Worker Boost & Network Attestations)
    uint32_t localCredits = 0;
    int localWorkerBoost = 0;
    if (chainActive.Tip()) {
        int height = chainActive.Height();
        if (pwalletMain) {
            std::set<CKeyID> setKeys;
            pwalletMain->GetKeys(setKeys);
            for (std::set<CKeyID>::const_iterator it = setKeys.begin(); it != setKeys.end(); ++it) {
                uint32_t c = GetWorkerCreditsInWindow(*it, height);
                if (c > localCredits) {
                    localCredits = c;
                }
            }
            localWorkerBoost = GetWorkerPoUSBoost(localCredits);
        }

        int onChainBoost = GetActiveAiStakeBoost(height);
        int onChainAttestations = GetAiAttestationsCountInWindow(height);
        if (onChainBoost > localWorkerBoost) {
            localWorkerBoost = onChainBoost;
        }
        if (onChainAttestations > (int)localCredits) {
            localCredits = onChainAttestations;
        }
    }

    static int s_cachedDoneToday = 0;
    uint32_t effectiveDisplayTasks = std::max((uint32_t)s_cachedDoneToday, localCredits);

    if (localWorkerBoost == 0 && (effectiveDisplayTasks > 0 || isWorkerEnabled)) {
        localWorkerBoost = 20;
        if (effectiveDisplayTasks == 0) effectiveDisplayTasks = 1;
    }

    if (localWorkerBoost > 0) {
        ui->labelAiBoostValue->setText(QString("+%1% (%2 tasks)").arg(localWorkerBoost).arg(effectiveDisplayTasks));
        ui->labelAiBoostValue->setStyleSheet("color: #2563eb; font-weight: bold; font-size: 11px;");
    } else {
        ui->labelAiBoostValue->setText("0% (Standby)");
        ui->labelAiBoostValue->setStyleSheet("color: #64748b; font-weight: 500; font-size: 11px;");
    }

    // 2. Status Badge
    if (!isWorkerEnabled) {
        ui->labelAiWorkerBadge->setText("○ STANDBY");
        ui->labelAiWorkerBadge->setStyleSheet("background-color: #f1f5f9; color: #64748b; border: 1px solid #cbd5e1; border-radius: 4px; padding: 2px 8px; font-weight: 600; font-size: 11px;");
    } else {
        ui->labelAiWorkerBadge->setText("● ACTIVE");
        ui->labelAiWorkerBadge->setStyleSheet("background-color: #eff6ff; color: #2563eb; border: 1px solid #bfdbfe; border-radius: 4px; padding: 2px 8px; font-weight: 600; font-size: 11px;");
    }

    // 3. Query Hub API if Worker Token is configured
    if (workerToken.isEmpty()) {
        return;
    }

    QString hubUrl = settings.value("hubBaseUrl", "https://quavence.com").toString().trimmed();
    if (hubUrl.isEmpty()) {
        hubUrl = "https://quavence.com";
    }

    QUrl url(hubUrl + "/api/ai/nodes/self/overview");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", QString("Bearer %1").arg(workerToken).toUtf8());

    QNetworkReply *reply = aiNetworkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localCredits, localWorkerBoost, isWorkerEnabled]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return;

        QJsonObject root = doc.object();
        if (!root.value("success").toBool()) return;

        QJsonObject payload = root.value("data").toObject();
        QJsonObject tasks = payload.value("tasks").toObject();
        QJsonObject rewards = payload.value("rewards").toObject();

        int doneToday = tasks.value("doneToday").toInt(tasks.value("done_today").toInt(0));
        int totalDone = tasks.value("done").toInt(0);
        auto parseAmount = [](const QJsonValue &val) -> double {
            if (val.isDouble()) return val.toDouble();
            if (val.isString()) return val.toString().toDouble();
            return 0.0;
        };
        double accrued = parseAmount(rewards.contains("accrued_amount") ? rewards.value("accrued_amount") : rewards.value("accruedAmount"));
        double paid = parseAmount(rewards.contains("paid_amount") ? rewards.value("paid_amount") : rewards.value("paidAmount"));

        ui->labelAiTasksValue->setText(QString("Today: %1  ·  Total: %2").arg(doneToday).arg(totalDone));
        ui->labelAiAccruedValue->setText(QString("%1 QVNC").arg(QString::number(accrued, 'f', 8)));
        ui->labelAiPaidValue->setText(QString("%1 QVNC").arg(QString::number(paid, 'f', 8)));

        s_cachedDoneToday = doneToday;
        int activeTasks = std::max((int)localCredits, doneToday);
        int activeBoost = localWorkerBoost;
        if (activeTasks >= 10) activeBoost = std::max(activeBoost, 50);
        else if (activeTasks >= 5) activeBoost = std::max(activeBoost, 35);
        else if (activeTasks >= 1) activeBoost = std::max(activeBoost, 20);
        else if (isWorkerEnabled) activeBoost = std::max(activeBoost, 20);

        if (activeBoost > 0) {
            ui->labelAiBoostValue->setText(QString("+%1% (%2 tasks)").arg(activeBoost).arg(activeTasks));
            ui->labelAiBoostValue->setStyleSheet("color: #2563eb; font-weight: bold; font-size: 11px;");
        }
    });
}
