// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#include "glyphspage.h"
#include "ui_glyphspage.h"

#include "airegistry.h"
#include "clientmodel.h"
#include "dstencode.h"
#include "guiutil.h"
#include "glyphstransferdialog.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "main.h"
#include "random.h"
#include "utilstrencodings.h"
#include "utiltime.h"


#include <algorithm>
#include <set>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QShowEvent>
#include <QTableWidgetItem>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>


GlyphsPage::GlyphsPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::GlyphsPage),
    clientModel(nullptr),
    walletModel(nullptr),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    // Setup table columns
    ui->tableGlyphs->setColumnCount(6);
    ui->tableGlyphs->verticalHeader()->hide();
    ui->tableGlyphs->setShowGrid(true);

    QStringList headers;
    headers << tr("Edition")
            << tr("Glyph Hash")
            << tr("Carrier Address")
            << tr("Carrier UTXO")
            << tr("Protection")
            << tr("Confirmations");
    ui->tableGlyphs->setHorizontalHeaderLabels(headers);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    ui->tableGlyphs->setColumnWidth(0, 80);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    ui->tableGlyphs->setColumnWidth(1, 185);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    ui->tableGlyphs->setColumnWidth(3, 125);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    ui->tableGlyphs->setColumnWidth(4, 115);

    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    ui->tableGlyphs->setColumnWidth(5, 110);

    ui->tableGlyphs->horizontalHeader()->setStretchLastSection(false);

    for (int col = 0; col < 6; ++col) {
        QTableWidgetItem *hItem = ui->tableGlyphs->horizontalHeaderItem(col);
        if (hItem) {
            if (col == 0 || col == 3 || col == 4 || col == 5) {
                hItem->setTextAlignment(Qt::AlignCenter);
            } else {
                hItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
    }

    // Connections
    connect(ui->btnRefresh, SIGNAL(clicked()), this, SLOT(onRefreshClicked()));
    connect(ui->btnExplorerAll, SIGNAL(clicked()), this, SLOT(onExplorerClicked()));
    connect(ui->btnEmptyExplorer, SIGNAL(clicked()), this, SLOT(onExplorerClicked()));
    connect(ui->btnTransfer, SIGNAL(clicked()), this, SLOT(onTransferClicked()));
    connect(ui->btnExplorerRow, SIGNAL(clicked()), this, SLOT(onExplorerClicked()));
    connect(ui->btnCopyHash, SIGNAL(clicked()), this, SLOT(onCopyHashClicked()));
    connect(ui->btnCopyAddress, SIGNAL(clicked()), this, SLOT(onCopyAddressClicked()));
    connect(ui->tableGlyphs, SIGNAL(itemSelectionChanged()), this, SLOT(onTableSelectionChanged()));
    connect(ui->tableGlyphs, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(onTableDoubleClicked(int,int)));

    ui->tableGlyphs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tableGlyphs, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCustomContextMenu(QPoint)));

    // Compute Pass initialization & connections
    lsaNetworkManager = new QNetworkAccessManager(this);
    selectedEdition = 0;
    showConsumerId = false;
    isAgentLinked = false;
    ui->btnToggleIdPrivacy->setVisible(false);
    ui->btnToggleIdPrivacy->setCursor(Qt::PointingHandCursor);
    connect(ui->btnOpenTelegram, SIGNAL(clicked()), this, SLOT(onOpenTelegramClicked()));
    connect(ui->btnOpenWebHub, SIGNAL(clicked()), this, SLOT(onOpenWebHubClicked()));
    connect(ui->btnToggleIdPrivacy, SIGNAL(clicked()), this, SLOT(onToggleIdPrivacyClicked()));

    // Live Telemetry Poller: updates fuel, cycles, phase in real-time (no tab switching needed)
    QTimer *telemetryTimer = new QTimer(this);
    connect(telemetryTimer, &QTimer::timeout, this, [this]() {
        if (selectedEdition > 0 && isVisible()) {
            requestAgentState(selectedEdition);
        }
    });
    telemetryTimer->start(30000); // 30s

    updateGlyphs();
}

