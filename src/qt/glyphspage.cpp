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

#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QUrl>

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
    QStringList headers;
    headers << tr("Edition")
            << tr("Glyph Hash")
            << tr("Carrier Address")
            << tr("Carrier UTXO")
            << tr("Protection")
            << tr("Confirmations");
    ui->tableGlyphs->setHorizontalHeaderLabels(headers);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->tableGlyphs->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

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

    updateGlyphs();
}

GlyphsPage::~GlyphsPage()
{
    delete ui;
}

void GlyphsPage::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void GlyphsPage::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
    if (walletModel) {
        connect(walletModel, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,unsigned int)),
                this, SLOT(updateGlyphs()));
    }
    updateGlyphs();
}

void GlyphsPage::updateGlyphs()
{
    currentGlyphs.clear();

    if (!walletModel || !walletModel->getWallet()) {
        ui->stackedWidget->setCurrentIndex(0);
        ui->labelTotalCount->setText(tr("<b>Owned Glyphs:</b> 0"));
        return;
    }

    CWallet *wallet = walletModel->getWallet();
    {
        LOCK2(cs_main, wallet->cs_wallet);

        for (const auto& entry : wallet->mapWallet) {
            const CWalletTx& wtx = entry.second;
            int nDepth = wtx.GetDepthInMainChain();
            if (nDepth < 0) continue; // In conflict/dead branch

            for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
                if (wtx.vout[i].nValue == GLYPH_CARRIER_DUST && wallet->IsMine(wtx.vout[i])) {
                    if (wallet->IsSpent(entry.first, i)) {
                        continue; // Already spent
                    }

                    GlyphCarrierRecord glyphRec;
                    if (GetTxGlyphCarrier(wtx, i, glyphRec)) {
                        GlyphEntry item;
                        item.edition = glyphRec.edition;
                        item.glyphHash = glyphRec.glyphHash;
                        item.txid = entry.first;
                        item.vout = i;
                        item.confirmations = nDepth;
                        item.isLocked = wallet->IsLockedCoin(entry.first, i);

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

    // Sort by edition
    std::sort(currentGlyphs.begin(), currentGlyphs.end(), [](const GlyphEntry& a, const GlyphEntry& b) {
        return a.edition < b.edition;
    });

    // Populate table
    ui->tableGlyphs->setRowCount(0);
    for (size_t row = 0; row < currentGlyphs.size(); ++row) {
        const GlyphEntry& g = currentGlyphs[row];
        ui->tableGlyphs->insertRow(row);

        // Edition
        QString editionStr = QString("💎 #%1").arg(g.edition);
        QTableWidgetItem *itemEdition = new QTableWidgetItem(editionStr);
        itemEdition->setFont(QFont(font().family(), 10, QFont::Bold));
        ui->tableGlyphs->setItem(row, 0, itemEdition);

        // Hash
        std::string hashHex = g.glyphHash.ToString();
        QString hashShort = QString::fromStdString(hashHex.substr(0, 10) + "..." + hashHex.substr(hashHex.length() - 8));
        QTableWidgetItem *itemHash = new QTableWidgetItem(hashShort);
        itemHash->setToolTip(QString::fromStdString(hashHex));
        ui->tableGlyphs->setItem(row, 1, itemHash);

        // Carrier Address
        QTableWidgetItem *itemAddress = new QTableWidgetItem(g.carrierAddress);
        itemAddress->setToolTip(g.carrierAddress);
        ui->tableGlyphs->setItem(row, 2, itemAddress);

        // Carrier UTXO
        QString utxoStr = QString("%1:%2")
            .arg(QString::fromStdString(g.txid.ToString().substr(0, 8) + "..."))
            .arg(g.vout);
        QTableWidgetItem *itemUtxo = new QTableWidgetItem(utxoStr);
        itemUtxo->setToolTip(QString("%1:%2").arg(QString::fromStdString(g.txid.ToString())).arg(g.vout));
        ui->tableGlyphs->setItem(row, 3, itemUtxo);

        // Protection
        QString lockStr = g.isLocked ? tr("🔒 Auto-Locked") : tr("🔓 Unlocked");
        QTableWidgetItem *itemLock = new QTableWidgetItem(lockStr);
        itemLock->setToolTip(tr("PoUS Staking & Spending Immunity Active"));
        ui->tableGlyphs->setItem(row, 4, itemLock);

        // Confirmations
        QTableWidgetItem *itemConf = new QTableWidgetItem(QString::number(g.confirmations));
        ui->tableGlyphs->setItem(row, 5, itemConf);
    }

    // Update stats & stack view
    size_t count = currentGlyphs.size();
    ui->labelTotalCount->setText(tr("<b>Owned Glyphs:</b> %1").arg(count));
    if (count == 0) {
        ui->stackedWidget->setCurrentIndex(0);
    } else {
        ui->stackedWidget->setCurrentIndex(1);
    }

    updateSelectionState();
}

void GlyphsPage::onTableSelectionChanged()
{
    updateSelectionState();
}

void GlyphsPage::updateSelectionState()
{
    int row = ui->tableGlyphs->currentRow();
    bool hasSelection = (row >= 0 && row < (int)currentGlyphs.size());
    ui->btnTransfer->setEnabled(hasSelection);
    ui->btnExplorerRow->setEnabled(hasSelection);
    ui->btnCopyHash->setEnabled(hasSelection);
    ui->btnCopyAddress->setEnabled(hasSelection);
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
        QString url = QString("https://explorer.quavence.com/glyphs/%1").arg(g.edition);
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
