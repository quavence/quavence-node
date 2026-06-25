// Copyright (c) 2026 The Quavence developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "splitforstakingdialog.h"
#include "ui_splitforstakingdialog.h"

#include "bitcoinunits.h"
#include "coincontrol.h"
#include "coincontroldialog.h"
#include "dstencode.h"
#include "main.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "wallet/wallet.h"

#include "policy/policy.h"
#include "script/script.h"
#include "script/standard.h"

#include <QMessageBox>
#include <QSignalBlocker>

#include <set>

namespace {

static const int MAX_OUTPUTS_DEFAULT = 100;
static const int MAX_OUTPUTS_HARD = 200;
static const int TARGET_OUTPUTS_RECOMMEND = 35;
/** Same as CWallet::GetStakeCombineThreshold() — keep at least this much mature for staking. */
static const CAmount MIN_MATURE_STAKE_RESERVE = 500 * COIN;

CAmount NiceRoundUp(CAmount raw)
{
    static const CAmount chunks[] = {
        COIN / 10,
        COIN / 4,
        COIN / 2,
        COIN,
        2 * COIN,
        5 * COIN,
        10 * COIN,
        25 * COIN,
        50 * COIN,
        100 * COIN,
        250 * COIN,
        500 * COIN,
    };
    for (CAmount c : chunks) {
        if (c >= raw)
            return c;
    }
    return 500 * COIN;
}

CAmount MinOutputAmount()
{
    std::vector<unsigned char> vch(20, 0);
    CScript script = CScript() << OP_DUP << OP_HASH160 << vch << OP_EQUALVERIFY << OP_CHECKSIG;
    CTxOut txOut(0, script);
    return txOut.GetDustThreshold(::minRelayTxFee) + 1;
}

} // namespace

SplitForStakingDialog::SplitForStakingDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SplitForStakingDialog),
    model(0),
    platformStyle(_platformStyle),
    fCoinControlEnabled(false),
    coinControlCopy(0),
    previewReady(false)
{
    ui->setupUi(this);

    connect(ui->useAvailableBalance, SIGNAL(clicked()), this, SLOT(useAvailableBalanceClicked()));
    connect(ui->recommendButton, SIGNAL(clicked()), this, SLOT(recommendButtonClicked()));
    connect(ui->previewButton, SIGNAL(clicked()), this, SLOT(previewButtonClicked()));
    connect(ui->splitButton, SIGNAL(clicked()), this, SLOT(splitButtonClicked()));
    connect(ui->splitAmount, SIGNAL(valueChanged()), this, SLOT(amountFieldsChanged()));
    connect(ui->outputSize, SIGNAL(valueChanged()), this, SLOT(amountFieldsChanged()));
    connect(ui->outputCount, SIGNAL(valueChanged(int)), this, SLOT(amountFieldsChanged()));
    connect(ui->inputsButton, SIGNAL(clicked()), this, SLOT(inputsButtonClicked()));
    connect(ui->radioSourceAutomatic, SIGNAL(toggled(bool)), this, SLOT(sourceModeChanged()));
    connect(ui->radioSourceSelected, SIGNAL(toggled(bool)), this, SLOT(sourceModeChanged()));
    connect(ui->radioDestSameAddress, SIGNAL(toggled(bool)), this, SLOT(destinationModeChanged()));
    connect(ui->radioDestNewInternal, SIGNAL(toggled(bool)), this, SLOT(destinationModeChanged()));

    ui->splitButton->setEnabled(false);
    updatePreviewSummaryVisibility(false);
    updateSourceSummary();
    updateDestinationSummary();
}

SplitForStakingDialog::~SplitForStakingDialog()
{
    delete coinControlCopy;
    delete ui;
}

void SplitForStakingDialog::setModel(WalletModel *_model)
{
    model = _model;
    if (!model)
        return;

    updateDisplayUnit();
    updateSourceSummary();
    invalidatePreviewState();
}

void SplitForStakingDialog::setCoinControlContext(bool coinControlEnabled, const CCoinControl *coinControl)
{
    fCoinControlEnabled = coinControlEnabled;
    delete coinControlCopy;
    coinControlCopy = 0;
    if (fCoinControlEnabled && coinControl) {
        coinControlCopy = new CCoinControl(*coinControl);
    }
    ui->radioSourceAutomatic->setEnabled(fCoinControlEnabled);
    ui->radioSourceSelected->setEnabled(fCoinControlEnabled);
    ui->inputsButton->setEnabled(fCoinControlEnabled);
    if (!fCoinControlEnabled) {
        ui->radioSourceAutomatic->setChecked(true);
    }
    updateSourceSummary();
    invalidatePreviewState();
}