GlyphsPage::~GlyphsPage()
{
    delete ui;
}

void GlyphsPage::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if (clientModel) {
        connect(clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)),
                this, SLOT(updateGlyphs()));
    }
}

void GlyphsPage::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
    if (walletModel) {
        connect(walletModel, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,unsigned int)),
                this, SLOT(updateGlyphs()));
        connect(walletModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)),
                this, SLOT(updateGlyphs()));
    }
    updateGlyphs();
}

void GlyphsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateGlyphs();
}

void GlyphsPage::updateGlyphs()
{
    uint32_t prevSelectedEdition = selectedEdition;
    currentGlyphs.clear();

    if (!walletModel || !walletModel->getWallet()) {
        ui->stackedWidget->setCurrentIndex(0);
        ui->labelKpiCount->setText(QString("<span style='font-size:18px; font-weight:700; color:#0f172a;'>0</span> <span style='font-size:12px; font-weight:500; color:#64748b;'>%1</span>").arg(tr("Relics")));
        return;
    }

    std::set<std::pair<uint256, unsigned int>> seenCoins;

    // 1. Primary Source: listCoins (same method that powers CoinControl, includes available & locked coins)
    std::map<QString, std::vector<COutput>> mapCoins;
    walletModel->listCoins(mapCoins);

    for (const auto& pair : mapCoins) {
        const QString& groupAddress = pair.first;
        for (const COutput& out : pair.second) {
            if (!out.tx) continue;

            uint256 txhash = out.tx->GetHash();
            if (walletModel->getWallet() && walletModel->getWallet()->IsSpent(txhash, out.i)) {
                continue;
            }

            GlyphCarrierRecord glyphRec;
            if (GetTxGlyphCarrier(*out.tx, out.i, glyphRec)) {
                uint256 txhash = out.tx->GetHash();
                seenCoins.insert({txhash, (unsigned int)out.i});

                GlyphEntry item;
                item.edition = glyphRec.edition;
                item.glyphHash = glyphRec.glyphHash;
                item.txid = txhash;
                item.vout = out.i;
                item.confirmations = out.nDepth;
                item.isLocked = walletModel->isLockedCoin(txhash, out.i);

                CTxDestination dest;
                if (ExtractDestination(out.tx->vout[out.i].scriptPubKey, dest)) {
                    item.carrierAddress = QString::fromStdString(EncodeDestination(dest));
                } else if (!groupAddress.isEmpty()) {
                    item.carrierAddress = groupAddress;
                } else {
                    item.carrierAddress = tr("(unknown)");
                }

                currentGlyphs.push_back(item);
            }
        }
    }

    // 2. Comprehensive Fallback: mapWallet scan
    CWallet *wallet = walletModel->getWallet();
    if (wallet) {
        LOCK2(cs_main, wallet->cs_wallet);

        for (const auto& entry : wallet->mapWallet) {
            const CWalletTx& wtx = entry.second;
            int nDepth = wtx.GetDepthInMainChain();
            if (nDepth < 0) continue; // In conflict/dead branch

            for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
                if (wtx.vout[i].nValue == GLYPH_CARRIER_DUST && (wallet->IsMine(wtx.vout[i]) & (ISMINE_SPENDABLE | ISMINE_WATCH_ONLY))) {
                    uint256 txhash = entry.first;
                    if (seenCoins.count({txhash, i})) {
                        continue;
                    }

                    if (wallet->IsSpent(txhash, i)) {
                        continue; // Spent on-chain
                    }

                    GlyphCarrierRecord glyphRec;
                    if (GetTxGlyphCarrier(wtx, i, glyphRec)) {
                        seenCoins.insert({txhash, i});

                        GlyphEntry item;
                        item.edition = glyphRec.edition;
                        item.glyphHash = glyphRec.glyphHash;
                        item.txid = txhash;
                        item.vout = i;
                        item.confirmations = nDepth;
                        item.isLocked = wallet->IsLockedCoin(txhash, i);

                        CTxDestination dest;
                        if (ExtractDestination(wtx.vout[i].scriptPubKey, dest)) {
                            item.carrierAddress = QString::fromStdString(EncodeDestination(dest));
                        } else {
                            item.carrierAddress = tr("(unknown)");
                        }

                        currentGlyphs.push_back(item);
                    }
                }
            }
        }
    }

    // Sort by edition ascending
    std::sort(currentGlyphs.begin(), currentGlyphs.end(), [](const GlyphEntry& a, const GlyphEntry& b) {
        return a.edition < b.edition;
    });

    // Populate table
    ui->tableGlyphs->setRowCount(0);
    for (size_t row = 0; row < currentGlyphs.size(); ++row) {
        const GlyphEntry& g = currentGlyphs[row];
        ui->tableGlyphs->insertRow(row);

        // Edition (clean, no emoji, centered)
        QString editionStr = QString("#%1").arg(g.edition);
        QTableWidgetItem *itemEdition = new QTableWidgetItem(editionStr);
        itemEdition->setFont(QFont(font().family(), 10, QFont::Bold));
        itemEdition->setTextAlignment(Qt::AlignCenter);
        ui->tableGlyphs->setItem(row, 0, itemEdition);

        // Hash (left-aligned)
        std::string hashHex = g.glyphHash.ToString();
        QString hashShort = QString::fromStdString(hashHex.substr(0, 10) + "..." + hashHex.substr(hashHex.length() - 8));
        QTableWidgetItem *itemHash = new QTableWidgetItem(hashShort);
        itemHash->setToolTip(QString::fromStdString(hashHex));
        itemHash->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        ui->tableGlyphs->setItem(row, 1, itemHash);

        // Carrier Address (left-aligned)
        QTableWidgetItem *itemAddress = new QTableWidgetItem(g.carrierAddress);
        itemAddress->setToolTip(g.carrierAddress);
        itemAddress->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        ui->tableGlyphs->setItem(row, 2, itemAddress);

        // Carrier UTXO (centered)
        QString utxoStr = QString("%1:%2")
            .arg(QString::fromStdString(g.txid.ToString().substr(0, 8) + "..."))
            .arg(g.vout);
        QTableWidgetItem *itemUtxo = new QTableWidgetItem(utxoStr);
        itemUtxo->setToolTip(QString("%1:%2").arg(QString::fromStdString(g.txid.ToString())).arg(g.vout));
        itemUtxo->setTextAlignment(Qt::AlignCenter);
        ui->tableGlyphs->setItem(row, 3, itemUtxo);

        // Protection (clean text, no emoji, centered)
        QString lockStr = g.isLocked ? tr("Auto-Locked") : tr("Unlocked");
        QTableWidgetItem *itemLock = new QTableWidgetItem(lockStr);
        itemLock->setToolTip(tr("PoUS Staking & Spending Immunity Active"));
        itemLock->setTextAlignment(Qt::AlignCenter);
        ui->tableGlyphs->setItem(row, 4, itemLock);

        // Confirmations (centered)
        QTableWidgetItem *itemConf = new QTableWidgetItem(QString::number(g.confirmations));
        itemConf->setTextAlignment(Qt::AlignCenter);
        ui->tableGlyphs->setItem(row, 5, itemConf);
    }

    // Update KPI metrics & stack view
    size_t count = currentGlyphs.size();
    ui->labelKpiCount->setText(QString("<span style='font-size:18px; font-weight:700; color:#0f172a;'>%1</span> <span style='font-size:12px; font-weight:500; color:#64748b;'>%2</span>")
        .arg(count)
        .arg(count == 1 ? tr("Relic") : tr("Relics")));

    if (count == 0) {
        ui->stackedWidget->setCurrentIndex(0);
        selectedEdition = 0;
        updateSelectionState();
    } else {
        ui->stackedWidget->setCurrentIndex(1);

        // Preserve previous selection or auto-select first glyph by default
        int rowToSelect = -1;
        if (prevSelectedEdition > 0) {
            for (size_t r = 0; r < currentGlyphs.size(); ++r) {
                if (currentGlyphs[r].edition == prevSelectedEdition) {
                    rowToSelect = (int)r;
                    break;
                }
            }
        }
        if (rowToSelect == -1 && !currentGlyphs.empty()) {
            rowToSelect = 0; // Auto-select first glyph
        }

        if (rowToSelect >= 0 && rowToSelect < (int)currentGlyphs.size()) {
            ui->tableGlyphs->selectRow(rowToSelect);
        } else {
            updateSelectionState();
        }
    }
}

