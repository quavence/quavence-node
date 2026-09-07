// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletmodel.h"

#include "addresstablemodel.h"
#include "coincontrol.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "recentrequeststablemodel.h"
#include "transactiontablemodel.h"
#include "dstencode.h"
#include "keystore.h"
#include "main.h"
#include "policy/policy.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "ui_interface.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h" // for BackupWallet

#include <stdint.h>

#include <limits>

#include <QDebug>
#include <QSet>
#include <QTimer>

#include <boost/foreach.hpp>

namespace {

/** Dummy P2PKH for fee/size probing only — not spendable wallet keys (same serialized size). */
static CScript DummyStakeSplitP2pkh(int index)
{
    std::vector<unsigned char> vch(20, 0x5a);
    for (int b = 0; b < 4; ++b)
        vch[16 + b] ^= static_cast<unsigned char>((index >> (b * 8)) & 0xff);
    return CScript() << OP_DUP << OP_HASH160 << vch << OP_EQUALVERIFY << OP_CHECKSIG;
}

static void BuildDummyRecipients(int nOutputs, CAmount amountEach, std::vector<CRecipient>& vecSend)
{
    vecSend.clear();
    vecSend.reserve(nOutputs);
    for (int i = 0; i < nOutputs; ++i) {
        CRecipient recipient = { DummyStakeSplitP2pkh(i), amountEach, false };
        vecSend.push_back(recipient);
    }
}

static bool ResolveSingleInputAddress(const CWallet* wallet, const CWalletTx& tx, CTxDestination& destOut, std::string& err)
{
    bool hasDest = false;
    BOOST_FOREACH(const CTxIn& in, tx.vin) {
        std::map<uint256, CWalletTx>::const_iterator it = wallet->mapWallet.find(in.prevout.hash);
        if (it == wallet->mapWallet.end() || in.prevout.n >= it->second.vout.size()) {
            err = "Could not resolve source input for same-address split.";
            return false;
        }
        CTxDestination dest;
        if (!ExtractDestination(it->second.vout[in.prevout.n].scriptPubKey, dest)) {
            err = "Same-address split requires standard address inputs.";
            return false;
        }
        if (!hasDest) {
            destOut = dest;
            hasDest = true;
        } else if (!(dest == destOut)) {
            err = "Same address split requires selected inputs from one address.";
            return false;
        }
    }
    if (!hasDest) {
        err = "No source inputs available for same-address split.";
        return false;
    }
    return true;
}

static void SelectExactInputs(const CWalletTx& tx, CCoinControl& coinControl)
{
    coinControl.UnSelectAll();
    BOOST_FOREACH(const CTxIn& in, tx.vin)
        coinControl.Select(in.prevout);
    coinControl.fAllowOtherInputs = false;
}

} // namespace

WalletModel::WalletModel(const PlatformStyle *platformStyle, CWallet *wallet, OptionsModel *optionsModel, QObject *parent) :
    QObject(parent), wallet(wallet), optionsModel(optionsModel), addressTableModel(0),
    transactionTableModel(0),
    recentRequestsTableModel(0),
    cachedBalance(0),
	cachedUnconfirmedBalance(0),
	cachedImmatureBalance(0),
	cachedStake(0),
	cachedWatchOnlyBalance(0),
	cachedWatchUnconfBalance(0),
	cachedWatchImmatureBalance(0),
	cachedWatchOnlyStake(0),
    cachedEncryptionStatus(Unencrypted),
	cachedNumBlocks(0)

{
    fHaveWatchOnly = wallet->HaveWatchOnly();
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(platformStyle, wallet, this);
    recentRequestsTableModel = new RecentRequestsTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

CAmount WalletModel::getBalance(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        CAmount nBalance = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(vCoins, true, coinControl);
        BOOST_FOREACH(const COutput& out, vCoins)
            if(out.fSpendable)
                nBalance += out.tx->vout[out.i].nValue;

        return nBalance;
    }

    return wallet->GetBalance();
}