void SplitForStakingDialog::updateDisplayUnit()
{
    if (!model || !model->getOptionsModel())
        return;
    int unit = model->getOptionsModel()->getDisplayUnit();
    ui->splitAmount->setDisplayUnit(unit);
    ui->outputSize->setDisplayUnit(unit);
}

CAmount SplitForStakingDialog::availableBalance() const
{
    if (!model)
        return 0;
    const CCoinControl *ctrl = activeCoinControl();
    if (ctrl)
        return model->getBalance(ctrl);
    return model->getBalance();
}

bool SplitForStakingDialog::estimateConservativeFee(CAmount totalAmount, CAmount &feeRet) const
{
    feeRet = 0;
    if (!model || totalAmount <= 0)
        return false;

    int nInputs = 5;
    if (usingSelectedInputs() && coinControlCopy && coinControlCopy->HasSelected()) {
        std::vector<COutPoint> selected;
        coinControlCopy->ListSelected(selected);
        nInputs = selected.size();
    }
    if (nInputs < 1)
        nInputs = 1;

    int nOutputs = ui->outputCount->value();
    CAmount outSize = ui->outputSize->value();
    if (outSize > 0) {
        nOutputs = (int)(totalAmount / outSize);
    }
    if (nOutputs < 1)
        nOutputs = TARGET_OUTPUTS_RECOMMEND;
    if (nOutputs > MAX_OUTPUTS_HARD)
        nOutputs = MAX_OUTPUTS_HARD;

    unsigned int nBytes = 148 * nInputs + 34 * (nOutputs + 1) + 10;
    CFeeRate feeRate = payTxFee;
    if (feeRate < ::minRelayTxFee)
        feeRate = ::minRelayTxFee;
    if (feeRate <= CFeeRate(0))
        feeRate = CFeeRate(1000); // 1000 satoshis/kB safety fallback

    feeRet = feeRate.GetFee(nBytes);
    return feeRet > 0;
}

const CCoinControl *SplitForStakingDialog::activeCoinControl() const
{
    if (fCoinControlEnabled && usingSelectedInputs() && coinControlCopy && coinControlCopy->HasSelected())
        return coinControlCopy;
    return NULL;
}

bool SplitForStakingDialog::usingSelectedInputs() const
{
    return fCoinControlEnabled && ui->radioSourceSelected->isChecked();
}

void SplitForStakingDialog::invalidatePreviewState(const QString &reason)
{
    previewReady = false;
    resolvedDestination.clear();
    ui->splitButton->setEnabled(false);
    if (!reason.isEmpty())
        ui->labelSplitPreviewNote->setText(reason);
    updateDestinationSummary();
    updatePreviewSummaryVisibility(false);
}

void SplitForStakingDialog::updateSourceSummary()
{
    if (!model || !model->getOptionsModel()) {
        ui->labelSourceSummary->setText(tr("Automatic coin selection"));
        return;
    }

    int unit = model->getOptionsModel()->getDisplayUnit();
    if (usingSelectedInputs() && coinControlCopy && coinControlCopy->HasSelected()) {
        std::vector<COutPoint> selected;
        coinControlCopy->ListSelected(selected);
        std::vector<COutput> selectedOutputs;
        model->getOutputs(selected, selectedOutputs);
        CAmount selectedTotal = 0;
        for (const COutput &out : selectedOutputs)
            selectedTotal += out.tx->vout[out.i].nValue;
        QString inputStr = selectedOutputs.size() == 1 ? tr("1 selected input") : tr("%1 selected inputs").arg(selectedOutputs.size());
        ui->labelSourceSummary->setText(
            tr("%1 · %2")
                .arg(inputStr)
                .arg(BitcoinUnits::formatWithUnit(unit, selectedTotal)));
        return;
    }

    if (usingSelectedInputs()) {
        ui->labelSourceSummary->setText(
            tr("No inputs selected, automatic coin selection will be used"));
        return;
    }

    ui->labelSourceSummary->setText(tr("Automatic coin selection"));
}

bool SplitForStakingDialog::usingSameAddressMode() const
{
    return ui->radioDestSameAddress->isChecked();
}