void GlyphsPage::onTableSelectionChanged()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        uint16_t newEdition = currentGlyphs[row].edition;
        if (newEdition != selectedEdition) {
            selectedEdition = newEdition;
            showConsumerId = false;
        }
        ui->lblCardTitle->setText(QString("PoUS Glyph #%1 · Compute Pass").arg(selectedEdition));
        requestAgentState(selectedEdition);
        updateSelectionState();
    } else if (!currentGlyphs.empty()) {
        // Sticky Selection: if user clicked table margin or focus shifted,
        // restore selection of the currently active edition if it is still valid.
        int rowToRestore = -1;
        if (selectedEdition > 0) {
            for (size_t r = 0; r < currentGlyphs.size(); ++r) {
                if (currentGlyphs[r].edition == selectedEdition) {
                    rowToRestore = (int)r;
                    break;
                }
            }
        }
        if (rowToRestore >= 0) {
            ui->tableGlyphs->selectRow(rowToRestore);
            return;
        }

        selectedEdition = 0;
        lastLoadedPreviewKey.clear();
        ui->lblGlyphPreview->setText(QString("<span style='color: #64748b; font-size: 11px; font-family: monospace;'>%1</span>").arg(tr("SELECT GLYPH")));
        ui->lblCardTitle->setText(tr("PoUS Glyph · Compute Pass"));
        ui->lblRarityBadge->setText(tr("[NONE]"));
        ui->lblRarityBadge->setStyleSheet("font-size: 10px; font-weight: 700; font-family: monospace; color: #64748b; background-color: #0f172a; border-radius: 3px; padding: 1px 4px;");
        ui->lblOnchainStatus->setText(tr("Auto-Locked · Carrier: 10,000 sat"));
        ui->lblDailyQuota->setText(tr("Daily Quota: —"));
        ui->lblQuotaStatus->setText(tr("Available: — · Resets in —"));
        ui->lblFuelReserve->setText(tr("Fuel Reserve: —"));
        isAgentLinked = false;
        rawConsumerId.clear();
        showConsumerId = false;
        updateLinkStatusDisplay();
        ui->btnOpenWebHub->setText(tr("Link to Web Hub"));
        updateSelectionState();
    } else {
        selectedEdition = 0;
        ui->lblGlyphPreview->setText(QString("<span style='color: #64748b; font-size: 11px; font-family: monospace;'>%1</span>").arg(tr("SELECT GLYPH")));
        ui->lblCardTitle->setText(tr("PoUS Glyph · Compute Pass"));
        ui->lblRarityBadge->setText(tr("[NONE]"));
        ui->lblRarityBadge->setStyleSheet("font-size: 10px; font-weight: 700; font-family: monospace; color: #64748b; background-color: #0f172a; border-radius: 3px; padding: 1px 4px;");
        ui->lblOnchainStatus->setText(tr("Auto-Locked · Carrier: 10,000 sat"));
        ui->lblDailyQuota->setText(tr("Daily Quota: —"));
        ui->lblQuotaStatus->setText(tr("Available: — · Resets in —"));
        ui->lblFuelReserve->setText(tr("Fuel Reserve: —"));
        isAgentLinked = false;
        rawConsumerId.clear();
        showConsumerId = false;
        updateLinkStatusDisplay();
        ui->btnOpenWebHub->setText(tr("Link to Web Hub"));
        updateSelectionState();
    }
}