CAmount WalletModel::getStake() const
{
    return wallet->GetStake();
}

CAmount WalletModel::getStakeRewards() const
{
    CAmount totalRewards = 0;
    LOCK2(cs_main, wallet->cs_wallet);

    for (std::map<uint256, CWalletTx>::const_iterator it = wallet->mapWallet.begin();
         it != wallet->mapWallet.end(); ++it) {
        const CWalletTx& wtx = it->second;
        if (!wtx.IsCoinStake())
            continue;
        if (!wtx.IsInMainChain())
            continue;

        const CAmount netReward = wtx.GetCredit(ISMINE_ALL) - wtx.GetDebit(ISMINE_ALL);
        // Display-only cumulative reward metric: never count returned principal.
        if (netReward > 0)
            totalRewards += netReward;
    }

    return totalRewards;
}

CAmount WalletModel::getWatchStake() const
{
    return wallet->GetWatchOnlyStake();
}

CAmount WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance();
}

CAmount WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

bool WalletModel::haveWatchOnly() const
{
    return fHaveWatchOnly;
}

CAmount WalletModel::getWatchBalance() const
{
    return wallet->GetWatchOnlyBalance();
}

CAmount WalletModel::getWatchUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedWatchOnlyBalance();
}

CAmount WalletModel::getWatchImmatureBalance() const
{
    return wallet->GetImmatureWatchOnlyBalance();
}

unsigned int WalletModel::getDonationPercentage() const
{
    return wallet->GetDonationPercentage();
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        Q_EMIT encryptionStatusChanged(newEncryptionStatus);
}

void WalletModel::pollBalanceChanged()
{
    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(fForceCheckBalanceChanged || chainActive.Height() != cachedNumBlocks)
    {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = chainActive.Height();

        checkBalanceChanged();
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    CAmount newBalance = getBalance();
    CAmount newUnconfirmedBalance = getUnconfirmedBalance();
    CAmount newImmatureBalance = getImmatureBalance();
    CAmount newStake = getStake();
    CAmount newWatchOnlyBalance = 0;
    CAmount newWatchUnconfBalance = 0;
    CAmount newWatchImmatureBalance = 0;
    CAmount newWatchOnlyStake = 0;
    CAmount newDonationPercentage = getDonationPercentage();
    if (haveWatchOnly())
    {
        newWatchOnlyBalance = getWatchBalance();
        newWatchUnconfBalance = getWatchUnconfirmedBalance();
        newWatchImmatureBalance = getWatchImmatureBalance();
        newWatchOnlyStake = getWatchStake();
    }

    if(cachedBalance != newBalance || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedImmatureBalance != newImmatureBalance ||
    		cachedWatchOnlyBalance != newWatchOnlyBalance || cachedWatchUnconfBalance != newWatchUnconfBalance || cachedWatchImmatureBalance != newWatchImmatureBalance || cachedStake != newStake || cachedWatchOnlyStake != newWatchOnlyStake)
    {
        cachedBalance = newBalance;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        cachedStake = newStake;
        cachedWatchOnlyBalance = newWatchOnlyBalance;
        cachedWatchUnconfBalance = newWatchUnconfBalance;
        cachedWatchImmatureBalance = newWatchImmatureBalance;
        cachedWatchOnlyStake = newWatchOnlyStake;
                Q_EMIT balanceChanged(newBalance, newUnconfirmedBalance, newImmatureBalance, newStake,
                                    newWatchOnlyBalance, newWatchUnconfBalance, newWatchImmatureBalance, newWatchOnlyStake,
                                    newDonationPercentage);
    }
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label,
        bool isMine, const QString &purpose, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, purpose, status);
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly)
{
    fHaveWatchOnly = fHaveWatchonly;
    Q_EMIT notifyWatchonlyChanged(fHaveWatchonly);
}

bool WalletModel::validateAddress(const QString &address) { return IsValidDestinationString(address.toStdString()); }