void SplitForStakingDialog::updateDestinationSummary()
{
    if (!usingSameAddressMode()) {
        ui->labelDestinationSummary->setText(tr("New internal wallet addresses"));
        return;
    }
    if (!resolvedDestination.isEmpty()) {
        QString shortened = resolvedDestination.left(6) + "..." + resolvedDestination.right(6);
        ui->labelDestinationSummary->setText(tr("Source staking address (%1)").arg(shortened));
        return;
    }

    QString selectedDest;
    QString selectedErr;
    if (selectedInputsResolveSingleDestination(selectedDest, selectedErr) && !selectedDest.isEmpty()) {
        QString shortened = selectedDest.left(6) + "..." + selectedDest.right(6);
        ui->labelDestinationSummary->setText(tr("Source staking address (%1)").arg(shortened));
        return;
    }
    ui->labelDestinationSummary->setText(tr("Source staking address"));
}

void SplitForStakingDialog::fillStakeSplitCoinControl(CCoinControl &ctrl) const
{
    const CCoinControl *active = activeCoinControl();
    if (active)
        ctrl = *active;
    ctrl.fAllowOtherInputs = (active == NULL);
}

bool SplitForStakingDialog::runFeeEstimate(CAmount splitAmount, CAmount outputSize, int nOutputs,
    CAmount &feeRet, CAmount &changeRet, unsigned int &txSizeRet, QString &errorOut)
{
    feeRet = 0;
    changeRet = 0;
    txSizeRet = 0;
    errorOut.clear();

    if (!model) {
        errorOut = tr("Wallet not loaded.");
        return false;
    }

    CCoinControl ctrl;
    fillStakeSplitCoinControl(ctrl);

    QString probe;
    WalletModel::SendCoinsReturn st = model->estimateStakeSplitFees(
        nOutputs, outputSize, &ctrl, usingSameAddressMode(), feeRet, txSizeRet, probe, resolvedDestination);

    if (st.status != WalletModel::OK) {
        if (!probe.isEmpty()) {
            errorOut = probe;
        } else {
            switch (st.status) {
            case WalletModel::AmountExceedsBalance:
                if (activeCoinControl()) {
                    errorOut = tr("Amount plus fee exceeds selected coins. Use available or reduce amount.");
                } else {
                    errorOut = tr("Amount plus fee exceeds available balance. Use available or reduce amount.");
                }
                break;
            case WalletModel::AmountWithFeeExceedsBalance:
                if (activeCoinControl()) {
                    errorOut = tr("Amount plus fee exceeds selected coins. Use available or reduce amount.");
                } else {
                    errorOut = tr("Amount plus fee exceeds available balance. Use available or reduce amount.");
                }
                break;
            case WalletModel::TransactionCreationFailed:
                errorOut = tr("Could not build transaction. Try fewer outputs or a larger output size.");
                break;
            case WalletModel::AbsurdFee:
                errorOut = tr("Calculated fee is higher than configured maximum.");
                break;
            default:
                errorOut = tr("Fee estimation failed.");
                break;
            }
        }
        return false;
    }

    const CAmount totalOut = outputSize * static_cast<CAmount>(nOutputs);
    changeRet = splitAmount - totalOut - feeRet;
    if (changeRet < 0)
        changeRet = 0;

    return true;
}

void SplitForStakingDialog::useAvailableBalanceClicked()
{
    if (!model)
        return;
    CAmount avail = availableBalance();
    CAmount fee = 0;
    if (estimateConservativeFee(avail, fee) && fee > 0 && avail > fee) {
        ui->splitAmount->setValue(avail - fee);
    } else {
        showError(tr("Split for Staking"), tr("Cannot use full balance because fee cannot be reserved."));
    }
}

