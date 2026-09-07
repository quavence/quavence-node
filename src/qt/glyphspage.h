// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#ifndef BITCOIN_QT_GLYPHSPAGE_H
#define BITCOIN_QT_GLYPHSPAGE_H

#include <QWidget>
#include <vector>
#include "uint256.h"

class ClientModel;
class WalletModel;
class PlatformStyle;
class QShowEvent;

namespace Ui {
class GlyphsPage;
}

struct GlyphEntry {
    uint16_t edition;
    uint256 glyphHash;
    QString carrierAddress;
    uint256 txid;
    unsigned int vout;
    int confirmations;
    bool isLocked;
};

class GlyphsPage : public QWidget
{
    Q_OBJECT

public:
    explicit GlyphsPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~GlyphsPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

public Q_SLOTS:
    void updateGlyphs();
    void onTransferClicked();
    void onExplorerClicked();
    void onRefreshClicked();
    void onTableSelectionChanged();
    void onTableDoubleClicked(int row, int column);
    void onCopyHashClicked();
    void onCopyAddressClicked();

protected:
    virtual void showEvent(QShowEvent *event) override;

private:
    Ui::GlyphsPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;
    std::vector<GlyphEntry> currentGlyphs;

    void updateSelectionState();
};

#endif // BITCOIN_QT_GLYPHSPAGE_H