WalletModel::SendCoinsReturn WalletModel::prepareTransaction(WalletModelTransaction &transaction, const CCoinControl *coinControl)
{
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;

    if(recipients.empty())
    {
        return OK;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    Q_FOREACH(const SendCoinsRecipient &rcp, recipients)
    {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;

        if (rcp.paymentRequest.IsInitialized())
        {   // PaymentRequest...
            CAmount subtotal = 0;
            const payments::PaymentDetails& details = rcp.paymentRequest.getDetails();
            for (int i = 0; i < details.outputs_size(); i++)
            {
                const payments::Output& out = details.outputs(i);
                if (out.amount() <= 0) continue;
                subtotal += out.amount();
                const unsigned char* scriptStr = (const unsigned char*)out.script().data();
                CScript scriptPubKey(scriptStr, scriptStr+out.script().size());
                CAmount nAmount = out.amount();
                CRecipient recipient = {scriptPubKey, nAmount, rcp.fSubtractFeeFromAmount};
                vecSend.push_back(recipient);
            }
            if (subtotal <= 0)
            {
                return InvalidAmount;
            }
            total += subtotal;
        }
        else
        {   // User-entered bitcoin address / amount:
            if(!validateAddress(rcp.address))
            {
                return InvalidAddress;
            }
            if(rcp.amount <= 0)
            {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;

            CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
            CRecipient recipient = {scriptPubKey, rcp.amount, rcp.fSubtractFeeFromAmount};
            vecSend.push_back(recipient);

            total += rcp.amount;
        }
    }
    if (setAddress.size() != nAddresses)
    {
        return DuplicateAddress;
    }

    CAmount nBalance = getBalance(coinControl);

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        transaction.newPossibleKeyChange(wallet);

        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strFailReason;

        CWalletTx *newTx = transaction.getTransaction();
        CReserveKey *keyChange = transaction.getPossibleKeyChange();
        bool fCreated = wallet->CreateTransaction(vecSend, *newTx, *keyChange, nFeeRequired, nChangePosRet, strFailReason, coinControl);
        transaction.setTransactionFee(nFeeRequired);
        if (fSubtractFeeFromAmount && fCreated)
            transaction.reassignAmounts(nChangePosRet);

        if(!fCreated)
        {
            if(!fSubtractFeeFromAmount && (total + nFeeRequired) > nBalance)
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }
            Q_EMIT message(tr("Send Coins"), QString::fromStdString(strFailReason),
                         CClientUIInterface::MSG_ERROR);
            return TransactionCreationFailed;
        }

        // reject absurdly high fee. (This can never happen because the
        // wallet caps the fee at maxTxFee. This merely serves as a
        // belt-and-suspenders check)
        if (nFeeRequired > maxTxFee)
            return AbsurdFee;
    }

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::estimateStakeSplitFees(
    int nOutputs, CAmount amountEach,
    const CCoinControl *coinControl,
    bool sameSourceAddressMode,
    CAmount &feeRetOut,
    unsigned int &txSizeRetOut,
    QString &errorReasonOut,
    QString &resolvedDestinationOut)
{
    feeRetOut = 0;
    txSizeRetOut = 0;
    errorReasonOut.clear();
    resolvedDestinationOut.clear();

    if (nOutputs < 1 || amountEach <= 0)
        return InvalidAmount;

    if (amountEach > std::numeric_limits<CAmount>::max() / static_cast<CAmount>(nOutputs))
        return InvalidAmount;

    const CAmount total = amountEach * static_cast<CAmount>(nOutputs);

    const CAmount nBalance = getBalance(coinControl);
    if (total > nBalance)
        return AmountExceedsBalance;

    {
        LOCK2(cs_main, wallet->cs_wallet);
        CCoinControl probeControl;
        if (coinControl)
            probeControl = *coinControl;
        if (!(coinControl && coinControl->HasSelected()))
            probeControl.fAllowOtherInputs = true;
        probeControl.destChange = CKeyID(uint160S("0000000000000000000000000000000000000001"));

        CWalletTx wProbe;
        CReserveKey rkProbe(wallet);
        CAmount nProbeFeeRequired = 0;
        int nProbeChangePosRet = -1;
        std::string strProbeFailReason;
        std::vector<CRecipient> vecProbeSend;
        BuildDummyRecipients(nOutputs, amountEach, vecProbeSend);
        bool fProbeCreated = wallet->CreateTransaction(vecProbeSend, wProbe, rkProbe,
            nProbeFeeRequired, nProbeChangePosRet, strProbeFailReason, &probeControl, false);

        if (!fProbeCreated && (total + nProbeFeeRequired) > nBalance) {
            errorReasonOut = tr("Amount plus fee exceeds balance.");
            return AmountWithFeeExceedsBalance;
        }
        if (!fProbeCreated) {
            errorReasonOut = QString::fromStdString(strProbeFailReason);
            return TransactionCreationFailed;
        }

        CTxDestination sourceDest;
        if (sameSourceAddressMode) {
            std::string resolveErr;
            if (!ResolveSingleInputAddress(wallet, wProbe, sourceDest, resolveErr)) {
                errorReasonOut = tr(resolveErr.c_str());
                return TransactionCreationFailed;
            }
        }

        CCoinControl estimateControl = probeControl;
        SelectExactInputs(wProbe, estimateControl);
        if (sameSourceAddressMode)
            estimateControl.destChange = sourceDest;
        else
            estimateControl.destChange = CKeyID(uint160S("0000000000000000000000000000000000000001"));

        std::vector<CRecipient> vecSend;
        vecSend.reserve(nOutputs);
        for (int i = 0; i < nOutputs; ++i) {
            CScript outScript = sameSourceAddressMode ? GetScriptForDestination(sourceDest) : DummyStakeSplitP2pkh(i);
            CRecipient recipient = { outScript, amountEach, false };
            vecSend.push_back(recipient);
        }

        CWalletTx wtemp;
        CReserveKey rkChange(wallet);
        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strFailReason;
        bool fCreated = wallet->CreateTransaction(vecSend, wtemp, rkChange,
            nFeeRequired, nChangePosRet, strFailReason, &estimateControl, false);

        if (!fCreated && (total + nFeeRequired) > nBalance) {
            errorReasonOut = tr("Amount plus fee exceeds balance.");
            return AmountWithFeeExceedsBalance;
        }
        if (!fCreated) {
            errorReasonOut = QString::fromStdString(strFailReason);
            return TransactionCreationFailed;
        }
        if (nFeeRequired > maxTxFee) {
            errorReasonOut = tr("Calculated fee is higher than configured maximum.");
            return AbsurdFee;
        }

        const unsigned int nBytes =
            ::GetSerializeSize(*(CTransaction*)&wtemp, SER_NETWORK, PROTOCOL_VERSION);
        if (nBytes >= MAX_STANDARD_TX_SIZE) {
            errorReasonOut = tr("Transaction would exceed the maximum size (%1 bytes). Use fewer outputs.")
                .arg(MAX_STANDARD_TX_SIZE);
            return TransactionCreationFailed;
        }

        feeRetOut = nFeeRequired;
        txSizeRetOut = nBytes;
        if (sameSourceAddressMode)
            resolvedDestinationOut = QString::fromStdString(EncodeDestination(sourceDest));
        else
            resolvedDestinationOut = tr("new internal wallet addresses");
    }
    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::commitStakeSplit(
    int nOutputs, CAmount amountEach,
    const CCoinControl *coinControl,
    bool sameSourceAddressMode,
    QString &errorReasonOut,
    QString &resolvedDestinationOut)
{
    errorReasonOut.clear();
    resolvedDestinationOut.clear();

    if (nOutputs < 1 || amountEach <= 0)
        return InvalidAmount;

    if (amountEach > std::numeric_limits<CAmount>::max() / static_cast<CAmount>(nOutputs))
        return InvalidAmount;

    const CAmount total = amountEach * static_cast<CAmount>(nOutputs);

    const CAmount nBalance = getBalance(coinControl);
    if (total > nBalance)
        return AmountExceedsBalance;

    SendCoinsReturn result(OK);

    {
        LOCK2(cs_main, wallet->cs_wallet);

        std::vector<CReserveKey> outputRK;
        std::vector<CPubKey> outPubkeys;
        std::vector<CRecipient> vecSend;
        vecSend.reserve(static_cast<size_t>(nOutputs));

        CCoinControl commitControl;
        if (coinControl)
            commitControl = *coinControl;
        if (!(coinControl && coinControl->HasSelected()))
            commitControl.fAllowOtherInputs = true;

        CTxDestination sourceDest;
        if (sameSourceAddressMode) {
            CWalletTx wProbe;
            CReserveKey rkProbe(wallet);
            CAmount nProbeFeeRequired = 0;
            int nProbeChangePosRet = -1;
            std::string strProbeFailReason;
            std::vector<CRecipient> vecProbeSend;
            BuildDummyRecipients(nOutputs, amountEach, vecProbeSend);
            bool fProbeCreated = wallet->CreateTransaction(vecProbeSend, wProbe, rkProbe,
                nProbeFeeRequired, nProbeChangePosRet, strProbeFailReason, &commitControl);
            if (!fProbeCreated) {
                errorReasonOut = QString::fromStdString(strProbeFailReason);
                return TransactionCreationFailed;
            }
            std::string resolveErr;
            if (!ResolveSingleInputAddress(wallet, wProbe, sourceDest, resolveErr)) {
                errorReasonOut = tr(resolveErr.c_str());
                return TransactionCreationFailed;
            }
            SelectExactInputs(wProbe, commitControl);
            commitControl.destChange = sourceDest;
            for (int i = 0; i < nOutputs; ++i) {
                CRecipient recipient = { GetScriptForDestination(sourceDest), amountEach, false };
                vecSend.push_back(recipient);
            }
            resolvedDestinationOut = QString::fromStdString(EncodeDestination(sourceDest));
        } else {
            outputRK.reserve(static_cast<size_t>(nOutputs));
            outPubkeys.reserve(static_cast<size_t>(nOutputs));
            for (int i = 0; i < nOutputs; ++i) {
                outputRK.emplace_back(wallet);
                CPubKey pk;
                if (!outputRK.back().GetReservedKey(pk)) {
                    errorReasonOut = tr("Could not reserve a key for split output %1/%2.")
                        .arg(i + 1).arg(nOutputs);
                    return TransactionCreationFailed;
                }
                outPubkeys.push_back(pk);
                CRecipient recipient = { GetScriptForDestination(pk.GetID()), amountEach, false };
                vecSend.push_back(recipient);
            }
            resolvedDestinationOut = tr("new internal wallet addresses");
        }

        CReserveKey rkChange(wallet);
        CWalletTx wtxNew;
        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strFailReason;
        bool fCreated = wallet->CreateTransaction(vecSend, wtxNew, rkChange,
            nFeeRequired, nChangePosRet, strFailReason, &commitControl);

        if (!fCreated && (total + nFeeRequired) > nBalance)
            result = AmountWithFeeExceedsBalance;
        else if (!fCreated) {
            errorReasonOut = QString::fromStdString(strFailReason);
            result = TransactionCreationFailed;
        } else if (nFeeRequired > maxTxFee) {
            for (auto& rk : outputRK) rk.ReturnKey();
            result = AbsurdFee;
        } else {
            if (!wallet->CommitTransaction(wtxNew, rkChange)) {
                for (auto& rk : outputRK) rk.ReturnKey();
                errorReasonOut = tr("Committing split transaction failed.");
                result = TransactionCommitFailed;
            } else {
                for (auto& rk : outputRK) rk.KeepKey();
                if (!sameSourceAddressMode) {
                    for (size_t idx = 0; idx < outPubkeys.size(); ++idx) {
                        const std::string label =
                            QString("split-stake-%1").arg(static_cast<int>(idx + 1)).toStdString();
                        wallet->SetAddressBook(outPubkeys[idx].GetID(), label, "receive");
                    }
                }
            }
        }
    }

    if (result.status == OK)
        checkBalanceChanged();
    else if (result.status == AmountWithFeeExceedsBalance && errorReasonOut.isEmpty())
        errorReasonOut = tr("Amount plus fee exceeds balance.");
    else if (result.status == AbsurdFee && errorReasonOut.isEmpty())
        errorReasonOut = tr("Calculated fee is higher than configured maximum.");

    return result;
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction &transaction)
{
    QByteArray transaction_array; /* store serialized transaction */

    {
        LOCK2(cs_main, wallet->cs_wallet);
        CWalletTx *newTx = transaction.getTransaction();

        Q_FOREACH(const SendCoinsRecipient &rcp, transaction.getRecipients())
        {
            if (rcp.paymentRequest.IsInitialized())
            {
                // Make sure any payment requests involved are still valid.
                if (PaymentServer::verifyExpired(rcp.paymentRequest.getDetails())) {
                    return PaymentRequestExpired;
                }

                // Store PaymentRequests in wtx.vOrderForm in wallet.
                std::string key("PaymentRequest");
                std::string value;
                rcp.paymentRequest.SerializeToString(&value);
                newTx->vOrderForm.push_back(make_pair(key, value));
            }
            else if (!rcp.message.isEmpty())
            {
                // Message from normal quavence:URI
                // (quavence:123...?message=example)
                newTx->vOrderForm.push_back(make_pair("Message", rcp.message.toStdString()));
            }
            else if (!rcp.message.isEmpty()) // Message from normal quavence:URI (quavence:123...?message=example)
                newTx->vOrderForm.push_back(make_pair("Message", rcp.message.toStdString()));
        }

        CReserveKey *keyChange = transaction.getPossibleKeyChange();
        if(!wallet->CommitTransaction(*newTx, *keyChange))
            return TransactionCommitFailed;

        CTransaction* t = (CTransaction*)newTx;
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << *t;
        transaction_array.append(&(ssTx[0]), ssTx.size());
    }

    // Add addresses / update labels that we've sent to to the address book,
    // and emit coinsSent signal for each recipient
    Q_FOREACH(const SendCoinsRecipient &rcp, transaction.getRecipients())
    {
        // Don't touch the address book when we have a payment request
        if (!rcp.paymentRequest.IsInitialized())
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                LOCK(wallet->cs_wallet);

                std::map<CTxDestination, CAddressBookData>::iterator mi = wallet->mapAddressBook.find(dest);

                // Check if we have a new address or an updated label
                if (mi == wallet->mapAddressBook.end())
                {
                    wallet->SetAddressBook(dest, strLabel, "send");
                }
                else if (mi->second.name != strLabel)
                {
                    wallet->SetAddressBook(dest, strLabel, ""); // "" means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(wallet, rcp, transaction_array);
    }
    checkBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

RecentRequestsTableModel *WalletModel::getRecentRequestsTableModel()
{
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!wallet->IsCrypted())
    {
        return Unencrypted;
    }
    else if(wallet->IsLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        return wallet->Unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return wallet->BackupWallet(filename.toLocal8Bit().data());
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet,
        const CTxDestination &address, const std::string &label, bool isMine,
        const std::string &purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(EncodeDestination(address));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + strPurpose + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(QString, strPurpose),
                              Q_ARG(int, status));
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    Q_UNUSED(wallet);
    Q_UNUSED(hash);
    Q_UNUSED(status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection);
}

static void ShowProgress(WalletModel *walletmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyWatchonlyChanged(WalletModel *walletmodel, bool fHaveWatchonly)
{
    QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
                              Q_ARG(bool, fHaveWatchonly));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    wallet->NotifyWatchonlyChanged.connect(boost::bind(NotifyWatchonlyChanged, this, _1));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    wallet->NotifyWatchonlyChanged.disconnect(boost::bind(NotifyWatchonlyChanged, this, _1));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;

    if ((!was_locked) && fWalletUnlockStakingOnly)
    {
    	setWalletLocked(true);
        was_locked = getEncryptionStatus() == Locked;

    }

    if(was_locked)
    {
        // Request UI to unlock wallet
        Q_EMIT requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked && !fWalletUnlockStakingOnly);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *wallet, bool valid, bool relock):
        wallet(wallet),
        valid(valid),
        relock(relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);
}

bool WalletModel::IsSpendable(const CTxDestination &dest) const { return wallet->IsMine(dest) & ISMINE_SPENDABLE; }
// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    BOOST_FOREACH(const COutPoint& outpoint, vOutpoints)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true, true);
        vOutputs.push_back(out);
    }
}