void SplitForStakingDialog::recommendButtonClicked()
{
    if (!model)
        return;

    CAmount avail = availableBalance();
    if (avail <= 0) {
        showError(tr("Split for Staking"), tr("No spendable balance available."));
        return;
    }

    CAmount fee = 0;
    if (!estimateConservativeFee(avail, fee) || fee <= 0 || avail <= fee) {
        showError(tr("Split for Staking"), tr("Available balance is too small to cover transaction fees."));
        return;
    }
    CAmount targetSplit = avail - fee;

    CAmount rawChunk = targetSplit / TARGET_OUTPUTS_RECOMMEND;
    if (rawChunk <= 0)
        rawChunk = COIN / 10;

    CAmount chunk = NiceRoundUp(rawChunk);
    CAmount minOut = MinOutputAmount();
    if (chunk < minOut)
        chunk = minOut;

    int nOutputs = (int)(targetSplit / chunk);
    if (nOutputs < 1)
        nOutputs = 1;
    if (nOutputs > MAX_OUTPUTS_DEFAULT)
        nOutputs = MAX_OUTPUTS_DEFAULT;

    chunk = targetSplit / nOutputs;
    chunk = NiceRoundUp(chunk);
    if (chunk < minOut)
        chunk = minOut;
    nOutputs = (int)(targetSplit / chunk);
    if (nOutputs > MAX_OUTPUTS_DEFAULT)
        nOutputs = MAX_OUTPUTS_DEFAULT;

    ui->splitAmount->setValue(targetSplit);
    ui->outputSize->setValue(chunk);
    ui->outputCount->setValue(nOutputs);
    previewButtonClicked();
}

void SplitForStakingDialog::amountFieldsChanged()
{
    invalidatePreviewState(
        tr("Preview does not create addresses. New internal addresses are reserved only when you confirm Split."));

    if (!model)
        return;

    CAmount splitAmount = ui->splitAmount->value();
    CAmount outputSize = ui->outputSize->value();
    if (splitAmount > 0 && outputSize > 0) {
        int n = (int)(splitAmount / outputSize);
        if (n < 1)
            n = 1;
        if (n > MAX_OUTPUTS_HARD)
            n = MAX_OUTPUTS_HARD;
        const QSignalBlocker blocker(ui->outputCount);
        ui->outputCount->setValue(n);
    }
}

void SplitForStakingDialog::sourceModeChanged()
{
    updateSourceSummary();
    updateDestinationSummary();
    invalidatePreviewState(
        tr("Source changed. Run Preview again to enable Split."));
}

void SplitForStakingDialog::destinationModeChanged()
{
    updateDestinationSummary();
    invalidatePreviewState(
        tr("Destination mode changed. Run Preview again to enable Split."));
}

void SplitForStakingDialog::inputsButtonClicked()
{
    if (!model || !fCoinControlEnabled)
        return;

    CCoinControl backup = coinControlCopy ? *coinControlCopy : CCoinControl();
    if (coinControlCopy)
        *CoinControlDialog::coinControl = *coinControlCopy;
    else
        CoinControlDialog::coinControl->UnSelectAll();

    CoinControlDialog dlg(platformStyle, this);
    dlg.setModel(model);
    if (dlg.exec() == QDialog::Accepted) {
        delete coinControlCopy;
        coinControlCopy = new CCoinControl(*CoinControlDialog::coinControl);

        if (coinControlCopy && coinControlCopy->HasSelected()) {
            std::vector<COutPoint> vCoinControl;
            std::vector<COutput> vOutputs;
            coinControlCopy->ListSelected(vCoinControl);
            model->getOutputs(vCoinControl, vOutputs);

            CAmount nAmount = 0;
            BOOST_FOREACH(const COutput& out, vOutputs) {
                COutPoint outpt(out.tx->GetHash(), out.i);
                if (!model->isSpent(outpt)) {
                    nAmount += out.tx->vout[out.i].nValue;
                }
            }

            if (nAmount > 0) {
                CAmount fee = 0;
                if (estimateConservativeFee(nAmount, fee) && fee > 0 && nAmount > fee) {
                    ui->splitAmount->setValue(nAmount - fee);
                    ui->radioSourceSelected->setChecked(true);
                } else {
                    showError(tr("Split for Staking"), tr("Cannot estimate fee for selected coins. Enter a smaller amount or set output size."));
                }
            }
        }
    } else {
        *CoinControlDialog::coinControl = backup;
    }

    updateSourceSummary();
    invalidatePreviewState(
        tr("Inputs changed. Run Preview again to enable Split."));
}

