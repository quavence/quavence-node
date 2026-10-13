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
class QNetworkAccessManager;
class QNetworkReply;

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
    void onCopyTxIdClicked();
    void onCopyUtxoClicked();
    void onCopyEditionClicked();
    void onCustomContextMenu(const QPoint &pos);

    // Compute Pass Slots
    void onAgentStateReply();
    void onAgentPreviewReply();
    void onOpenTelegramClicked();
    void onOpenWebHubClicked();
    void onToggleIdPrivacyClicked();

protected:
    virtual void showEvent(QShowEvent *event) override;

private:
    Ui::GlyphsPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;
    std::vector<GlyphEntry> currentGlyphs;

    // Compute Pass State & Network
    QNetworkAccessManager *lsaNetworkManager;
    uint16_t selectedEdition;
    bool showConsumerId;
    QString rawConsumerId;
    bool isAgentLinked;
    QString lastLoadedPreviewKey;

    void updateLinkStatusDisplay();

    void updateSelectionState();
    void requestAgentState(uint16_t edition);
    void requestAgentPreview(uint16_t edition, const QString &phase);
    QString getHubBaseUrl() const;
};

#endif // BITCOIN_QT_GLYPHSPAGE_H