void GlyphsPage::updateSelectionState()
{
    int row = ui->tableGlyphs->currentRow();
    bool hasSelection = (row >= 0 && row < (int)currentGlyphs.size());
    ui->btnTransfer->setEnabled(hasSelection);
    ui->btnExplorerRow->setEnabled(hasSelection);
    ui->btnCopyHash->setEnabled(hasSelection);
    ui->btnCopyAddress->setEnabled(hasSelection);

    ui->btnOpenTelegram->setEnabled(hasSelection);
    ui->btnOpenWebHub->setEnabled(hasSelection);
}

void GlyphsPage::onTableDoubleClicked(int row, int /*column*/)
{
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        onTransferClicked();
    }
}

void GlyphsPage::onTransferClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row < 0 || row >= (int)currentGlyphs.size()) {
        return;
    }

    const GlyphEntry& g = currentGlyphs[row];
    GlyphTransferDialog dlg(platformStyle, g.edition, g.glyphHash, g.txid, g.vout, g.carrierAddress, this);
    dlg.setWalletModel(walletModel);
    if (dlg.exec() == QDialog::Accepted) {
        updateGlyphs();
    }
}

void GlyphsPage::onExplorerClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        const GlyphEntry& g = currentGlyphs[row];
        QString url = QString("https://explorer.quavence.com/glyph/%1").arg(g.edition);
        QDesktopServices::openUrl(QUrl(url));
    } else {
        QDesktopServices::openUrl(QUrl("https://explorer.quavence.com/glyphs"));
    }
}