bool SplitForStakingDialog::parseParams(CAmount &splitAmount, CAmount &outputSize, int &nOutputs,
    QString &error) const
{
    if (!model) {
        error = tr("Wallet not loaded.");
        return false;
    }

    // Pre-PoS bootstrap guard: forbid Split until at least block 1 is on chain.
    {
        int tipHeight = chainActive.Height();
        if (tipHeight < 1) {
            error = tr("Split is disabled until the first block is produced.");
            return false;
        }
    }

    splitAmount = ui->splitAmount->value();
    outputSize = ui->outputSize->value();
    nOutputs = ui->outputCount->value();

    if (splitAmount <= 0) {
        error = tr("Enter an amount to split.");
        return false;
    }
    if (outputSize <= 0) {
        error = tr("Enter output size or click Recommend.");
        return false;
    }
    if (nOutputs < 1 || nOutputs > MAX_OUTPUTS_HARD) {
        error = tr("Number of outputs must be between 1 and %1.").arg(MAX_OUTPUTS_HARD);
        return false;
    }
    if (nOutputs > MAX_OUTPUTS_DEFAULT) {
        error = tr("More than %1 outputs is not recommended. Reduce the count or increase output size.")
            .arg(MAX_OUTPUTS_DEFAULT);
        return false;
    }

    CAmount totalOut = outputSize * nOutputs;
    if (totalOut > splitAmount) {
        error = tr("Total output amount (%1) exceeds the split amount (%2).")
            .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), totalOut),
                 BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), splitAmount));
        return false;
    }

    CAmount minOut = MinOutputAmount();
    if (outputSize < minOut) {
        error = tr("Output size is below the dust threshold (%1).")
            .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), minOut));
        return false;
    }

    CAmount avail = availableBalance();
    if (splitAmount > avail) {
        error = tr("Split amount exceeds available balance.");
        return false;
    }

    if (activeCoinControl() && splitAmount > avail) {
        error = tr("Selected coins do not cover the split amount.");
        return false;
    }

    return true;
}

bool SplitForStakingDialog::leavesMatureStakeReserve(CAmount splitAmount, bool &singleUtxoPause,
    QString &error) const
{
    singleUtxoPause = false;
    if (!model)
        return true;

    std::vector<COutput> vCoins;
    model->listMatureStakingCoins(vCoins);

    if (vCoins.empty()) {
        error = tr("No mature staking UTXOs available.");
        return false;
    }

    CAmount totalMatureStake = 0;
    for (const COutput& out : vCoins)
        totalMatureStake += out.tx->vout[out.i].nValue;

    const CCoinControl *active = activeCoinControl();
    if (fCoinControlEnabled && active && active->HasSelected()) {
        int nUnselected = 0;
        CAmount unselectedValue = 0;
        for (const COutput& out : vCoins) {
            const COutPoint op(out.tx->GetHash(), out.i);
            if (active->IsSelected(op))
                continue;
            nUnselected++;
            unselectedValue += out.tx->vout[out.i].nValue;
        }
        if (nUnselected > 0 && unselectedValue >= MIN_MATURE_STAKE_RESERVE)
            return true;
        error = tr("In Coin Control, leave at least one mature UTXO unchecked (combined value at least %1).")
            .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), MIN_MATURE_STAKE_RESERVE));
        return false;
    }

    if (splitAmount >= totalMatureStake) {
        error = tr("Split amount is too high. Leave at least one mature staking UTXO path untouched.");
        return false;
    }

    if (vCoins.size() >= 2) {
        CAmount avail = availableBalance();
        if (splitAmount <= avail - MIN_MATURE_STAKE_RESERVE)
            return true;
        error = tr("Split amount is too high. Keep at least %1 in mature UTXOs so staking can continue.")
            .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), MIN_MATURE_STAKE_RESERVE));
        return false;
    }

    singleUtxoPause = true;
    CAmount avail = availableBalance();
    if (splitAmount >= avail) {
        error = tr("Cannot split the entire balance in one transaction.");
        return false;
    }
    return true;
}

bool SplitForStakingDialog::selectedInputsPassSafetyChecks(QString &warningOrError) const
{
    warningOrError.clear();

    if (!usingSelectedInputs() || !coinControlCopy || !coinControlCopy->HasSelected())
        return true;

    std::vector<COutPoint> selected;
    coinControlCopy->ListSelected(selected);
    std::vector<COutput> selectedOutputs;
    model->getOutputs(selected, selectedOutputs);
    if (selectedOutputs.empty())
        return true;

    std::set<COutPoint> matureStakeSet;
    std::vector<COutput> matureStakeCoins;
    model->listMatureStakingCoins(matureStakeCoins);
    for (const COutput &out : matureStakeCoins)
        matureStakeSet.insert(COutPoint(out.tx->GetHash(), out.i));

    bool hasNonMatureOrRecent = false;
    for (const COutput &out : selectedOutputs) {
        const COutPoint op(out.tx->GetHash(), out.i);
        if (matureStakeSet.count(op) == 0) {
            hasNonMatureOrRecent = true;
            break;
        }
    }

    if (hasNonMatureOrRecent) {
        warningOrError = tr("Selected inputs include immature or recently staked coins. They may not contribute staking weight until matured.");
    }

    return true;
}

