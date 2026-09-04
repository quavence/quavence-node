// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#ifndef BITCOIN_QT_SPLITFORSTAKINGDIALOG_H
#define BITCOIN_QT_SPLITFORSTAKINGDIALOG_H

#include "walletmodel.h"

#include <QDialog>

class CCoinControl;
class PlatformStyle;

namespace Ui {
class SplitForStakingDialog;
}

/** Dialog to split wallet balance into many outputs for PoS staking (self-send). */
class SplitForStakingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SplitForStakingDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~SplitForStakingDialog();

    void setModel(WalletModel *model);
    /** When coin control is enabled, pass the global selection (may be empty). */
    void setCoinControlContext(bool coinControlEnabled, const CCoinControl *coinControl);

public Q_SLOTS:
    void updateDisplayUnit();

private Q_SLOTS:
    void recommendButtonClicked();
    void previewButtonClicked();
    void splitButtonClicked();
    void amountFieldsChanged();
    void useAvailableBalanceClicked();
    void inputsButtonClicked();
    void sourceModeChanged();
    void destinationModeChanged();

private:
    Ui::SplitForStakingDialog *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    bool fCoinControlEnabled;
    CCoinControl *coinControlCopy;
    bool previewReady;
    QString resolvedDestination;

    CAmount availableBalance() const;
    bool estimateConservativeFee(CAmount totalAmount, CAmount &feeRet) const;
    const CCoinControl *activeCoinControl() const;
    bool usingSelectedInputs() const;
    bool usingSameAddressMode() const;
    void invalidatePreviewState(const QString &reason = QString());
    void updateSourceSummary();
    void updateDestinationSummary();

    /** Coin control preset for Split (allows other coins when Coin Control inactive). */
    void fillStakeSplitCoinControl(CCoinControl &ctrl) const;

    /** Dummy-output fee/size estimate only (no wallet keys touched). */
    bool runFeeEstimate(CAmount splitAmount, CAmount outputSize, int nOutputs,
                       CAmount &feeRet, CAmount &changeRet, unsigned int &txSizeRet,
                       QString &errorOut);

    bool parseParams(CAmount &splitAmount, CAmount &outputSize, int &nOutputs, QString &error) const;
    /** False if split would leave no mature UTXO for staking (deadlock). Sets singleUtxoPause if only one mature input. */
    bool leavesMatureStakeReserve(CAmount splitAmount, bool &singleUtxoPause, QString &error) const;
    bool selectedInputsPassSafetyChecks(QString &warningOrError) const;
    bool selectedInputsResolveSingleDestination(QString &destinationOut, QString &errorOut) const;

    void updateFeeLabels(CAmount fee, CAmount change, unsigned int txSize);
    void updatePreviewSummaryVisibility(bool showSummary);
    void showError(const QString &title, const QString &text);
};

#endif // BITCOIN_QT_SPLITFORSTAKINGDIALOG_H