bool WalletModel::isSpent(const COutPoint& outpoint) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsSpent(outpoint.hash, outpoint.n);
}

void WalletModel::listMatureStakingCoins(std::vector<COutput>& vCoinsOut) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->AvailableCoinsForStaking(vCoinsOut);
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address)
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(vCoins);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet
    std::vector<COutPoint> vLockedCoins;
    wallet->ListLockedCoins(vLockedCoins);

    // add locked coins (only unspent)
    BOOST_FOREACH(const COutPoint& outpoint, vLockedCoins)
    {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        if (wallet->IsSpent(outpoint.hash, outpoint.n)) continue;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true, true);
        if (outpoint.n < out.tx->vout.size() && wallet->IsMine(out.tx->vout[outpoint.n]) == ISMINE_SPENDABLE)
            vCoins.push_back(out);
    }

    BOOST_FOREACH(const COutput& out, vCoins)
    {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0]))
        {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0, true, true);
        }

        CTxDestination address;
        if(!out.fSpendable || !ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address))
            continue;
        mapCoins[QString::fromStdString(EncodeDestination(address))].push_back(out);
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsLockedCoin(hash, n);
}

void WalletModel::lockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->LockCoin(output);
}

void WalletModel::unlockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->UnlockCoin(output);
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->ListLockedCoins(vOutpts);
}