bool SplitForStakingDialog::selectedInputsResolveSingleDestination(QString &destinationOut, QString &errorOut) const
{
    destinationOut.clear();
    errorOut.clear();
    if (!usingSelectedInputs() || !coinControlCopy || !coinControlCopy->HasSelected())
        return true;

    std::vector<COutPoint> selected;
    coinControlCopy->ListSelected(selected);
    std::vector<COutput> selectedOutputs;
    model->getOutputs(selected, selectedOutputs);
    if (selectedOutputs.empty()) {
        errorOut = tr("No selected inputs available.");
        return false;
    }

    bool haveDest = false;
    CTxDestination singleDest;
    for (const COutput &out : selectedOutputs) {
        const CTxOut &txout = out.tx->vout[out.i];
        CTxDestination dest;
        if (!ExtractDestination(txout.scriptPubKey, dest)) {
            errorOut = tr("Same-address split requires selected inputs from one standard address.");
            return false;
        }
        if (!haveDest) {
            singleDest = dest;
            haveDest = true;
        } else if (!(dest == singleDest)) {
            errorOut = tr("Same address split requires selected inputs from one address.");
            return false;
        }
    }
    destinationOut = QString::fromStdString(EncodeDestination(singleDest));
    return true;
}

void SplitForStakingDialog::updateFeeLabels(CAmount fee, CAmount change, unsigned int txSize)
{
    int unit = model->getOptionsModel()->getDisplayUnit();
    ui->labelFee->setText(tr("Fee: %1").arg(BitcoinUnits::formatWithUnit(unit, fee)));
    ui->labelChange->setText(tr("Change: %1").arg(BitcoinUnits::formatWithUnit(unit, change)));
    ui->labelTxSize->setText(tr("Size: %1 bytes").arg(txSize));
    ui->labelSplitPreviewNote->setText(
        tr("New internal addresses are reserved only when you confirm Split."));
    updatePreviewSummaryVisibility(true);
}

void SplitForStakingDialog::showError(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text);
}

void SplitForStakingDialog::previewButtonClicked()
{
    invalidatePreviewState(
        tr("New internal addresses are reserved only when you confirm Split."));

    CAmount splitAmount, outputSize;
    int nOutputs;
    QString error;
    if (!parseParams(splitAmount, outputSize, nOutputs, error)) {
        showError(tr("Split for Staking"), error);
        return;
    }

    if (usingSameAddressMode() && usingSelectedInputs()) {
        QString selectedDest;
        QString selectedErr;
        if (!selectedInputsResolveSingleDestination(selectedDest, selectedErr)) {
            showError(tr("Split for Staking"), selectedErr);
            return;
        }
    }

    CAmount fee, change;
    unsigned int txSize;
    if (!runFeeEstimate(splitAmount, outputSize, nOutputs, fee, change, txSize, error)) {
        showError(tr("Split for Staking"), error);
        return;
    }

    const CCoinControl *active = activeCoinControl();
    if (active && active->HasSelected()) {
        std::vector<COutPoint> selected;
        active->ListSelected(selected);
        std::vector<COutput> selectedOutputs;
        model->getOutputs(selected, selectedOutputs);
        CAmount selectedTotal = 0;
        for (const COutput &out : selectedOutputs)
            selectedTotal += out.tx->vout[out.i].nValue;
        if (selectedTotal < splitAmount + fee) {
            showError(tr("Split for Staking"),
                tr("Selected amount is insufficient. Need at least %1 including estimated fee.")
                    .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), splitAmount + fee)));
            return;
        }
    }

    updateFeeLabels(fee, change, txSize);
    updateDestinationSummary();
    QString warning;
    if (selectedInputsPassSafetyChecks(warning) && !warning.isEmpty()) {
        ui->labelSplitPreviewNote->setText(
            tr("Preview does not create addresses. New internal addresses are reserved only when you confirm Split.\n%1")
                .arg(warning));
    }
    previewReady = true;
    ui->splitButton->setEnabled(true);
}

