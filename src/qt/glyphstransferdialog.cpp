// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#include "glyphstransferdialog.h"
#include "ui_glyphstransferdialog.h"

#include "addressbookpage.h"
#include "airegistry.h"
#include "coincontrol.h"
#include "dstencode.h"
#include "guiutil.h"
#include "main.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "wallet/wallet.h"

#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

GlyphTransferDialog::GlyphTransferDialog(const PlatformStyle *_platformStyle,
                                         uint16_t edition,
                                         const uint256 &glyphHash,
                                         const uint256 &carrierTxid,
                                         unsigned int carrierVout,
                                         const QString &currentAddress,
                                         QWidget *parent) :
    QDialog(parent),
    ui(new Ui::GlyphTransferDialog),
    platformStyle(_platformStyle),
    walletModel(nullptr),
    m_edition(edition),
    m_glyphHash(glyphHash),
    m_carrierTxid(carrierTxid),
    m_carrierVout(carrierVout),
    m_currentAddress(currentAddress)
{
    ui->setupUi(this);

    ui->lblEditionVal->setText(QString("PoUS Edition #%1").arg(m_edition));
    ui->lblHashVal->setText(QString::fromStdString(m_glyphHash.ToString()));
    ui->lblCarrierVal->setText(QString("%1:%2 (10,000 satoshi)").arg(QString::fromStdString(m_carrierTxid.ToString())).arg(m_carrierVout));

    connect(ui->lineEditRecipient, SIGNAL(textChanged(QString)), this, SLOT(onRecipientChanged(QString)));
    connect(ui->btnPaste, SIGNAL(clicked()), this, SLOT(onPasteClicked()));
    connect(ui->btnAddressBook, SIGNAL(clicked()), this, SLOT(onAddressBookClicked()));
    connect(ui->btnTransfer, SIGNAL(clicked()), this, SLOT(onTransferClicked()));
    connect(ui->btnCancel, SIGNAL(clicked()), this, SLOT(reject()));
}

GlyphTransferDialog::~GlyphTransferDialog()
{
    delete ui;
}

void GlyphTransferDialog::setWalletModel(WalletModel *_walletModel)
{
    this->walletModel = _walletModel;
}

void GlyphTransferDialog::onRecipientChanged(const QString &text)
{
    bool valid = false;
    if (walletModel && !text.trimmed().isEmpty()) {
        valid = walletModel->validateAddress(text.trimmed());
    }
    ui->btnTransfer->setEnabled(valid);
}

void GlyphTransferDialog::onPasteClicked()
{
    ui->lineEditRecipient->setText(QApplication::clipboard()->text().trimmed());
}

void GlyphTransferDialog::onAddressBookClicked()
{
    if (!walletModel || !walletModel->getAddressTableModel())
        return;

    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(walletModel->getAddressTableModel());
    if (dlg.exec()) {
        ui->lineEditRecipient->setText(dlg.getReturnValue());
    }
}

void GlyphTransferDialog::onTransferClicked()
{
    if (!walletModel || !walletModel->getWallet()) {
        QMessageBox::critical(this, tr("Error"), tr("Wallet is not available."));
        return;
    }

    QString recipientAddress = ui->lineEditRecipient->text().trimmed();
    if (!walletModel->validateAddress(recipientAddress)) {
        QMessageBox::warning(this, tr("Invalid Address"),
            tr("Please enter a valid Quavence recipient address."));
        return;
    }

    QString confirmText = tr("Are you sure you want to transfer PoUS Glyph Edition #%1 to:\n\n%2\n\n"
                             "This will send the 10,000 sat carrier UTXO with the 40-byte OP_RETURN transfer payload.")
        .arg(m_edition)
        .arg(recipientAddress);

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        tr("Confirm Glyph Transfer"), confirmText, QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    // Request unlock if wallet is encrypted
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        return; // Cancelled or incorrect passphrase
    }

    CWallet *wallet = walletModel->getWallet();
    COutPoint carrierOutpoint(m_carrierTxid, m_carrierVout);

    // Build recipient scripts
    CTxDestination dest = DecodeDestination(recipientAddress.toStdString());
    CScript scriptCarrier = GetScriptForDestination(dest);

    // 40-byte OP_RETURN payload: 'QVNC', 0x01, 0x03, 32-byte glyphHash, 2-byte edition (LE)
    std::vector<unsigned char> vData;
    vData.reserve(40);
    vData.push_back('Q');
    vData.push_back('V');
    vData.push_back('N');
    vData.push_back('C');
    vData.push_back(0x01); // version
    vData.push_back(0x03); // opType TRANSFER
    for (unsigned int b = 0; b < 32; ++b) {
        vData.push_back(m_glyphHash.begin()[b]);
    }
    vData.push_back(m_edition & 0xFF);
    vData.push_back((m_edition >> 8) & 0xFF);

    CScript scriptOpReturn = CScript() << OP_RETURN << vData;

    std::vector<CRecipient> vecSend;
    CRecipient rCarrier = {scriptCarrier, GLYPH_CARRIER_DUST, false};
    CRecipient rOpReturn = {scriptOpReturn, 0, false};
    vecSend.push_back(rCarrier);
    vecSend.push_back(rOpReturn);

    CCoinControl coinControl;
    coinControl.Select(carrierOutpoint);
    coinControl.fAllowOtherInputs = true; // Allow fee funding from wallet

    CWalletTx wtxNew;
    CReserveKey reservekey(wallet);
    CAmount nFeeRet = 0;
    int nChangePosRet = -1;
    std::string strFailReason;

    bool wasLocked = false;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        wasLocked = wallet->IsLockedCoin(carrierOutpoint.hash, carrierOutpoint.n);
        if (wasLocked) {
            wallet->UnlockCoin(carrierOutpoint);
        }
    }

    bool fCreated = wallet->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, nChangePosRet, strFailReason, &coinControl);

    if (!fCreated) {
        if (wasLocked) {
            LOCK2(cs_main, wallet->cs_wallet);
            wallet->LockCoin(carrierOutpoint);
        }
        QMessageBox::critical(this, tr("Transfer Failed"),
            tr("Failed to create transaction: %1").arg(QString::fromStdString(strFailReason)));
        return;
    }

    bool fCommitted = false;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        fCommitted = wallet->CommitTransaction(wtxNew, reservekey);
    }

    if (!fCommitted || !wtxNew.InMempool()) {
        if (wasLocked) {
            LOCK2(cs_main, wallet->cs_wallet);
            wallet->LockCoin(carrierOutpoint);
        }
        {
            LOCK2(cs_main, wallet->cs_wallet);
            wallet->AbandonTransaction(wtxNew.GetHash());
        }
        QMessageBox::critical(this, tr("Broadcast Failed"),
            tr("Transaction was rejected by the network mempool. The unconfirmed transaction has been automatically cancelled."));
        return;
    }

    QString txidStr = QString::fromStdString(wtxNew.GetHash().ToString());
    QMessageBox::information(this, tr("Transfer Broadcasted"),
        tr("PoUS Glyph Edition #%1 has been successfully transferred!\n\nTxID: %2\nFee paid: %3 QVNC")
            .arg(m_edition)
            .arg(txidStr)
            .arg(QString::number(double(nFeeRet) / COIN, 'f', 8)));

    accept();
}