void GlyphsPage::onRefreshClicked()
{
    updateGlyphs();
}

void GlyphsPage::onCopyHashClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        QApplication::clipboard()->setText(QString::fromStdString(currentGlyphs[row].glyphHash.ToString()));
    }
}

void GlyphsPage::onCopyAddressClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        QApplication::clipboard()->setText(currentGlyphs[row].carrierAddress);
    }
}

void GlyphsPage::onCopyTxIdClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        QApplication::clipboard()->setText(QString::fromStdString(currentGlyphs[row].txid.ToString()));
    }
}

void GlyphsPage::onCopyUtxoClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        const GlyphEntry& g = currentGlyphs[row];
        QString utxoStr = QString("%1:%2").arg(QString::fromStdString(g.txid.ToString())).arg(g.vout);
        QApplication::clipboard()->setText(utxoStr);
    }
}

void GlyphsPage::onCopyEditionClicked()
{
    int row = ui->tableGlyphs->currentRow();
    if (row >= 0 && row < (int)currentGlyphs.size()) {
        QApplication::clipboard()->setText(QString::number(currentGlyphs[row].edition));
    }
}

void GlyphsPage::onCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = ui->tableGlyphs->indexAt(pos);
    if (!index.isValid()) return;
    int row = index.row();
    if (row < 0 || row >= (int)currentGlyphs.size()) return;

    ui->tableGlyphs->selectRow(row);
    updateSelectionState();

    const GlyphEntry &g = currentGlyphs[row];
    QMenu contextMenu(this);

    contextMenu.addAction(tr("Copy Carrier Address"), this, SLOT(onCopyAddressClicked()));
    contextMenu.addAction(tr("Copy Carrier Transaction ID"), this, SLOT(onCopyTxIdClicked()));
    contextMenu.addAction(tr("Copy Carrier UTXO (txid:vout)"), this, SLOT(onCopyUtxoClicked()));
    contextMenu.addAction(tr("Copy Glyph Hash"), this, SLOT(onCopyHashClicked()));
    contextMenu.addAction(tr("Copy Edition (#%1)").arg(g.edition), this, SLOT(onCopyEditionClicked()));

    contextMenu.addSeparator();
    contextMenu.addAction(tr("Safe Transfer Selected Glyph..."), this, SLOT(onTransferClicked()));
    contextMenu.addAction(tr("View in Quavence Explorer"), this, SLOT(onExplorerClicked()));

    contextMenu.exec(ui->tableGlyphs->viewport()->mapToGlobal(pos));
}

