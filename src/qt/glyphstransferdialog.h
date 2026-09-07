// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#ifndef BITCOIN_QT_GLYPHTRANSFERDIALOG_H
#define BITCOIN_QT_GLYPHTRANSFERDIALOG_H

#include <QDialog>
#include "uint256.h"

class PlatformStyle;
class WalletModel;

namespace Ui {
class GlyphTransferDialog;
}

class GlyphTransferDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlyphTransferDialog(const PlatformStyle *platformStyle,
                                 uint16_t edition,
                                 const uint256 &glyphHash,
                                 const uint256 &carrierTxid,
                                 unsigned int carrierVout,
                                 const QString &currentAddress,
                                 QWidget *parent = nullptr);
    ~GlyphTransferDialog();

    void setWalletModel(WalletModel *walletModel);

private Q_SLOTS:
    void onTransferClicked();
    void onPasteClicked();
    void onAddressBookClicked();
    void onRecipientChanged(const QString &text);

private:
    Ui::GlyphTransferDialog *ui;
    const PlatformStyle *platformStyle;
    WalletModel *walletModel;

    uint16_t m_edition;
    uint256 m_glyphHash;
    uint256 m_carrierTxid;
    unsigned int m_carrierVout;
    QString m_currentAddress;
};

#endif // BITCOIN_QT_GLYPHTRANSFERDIALOG_H