void SplitForStakingDialog::splitButtonClicked()
{
    if (!previewReady) {
        showError(tr("Split for Staking"), tr("Run Preview successfully before confirming Split."));
        return;
    }

    CAmount splitAmount, outputSize;
    int nOutputs;
    QString error;
    if (!parseParams(splitAmount, outputSize, nOutputs, error)) {
        showError(tr("Split for Staking"), error);
        return;
    }

    if (usingSameAddressMode() && usingSelectedInputs()) {
        QString selectedDest;
        QString selectedErr;
        if (!selectedInputsResolveSingleDestination(selectedDest, selectedErr)) {
            showError(tr("Split for Staking"), selectedErr);
            return;
        }
    }

    bool singleUtxoPause = false;
    if (!leavesMatureStakeReserve(splitAmount, singleUtxoPause, error)) {
        showError(tr("Split for Staking"), error);
        return;
    }

    CAmount fee, change;
    unsigned int txSize;
    if (!runFeeEstimate(splitAmount, outputSize, nOutputs, fee, change, txSize, error)) {
        showError(tr("Split for Staking"), error);
        return;
    }

    const CCoinControl *active = activeCoinControl();
    if (active && active->HasSelected()) {
        std::vector<COutPoint> selected;
        active->ListSelected(selected);
        std::vector<COutput> selectedOutputs;
        model->getOutputs(selected, selectedOutputs);
        CAmount selectedTotal = 0;
        for (const COutput &out : selectedOutputs)
            selectedTotal += out.tx->vout[out.i].nValue;
        if (selectedTotal < splitAmount + fee) {
            showError(tr("Split for Staking"),
                tr("Selected amount is insufficient. Need at least %1 including estimated fee.")
                    .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), splitAmount + fee)));
            invalidatePreviewState(
                tr("Inputs changed. Run Preview again to enable Split."));
            return;
        }
    }

    updateFeeLabels(fee, change, txSize);

    QString question = tr("Split %1 into %2 outputs of %3 each?\n\n"
                          "Fee: %4\n"
                          "Change: %5\n"
                          "New internal receive addresses (+ change when needed) are created only now, not during Preview.\n\n"
                          "Are you sure?")
        .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), splitAmount))
        .arg(nOutputs)
        .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), outputSize))
        .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), fee))
        .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), change));

    if (singleUtxoPause) {
        question.prepend(tr("You have only one mature staking UTXO. Staking will pause until this split confirms (about 1 block).\n\n"));
    }
    QString selectedWarning;
    selectedInputsPassSafetyChecks(selectedWarning);
    if (!selectedWarning.isEmpty())
        question.prepend(selectedWarning + "\n\n");

    if (QMessageBox::question(this, tr("Confirm split"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
        return;

    CCoinControl ctrl;
    fillStakeSplitCoinControl(ctrl);

    QString commitErr;
    QString commitDestination;
    WalletModel::SendCoinsReturn st = model->commitStakeSplit(
        nOutputs, outputSize, &ctrl, usingSameAddressMode(), commitErr, commitDestination);

    if (st.status != WalletModel::OK) {
        QString msg;
        if (!commitErr.isEmpty())
            msg = commitErr;
        else {
            switch (st.status) {
            case WalletModel::AmountExceedsBalance:
                msg = tr("Amount exceeds balance.");
                break;
            case WalletModel::AmountWithFeeExceedsBalance:
                msg = tr("Amount plus fee exceeds balance.");
                break;
            case WalletModel::TransactionCreationFailed:
                msg = tr("Could not create split transaction.");
                break;
            case WalletModel::TransactionCommitFailed:
                msg = tr("Could not finalize split transaction.");
                break;
            case WalletModel::AbsurdFee:
                msg = tr("Calculated fee is higher than configured maximum.");
                break;
            default:
                msg = tr("Split failed.");
                break;
            }
        }
        showError(tr("Split for Staking"), msg);
        return;
    }

    QMessageBox::information(this, tr("Split for Staking"),
        tr("Split transaction sent. %1 outputs will appear after confirmation.")
            .arg(nOutputs));
    accept();
}

void SplitForStakingDialog::updatePreviewSummaryVisibility(bool showSummary)
{
    ui->labelFee->setVisible(showSummary);
    ui->labelChange->setVisible(showSummary);
    ui->labelTxSize->setVisible(showSummary);
    ui->labelPreviewPlaceholder->setVisible(!showSummary);
}