QString GlyphsPage::getHubBaseUrl() const
{
    QString hubEnv = QString::fromLocal8Bit(qgetenv("QUAVENCE_HUB_URL"));
    if (!hubEnv.trimmed().isEmpty()) {
        return hubEnv.trimmed();
    }
    return QString("https://quavence.com");
}

void GlyphsPage::requestAgentState(uint16_t edition)
{
    if (edition == 0) return;
    QUrl url(getHubBaseUrl() + "/api/sovereign-agent/" + QString::number(edition) + "/state");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = lsaNetworkManager->get(request);
    connect(reply, SIGNAL(finished()), this, SLOT(onAgentStateReply()));
}

void GlyphsPage::onAgentStateReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        ui->lblDailyQuota->setText(tr("Daily Quota: [OFFLINE]"));
        ui->lblQuotaStatus->setText(tr("Available: — · Resets in —"));
        ui->lblFuelReserve->setText(tr("Fuel Reserve: [OFFLINE]"));
        ui->lblLinkStatus->setText(tr("Telegram: [OFFLINE]"));
        ui->lblLinkStatus->setStyleSheet("font-size: 10px; color: #64748b; font-family: monospace;");
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        reply->deleteLater();
        return;
    }

    QJsonObject obj = doc.object();
    QJsonObject computePass = obj["compute_pass"].toObject();

    QString tier = computePass.contains("tier") ? computePass["tier"].toString("Common") : obj["rarity"].toString("Common");
    double dailyQuotaUsd = computePass["daily_quota_usd"].toDouble(0.0);
    double dailyQuotaQvnc = computePass["daily_quota_qvnc"].toDouble(0.0);
    double quotaRemainingUsd = computePass["quota_remaining_usd"].toDouble(dailyQuotaUsd);
    int resetsInHours = computePass["quota_resets_in_hours"].toInt(24);

    double fuelReserve = 0.0;
    if (computePass.contains("fuel_reserve_qvnc")) {
        fuelReserve = computePass["fuel_reserve_qvnc"].toDouble(0.0);
    } else if (obj.contains("fuel_balance")) {
        fuelReserve = obj["fuel_balance"].toString("0").toDouble();
    }

    bool isLinked = obj["is_linked"].toBool(false);
    QString consumerId = obj["consumer_id"].toString();

    // 1. Title & Rarity Badge
    ui->lblCardTitle->setText(QString(tr("PoUS Glyph #%1 · Compute Pass")).arg(selectedEdition));
    ui->lblRarityBadge->setText(QString("[%1]").arg(tier.toUpper()));

    QString badgeColor = "#94a3b8"; // Common
    QString tierLower = tier.toLower();
    if (tierLower == "rare") {
        badgeColor = "#38bdf8";
    } else if (tierLower == "epic") {
        badgeColor = "#c084fc";
    } else if (tierLower == "legendary") {
        badgeColor = "#f59e0b";
    }
    ui->lblRarityBadge->setStyleSheet(QString("font-size: 10px; font-weight: 700; font-family: monospace; color: %1; background-color: #0f172a; border-radius: 3px; padding: 1px 4px;").arg(badgeColor));

    // 2. Onchain Status
    ui->lblOnchainStatus->setText(tr("Auto-Locked · Carrier: 10,000 sat"));

    // 3. Daily Quota & Quota Status
    ui->lblDailyQuota->setText(QString(tr("Daily Quota: $%1 / day (~%2 QVNC)"))
        .arg(QString::number(dailyQuotaUsd, 'f', 2))
        .arg(QString::number(dailyQuotaQvnc, 'f', 3)));

    double quotaRemainingQvnc = computePass["quota_remaining_qvnc"].toDouble(0.0);
    int percentAvail = dailyQuotaUsd > 0.0 ? (int)std::round((quotaRemainingUsd / dailyQuotaUsd) * 100.0) : 100;
    ui->lblQuotaStatus->setText(QString(tr("Available: $%1 (~%2 QVNC) · %3% · Resets in %4h"))
        .arg(QString::number(quotaRemainingUsd, 'f', 2))
        .arg(QString::number(quotaRemainingQvnc, 'f', 3))
        .arg(percentAvail)
        .arg(resetsInHours));

    // 4. Fuel Reserve
    ui->lblFuelReserve->setText(QString(tr("Fuel Reserve: %1 QVNC"))
        .arg(QString::number(fuelReserve, 'f', 8)));

    // 5. Link Status
    isAgentLinked = isLinked;
    rawConsumerId = consumerId;
    updateLinkStatusDisplay();
    if (isLinked) {
        ui->btnOpenWebHub->setText(tr("Open Web Hub"));
    } else {
        ui->btnOpenWebHub->setText(tr("Link to Web Hub"));
    }

    QString phase = obj.value("phase").toString("AWAKE_IDLE");
    QString previewKey = QString("%1_%2").arg(selectedEdition).arg(phase);
    if (previewKey != lastLoadedPreviewKey) {
        lastLoadedPreviewKey = previewKey;
        requestAgentPreview(selectedEdition, phase);
    }
    reply->deleteLater();
}

