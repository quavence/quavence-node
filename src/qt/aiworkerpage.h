// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#ifndef BITCOIN_QT_AIWORKERPAGE_H
#define BITCOIN_QT_AIWORKERPAGE_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QStringList>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>

class ClientModel;
class WalletModel;
class PlatformStyle;

namespace Ui {
class AIWorkerPage;
}

/**
 * All-in-One AI Worker & DePIN Node Controller
 * Autonomous C++ engine that communicates with Quavence Hub (quavence.com),
 * syncs runtime policy, claims decentralized AI tasks, runs inference via LM Studio / Ollama,
 * submits cryptographic attestations, and monitors on-chain PoUS consensus boost.
 */
class AIWorkerPage : public QWidget
{
    Q_OBJECT

public:
    enum class PreflightStatus {
        Standby,
        CheckingRuntime,
        TokenRequired,
        AddressRequired,
        InvalidAddress,
        RuntimeOffline,
        ModelNotLoaded
    };
    explicit AIWorkerPage(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~AIWorkerPage();

    static void EnsureSslCertificatesLoaded();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);

public Q_SLOTS:
    void updatePoUSStatus();
    void onToggleWorker(bool checked);
    void onCheckRuntimeConnection();
    void onRuntimeProviderChanged(int index);
    void onModelSelectionChanged(int index);
    void onAddressSelectionChanged(int index);
    void onWorkerTokenChanged(const QString &token);
    void onToggleWorkerClicked();
    void onToggleTokenEdit();
    void onToggleTokenVisibility();

private Q_SLOTS:
    void onProbeReplyFinished(QNetworkReply *reply);
    void onProbeV0ReplyFinished(QNetworkReply *reply);
    void onHubPolicyReply(QNetworkReply *reply);
    void onHubHeartbeatReply(QNetworkReply *reply);
    void onHubClaimReply(QNetworkReply *reply);
    void onInferenceReply(QNetworkReply *reply);
    void onInferenceReadyRead(QNetworkReply *reply);
    void onHubSubmitReply(QNetworkReply *reply);
    void onHubFailReply(QNetworkReply *reply, const QString &taskId);

    void onHeartbeatTimer();
    void onClaimPollTimer();
    void onPeriodicRefresh();

private:
    Ui::AIWorkerPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;

    QNetworkAccessManager *networkManager;
    QTimer *refreshTimer;
    QTimer *heartbeatTimer;
    QTimer *claimPollTimer;
    QTimer *heartbeatDebounceTimer;

    bool isWorkerActive;
    bool isTokenEditing;
    bool isModelPolicyCompliant;
    bool isTaskRunning;
    bool isRuntimeOnline;
    bool pendingStartAfterProbe;
    PreflightStatus preflightStatus;
    QString currentModelName;
    QString workerDeviceId;
    int attestationCount;
    int currentBoostPercent;
    int tasksCompletedCount;

    // Hub Policy & Runtime Probing
    QString hubPolicyVersion;
    QString hubRequiredGenModel;
    QString hubRequiredEmbedModel;
    QStringList hubAllowedGenModels;
    int hubTier;
    int detectedMaxContextTokens;
    QMap<QString, int> modelContextLengths;

    // Current in-flight task state
    QString currentTaskId;
    QString currentTaskType;
    QString currentClaimNonce;
    bool currentTaskIsControl;
    bool currentTaskStreamRequested;
    bool isTaskStreaming;
    int currentStreamSeq;
    QString accumulatedStreamText;
    QByteArray streamReadBuffer;
    QStringList pendingStreamDeltas;
    bool isStreamFlushing;
    bool isStreamFinishing;
    QString pendingFinishOutputText;
    QJsonObject currentTurnInput;
    QJsonArray currentTaskTools;
    QJsonValue currentTaskToolChoice;
    QJsonArray accumulatedToolCalls;
    QList<QJsonObject> pendingStreamToolCallsFrames;

    void logMessage(const QString &msg, const QString &level = "INFO");
    void updateBoostUI();
    void updateNodeStatusBadge();
    void populateAddresses();
    void loadSettings();
    void saveSettings();
    QString getSelectedEndpointUrl() const;
    QString getWorkerToken() const;
    QString getHubBaseUrl() const;

    bool isApprovedGenerationModel(const QString &modelId) const;
    bool isEmbeddingModel(const QString &modelId) const;
    bool isContextOverflowError(int httpStatus, const QString &errorBody) const;

    // Hub Engine Methods
    void fetchHubRuntimePolicy();
    void sendHubHeartbeat();
    void pollHubTask();
    void dispatchTask(const QString &taskId, const QString &taskType, const QString &claimNonce, const QJsonObject &resultJson, bool isControl = false);
    void executeInference(const QString &systemPrompt, const QString &userPrompt, const QJsonArray &structuredMessages = QJsonArray());
    void executeEmbeddingInference(const QJsonObject &resultJson);
    void mergeStreamingToolCall(const QJsonObject &deltaTc);
    void flushStreamQueue();
    void finalizeTaskInference(const QString &outputText);
    void sendStreamChunk(const QString &taskId, const QString &claimNonce, int seq, const QString &delta, const QString &type = "chunk");
    void submitTaskResult(const QString &taskId, const QString &taskType, const QString &claimNonce, const QJsonObject &result);
    void reportTaskFailure(const QString &taskId, const QString &reasonCode, const QString &errorMessage, const QJsonObject &details);

    QJsonObject buildRuntimeAttestation() const;
    QString computeHmacSha256(const QString &key, const QString &data) const;
    QString canonicalJson(const QJsonObject &obj) const;
    QJsonObject parseTaskJsonOutput(const QString &rawText, const QString &taskType);
};

#endif // BITCOIN_QT_AIWORKERPAGE_H