void WalletModel::loadReceiveRequests(std::vector<std::string>& vReceiveRequests)
{
    LOCK(wallet->cs_wallet);
    BOOST_FOREACH(const PAIRTYPE(CTxDestination, CAddressBookData)& item, wallet->mapAddressBook)
        BOOST_FOREACH(const PAIRTYPE(std::string, std::string)& item2, item.second.destdata)
            if (item2.first.size() > 2 && item2.first.substr(0,2) == "rr") // receive request
                vReceiveRequests.push_back(item2.second);
}

bool WalletModel::saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest)
{
    CTxDestination dest = DecodeDestination(sAddress);

    std::stringstream ss;
    ss << nId;
    std::string key = "rr" + ss.str(); // "rr" prefix = "receive request" in destdata

    LOCK(wallet->cs_wallet);
    if (sRequest.empty())
        return wallet->EraseDestData(dest, key);
    else
        return wallet->AddDestData(dest, key, sRequest);
}

bool WalletModel::transactionCanBeAbandoned(uint256 hash) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    const CWalletTx *wtx = wallet->GetWalletTx(hash);
    if (!wtx || wtx->isAbandoned() || wtx->GetDepthInMainChain() > 0 || wtx->InMempool())
        return false;
    return true;
}

bool WalletModel::abandonTransaction(uint256 hash) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->AbandonTransaction(hash);
}

bool WalletModel::hdEnabled() const
{
    return wallet->IsHDEnabled();
}