void GlyphsPage::requestAgentPreview(uint16_t edition, const QString &phase)
{
    if (edition == 0) return;
    QUrl url(getHubBaseUrl() + "/api/sovereign-agent/" + QString::number(edition) + "/preview.png?phase=" + phase);
    QNetworkRequest request(url);
    QNetworkReply *reply = lsaNetworkManager->get(request);
    connect(reply, SIGNAL(finished()), this, SLOT(onAgentPreviewReply()));
}

void GlyphsPage::onAgentPreviewReply()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray imgData = reply->readAll();
        QPixmap pixmap;
        if (pixmap.loadFromData(imgData)) {
            ui->lblGlyphPreview->setPixmap(pixmap.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        lastLoadedPreviewKey.clear();
    }
    reply->deleteLater();
}

void GlyphsPage::onOpenTelegramClicked()
{
    QDesktopServices::openUrl(QUrl("https://t.me/qvnc_wallet_bot"));
}

void GlyphsPage::onOpenWebHubClicked()
{
    if (selectedEdition == 0) {
        QMessageBox::warning(this, tr("Direct Hub Link"), tr("Please select a glyph in the list before linking."));
        return;
    }

    int row = ui->tableGlyphs->currentRow();
    const GlyphEntry* selectedEntry = nullptr;
    if (row >= 0 && row < (int)currentGlyphs.size() && currentGlyphs[row].edition == selectedEdition) {
        selectedEntry = &currentGlyphs[row];
    } else {
        for (size_t i = 0; i < currentGlyphs.size(); ++i) {
            if (currentGlyphs[i].edition == selectedEdition) {
                selectedEntry = &currentGlyphs[i];
                break;
            }
        }
    }

    if (!selectedEntry) {
        QMessageBox::warning(this, tr("Direct Hub Link"), tr("No glyph selected or glyph edition not found."));
        return;
    }

    // Copy required data by value to the stack to prevent Use-After-Free across modal unlock
    uint16_t edition = selectedEdition;
    QString carrierAddrQStr = selectedEntry->carrierAddress.trimmed();
    std::string carrierAddrStr = carrierAddrQStr.toStdString();
    selectedEntry = nullptr; // Invalidate raw pointer before modal operations

    if (carrierAddrStr.empty() || carrierAddrQStr == tr("(unknown)")) {
        QMessageBox::warning(this, tr("Direct Hub Link"), tr("Carrier address for this glyph is invalid or unknown."));
        return;
    }

    CTxDestination dest = DecodeDestination(carrierAddrStr);
    const CKeyID *pKeyID = boost::get<CKeyID>(&dest);
    if (!pKeyID) {
        QMessageBox::critical(this, tr("Direct Hub Link"), tr("The carrier address (%1) does not refer to a valid key.").arg(carrierAddrQStr));
        return;
    }
    CKeyID keyID = *pKeyID;

    CWallet* pwallet = (walletModel && walletModel->getWallet()) ? walletModel->getWallet() : pwalletMain;
    if (!pwallet) {
        QMessageBox::critical(this, tr("Direct Hub Link"), tr("Wallet is not available."));
        return;
    }

    if (walletModel) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            return; // Wallet unlock cancelled
        }
    }

    CKey key;
    if (!pwallet->GetKey(keyID, key)) {
        QMessageBox::critical(this, tr("Direct Hub Link"), tr("Private key for carrier address (%1) is not available in this wallet.").arg(carrierAddrQStr));
        return;
    }

    int64_t ts = GetTime();
    std::string nonce = GetRandHash().ToString().substr(0, 16);
    std::string msg = "qvnc:lsa:link:" + std::to_string(edition) + ":" + 
                      carrierAddrStr + ":" + std::to_string(ts) + ":" + nonce;

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << msg;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        QMessageBox::critical(this, tr("Direct Hub Link"), tr("Failed to generate ECDSA cryptographic signature."));
        return;
    }

    std::string sigBase64 = EncodeBase64(&vchSig[0], vchSig.size());

    QString baseUrl = getHubBaseUrl();

    QString claimUrl = QString("%1/agents/link?edition=%2&address=%3&ts=%4&nonce=%5&sig=%6")
        .arg(baseUrl)
        .arg(edition)
        .arg(QString::fromStdString(carrierAddrStr))
        .arg(ts)
        .arg(QString::fromStdString(nonce))
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(sigBase64))));

    QApplication::clipboard()->setText(claimUrl);
    QDesktopServices::openUrl(QUrl(claimUrl));

    QMessageBox::information(this, tr("Direct Hub Link"),
        tr("Direct Hub Link initialized for Glyph #%1.\n\n"
           "Opening verification gateway in browser:\n%2\n\n"
           "(Link copied to clipboard).")
        .arg(edition).arg(claimUrl));
}


void GlyphsPage::onToggleIdPrivacyClicked()
{
    showConsumerId = !showConsumerId;
    updateLinkStatusDisplay();
}

void GlyphsPage::updateLinkStatusDisplay()
{
    if (!isAgentLinked) {
        ui->lblLinkStatus->setText(tr("Telegram: Not Linked"));
        ui->lblLinkStatus->setStyleSheet("font-size: 10px; color: #475569; font-family: monospace;");
        ui->btnToggleIdPrivacy->setVisible(false);
        return;
    }

    ui->btnToggleIdPrivacy->setVisible(true);

    if (showConsumerId) {
        QString displayId = rawConsumerId.isEmpty() ? tr("tg:active") : rawConsumerId;
        ui->lblLinkStatus->setText(QString(tr("Telegram: Linked (%1)")).arg(displayId));
        ui->btnToggleIdPrivacy->setText(tr("Hide"));
        ui->btnToggleIdPrivacy->setToolTip(tr("Mask private Telegram account ID"));
    } else {
        QString maskedId;
        if (rawConsumerId.startsWith("tg:")) {
            maskedId = "tg:••••••••";
        } else if (!rawConsumerId.isEmpty()) {
            maskedId = "••••••••";
        } else {
            maskedId = "tg:••••••••";
        }
        ui->lblLinkStatus->setText(QString(tr("Telegram: Linked (%1)")).arg(maskedId));
        ui->btnToggleIdPrivacy->setText(tr("Show"));
        ui->btnToggleIdPrivacy->setToolTip(tr("Reveal private Telegram account ID"));
    }
    ui->lblLinkStatus->setStyleSheet("font-size: 10px; color: #0284c7; font-family: monospace;");
}
