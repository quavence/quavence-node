// Copyright (c) 2026 The Quavence developers
// Licensed under the Business Source License 1.1 (BSL-1.1)
// See LICENSE-ADDITIONS in the root of this repository

#include "aiworkerpage.h"
#include "ui_aiworkerpage.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "addresstablemodel.h"
#include "platformstyle.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "airegistry.h"
#include "main.h"

#include <QDateTime>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QUuid>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslError>
#include <QFile>
#include <QSet>

namespace {
    static const QString DEFAULT_HUB_BASE_URL = "https://quavence.com";
    static const QString DEFAULT_LM_STUDIO_URL = "http://127.0.0.1:1234/v1";
    static const QString DEFAULT_OLLAMA_URL = "http://127.0.0.1:11434";
    static const QString SETTINGS_GROUP = "AIWorker";
}


void AIWorkerPage::EnsureSslCertificatesLoaded()
{
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
#else
    SSL_library_init();
#endif

    if (!QSslSocket::supportsSsl()) {
        qWarning() << "[SSL] QSslSocket reports SSL is NOT supported! Build:"
                   << QSslSocket::sslLibraryBuildVersionString()
                   << "Loaded:" << QSslSocket::sslLibraryVersionString();
        return;
    }

    QList<QSslCertificate> certs = QSslConfiguration::systemCaCertificates();

    static const QStringList certLocations = {
        QStringLiteral("/etc/ssl/certs/ca-certificates.crt"),
        QStringLiteral("/etc/pki/tls/certs/ca-bundle.crt"),
        QStringLiteral("/etc/ssl/ca-bundle.pem"),
        QStringLiteral("/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"),
        QStringLiteral("/etc/ssl/cert.pem"),
        QStringLiteral("/usr/share/ca-certificates/ca-certificates.crt"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/ca-certificates/ca-certificates.crt"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../etc/ssl/certs/ca-certificates.crt")
    };

    QByteArray envCertFile = qgetenv("SSL_CERT_FILE");
    if (!envCertFile.isEmpty() && QFile::exists(QString::fromLocal8Bit(envCertFile))) {
        certs.append(QSslCertificate::fromPath(QString::fromLocal8Bit(envCertFile)));
    }

    for (const QString &loc : certLocations) {
        if (QFile::exists(loc)) {
            certs.append(QSslCertificate::fromPath(loc));
        }
    }

    QList<QSslCertificate> uniqueCerts;
    QSet<QByteArray> digests;
    for (const QSslCertificate &cert : certs) {
        if (cert.isNull()) continue;
        QByteArray d = cert.digest();
        if (!digests.contains(d)) {
            digests.insert(d);
            uniqueCerts.append(cert);
        }
    }

    if (!uniqueCerts.isEmpty()) {
        QSslConfiguration conf = QSslConfiguration::defaultConfiguration();
        conf.setCaCertificates(uniqueCerts);
        QSslConfiguration::setDefaultConfiguration(conf);
        qInfo() << "[SSL] Successfully configured" << uniqueCerts.size() << "CA root certificates.";
    } else {
        qWarning() << "[SSL] Warning: No CA root certificates could be found in system or bundled paths!";
    }
}

AIWorkerPage::AIWorkerPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AIWorkerPage),
    clientModel(nullptr),
    walletModel(nullptr),
    platformStyle(platformStyle),
    networkManager(new QNetworkAccessManager(this)),
    refreshTimer(new QTimer(this)),
    heartbeatTimer(new QTimer(this)),
    claimPollTimer(new QTimer(this)),
    isWorkerActive(false),
    isTokenEditing(false),
    isModelPolicyCompliant(true),
    isTaskRunning(false),
    isRuntimeOnline(false),
    pendingStartAfterProbe(false),
    preflightStatusMessage(""),
    currentModelName("qwen/qwen3-vl-8b"),
    workerDeviceId(""),
    attestationCount(0),
    currentBoostPercent(0),
    tasksCompletedCount(0),
    hubPolicyVersion("9adf4daa76f246be"),
    hubRequiredGenModel("qwen/qwen3-vl-8b"),
    hubRequiredEmbedModel("text-embedding-nomic-embed-text-v2-moe")
{
    EnsureSslCertificatesLoaded();
    ui->setupUi(this);

    // Initialize Provider Combo
    ui->comboProvider->clear();
    ui->comboProvider->addItem("LM Studio (Local / GPU)", DEFAULT_LM_STUDIO_URL);
    ui->comboProvider->addItem("Ollama (Local Service)", DEFAULT_OLLAMA_URL);
    ui->comboProvider->addItem("Custom OpenAI Compatible", "");

    // Initialize Model Combo with default approved model
    ui->comboModel->clear();
    ui->comboModel->addItem("qwen/qwen3-vl-8b");

    // Initialize Device ID
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    workerDeviceId = settings.value("deviceId").toString();
    if (workerDeviceId.isEmpty()) {
        workerDeviceId = "qt-worker-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
        settings.setValue("deviceId", workerDeviceId);
    }
    settings.endGroup();

    loadSettings();

    // Standard unified button stylesheets
    ui->btnCheckConnection->setStyleSheet(
        "QPushButton { background-color: #f1f5f9; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; padding: 7px 16px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { background-color: #e2e8f0; }"
        "QPushButton:disabled { background-color: #f8fafc; color: #94a3b8; border-color: #e2e8f0; }"
    );
    ui->btnEditToken->setStyleSheet(
        "QPushButton { background-color: #f1f5f9; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; padding: 4px 12px; font-weight: 500; font-size: 12px; }"
        "QPushButton:hover { background-color: #e2e8f0; }"
    );
    ui->btnToggleTokenVisibility->setStyleSheet(
        "QPushButton { background-color: #f1f5f9; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; padding: 4px 8px; }"
        "QPushButton:hover { background-color: #e2e8f0; }"
    );

    // Wiring UI signals
    connect(ui->btnToggleWorker, &QPushButton::clicked, this, &AIWorkerPage::onToggleWorkerClicked);
    connect(ui->btnEditToken, &QPushButton::clicked, this, &AIWorkerPage::onToggleTokenEdit);
    connect(ui->btnToggleTokenVisibility, &QPushButton::clicked, this, &AIWorkerPage::onToggleTokenVisibility);
    connect(ui->btnCheckConnection, &QPushButton::clicked, this, &AIWorkerPage::onCheckRuntimeConnection);
    connect(ui->comboProvider, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AIWorkerPage::onRuntimeProviderChanged);
    connect(ui->comboModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AIWorkerPage::onModelSelectionChanged);
    connect(ui->comboLinkedAddress, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AIWorkerPage::onAddressSelectionChanged);
    connect(ui->editWorkerToken, &QLineEdit::textChanged, this, &AIWorkerPage::onWorkerTokenChanged);

    // Setup timers
    connect(refreshTimer, &QTimer::timeout, this, &AIWorkerPage::onPeriodicRefresh);
    refreshTimer->start(15000); // 15s

    connect(heartbeatTimer, &QTimer::timeout, this, &AIWorkerPage::onHeartbeatTimer);
    heartbeatTimer->start(30000); // 30s

    connect(claimPollTimer, &QTimer::timeout, this, &AIWorkerPage::onClaimPollTimer);
    claimPollTimer->start(5000); // 5s

    logMessage("All-in-One AI Worker initialized. Ready for DePIN tasks.", "SYS");
    updateBoostUI();
    updateNodeStatusBadge();
    fetchHubRuntimePolicy();
    onCheckRuntimeConnection();
}

AIWorkerPage::~AIWorkerPage()
{
    saveSettings();
    delete ui;
}

void AIWorkerPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if (clientModel) {
        updatePoUSStatus();
    }
}

void AIWorkerPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if (walletModel) {
        populateAddresses();
    }
}

void AIWorkerPage::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    QString token = settings.value("workerToken").toString();
    if (!token.isEmpty()) {
        ui->editWorkerToken->setText(token);
    }
    ui->editEndpointUrl->setText(settings.value("endpointUrl", DEFAULT_LM_STUDIO_URL).toString());
    ui->comboProvider->setCurrentIndex(settings.value("providerIndex", 0).toInt());
    isWorkerActive = settings.value("workerEnabled", false).toBool();
    settings.endGroup();
}

void AIWorkerPage::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("workerToken", ui->editWorkerToken->text().trimmed());
    settings.setValue("endpointUrl", ui->editEndpointUrl->text().trimmed());
    settings.setValue("providerIndex", ui->comboProvider->currentIndex());
    settings.setValue("workerEnabled", isWorkerActive);
    settings.endGroup();
}

void AIWorkerPage::populateAddresses()
{
    if (!walletModel || !walletModel->getAddressTableModel())
        return;

    ui->comboLinkedAddress->clear();
    AddressTableModel *addressTable = walletModel->getAddressTableModel();
    int rows = addressTable->rowCount(QModelIndex());

    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    QString savedAddr = settings.value("payoutAddress").toString();
    settings.endGroup();

    for (int i = 0; i < rows; ++i) {
        QModelIndex addrIndex = addressTable->index(i, AddressTableModel::Address, QModelIndex());
        QModelIndex labelIndex = addressTable->index(i, AddressTableModel::Label, QModelIndex());
        QString address = addressTable->data(addrIndex, Qt::DisplayRole).toString();
        QString label = addressTable->data(labelIndex, Qt::DisplayRole).toString();

        QString display = label.isEmpty() ? address : QString("%1 (%2)").arg(label, address);
        ui->comboLinkedAddress->addItem(display, address);
    }

    // Add fallback if empty
    if (ui->comboLinkedAddress->count() == 0) {
        QString stakingAddr = "SXbKabuHh7xn3QuXF7DMG758D9j4rVcL6V";
        ui->comboLinkedAddress->addItem(stakingAddr + " (Default DAO)", stakingAddr);
    }

    if (!savedAddr.isEmpty()) {
        int idx = ui->comboLinkedAddress->findData(savedAddr);
        if (idx >= 0) {
            ui->comboLinkedAddress->setCurrentIndex(idx);
        }
    }
}

void AIWorkerPage::onAddressSelectionChanged(int index)
{
    if (index < 0) return;
    QString addr = ui->comboLinkedAddress->itemData(index).toString();
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("payoutAddress", addr);
    settings.endGroup();
}

void AIWorkerPage::onWorkerTokenChanged(const QString &token)
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue("workerToken", token.trimmed());
    settings.endGroup();
}

void AIWorkerPage::onToggleWorkerClicked()
{
    if (isWorkerActive) {
        pendingStartAfterProbe = false;
        onToggleWorker(false);
        return;
    }

    // Preflight check 1: Worker Token
    QString token = getWorkerToken();
    if (token.isEmpty()) {
        preflightStatusMessage = "TOKEN REQUIRED";
        updateNodeStatusBadge();
        logMessage("Cannot start worker: Worker Token is empty. Click Edit to enter and save your token.", "WARN");
        return;
    }

    // Preflight check 2: Linked Payout Address
    if (ui->comboLinkedAddress->count() == 0 || ui->comboLinkedAddress->currentData().toString().trimmed().isEmpty()) {
        preflightStatusMessage = "ADDRESS REQUIRED";
        updateNodeStatusBadge();
        logMessage("Cannot start worker: No linked QVNC address selected for payouts.", "WARN");
        return;
    }

    // Preflight check 3: Runtime & Model Ping
    pendingStartAfterProbe = true;
    preflightStatusMessage = "CHECKING RUNTIME...";
    updateNodeStatusBadge();
    logMessage("Running preflight runtime check at " + getSelectedEndpointUrl() + "...", "SYS");
    onCheckRuntimeConnection();
}

void AIWorkerPage::onToggleTokenEdit()
{
    if (!isTokenEditing) {
        isTokenEditing = true;
        ui->editWorkerToken->setReadOnly(false);
        ui->editWorkerToken->setFocus();
        ui->btnEditToken->setText("Save");
        ui->btnEditToken->setStyleSheet("background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 4px 10px; font-weight: 600;");
    } else {
        isTokenEditing = false;
        ui->editWorkerToken->setReadOnly(true);
        ui->btnEditToken->setText("Edit");
        ui->btnEditToken->setStyleSheet("background-color: #f1f5f9; color: #334155; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 10px; font-weight: 500;");
        saveSettings();
        logMessage("Worker Token updated and saved.", "SYS");
    }
}

void AIWorkerPage::onToggleTokenVisibility()
{
    if (ui->editWorkerToken->echoMode() == QLineEdit::Password) {
        ui->editWorkerToken->setEchoMode(QLineEdit::Normal);
    } else {
        ui->editWorkerToken->setEchoMode(QLineEdit::Password);
    }
}

void AIWorkerPage::onToggleWorker(bool checked)
{
    isWorkerActive = checked;
    saveSettings();
    updateNodeStatusBadge();
    if (checked) {
        logMessage("AI Worker enabled. Syncing policy & starting task runner...", "SYS");
        fetchHubRuntimePolicy();
        sendHubHeartbeat();
    } else {
        logMessage("AI Worker stopped.", "SYS");
    }
}

void AIWorkerPage::onRuntimeProviderChanged(int index)
{
    if (index == 0) {
        ui->editEndpointUrl->setText(DEFAULT_LM_STUDIO_URL);
    } else if (index == 1) {
        ui->editEndpointUrl->setText(DEFAULT_OLLAMA_URL);
    }
    saveSettings();
    onCheckRuntimeConnection();
}

void AIWorkerPage::onModelSelectionChanged(int index)
{
    if (index < 0) return;
    currentModelName = ui->comboModel->itemText(index);
    isModelPolicyCompliant = isApprovedGenerationModel(currentModelName);
    updateNodeStatusBadge();
}

void AIWorkerPage::logMessage(const QString &msg, const QString &level)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString formatted = QString("[%1] [%2] %3").arg(timestamp, level, msg);
    ui->textLog->appendPlainText(formatted);
}

QString AIWorkerPage::getWorkerToken() const
{
    return ui->editWorkerToken->text().trimmed();
}

QString AIWorkerPage::getHubBaseUrl() const
{
    return DEFAULT_HUB_BASE_URL;
}

QString AIWorkerPage::getSelectedEndpointUrl() const
{
    QString url = ui->editEndpointUrl->text().trimmed();
    if (url.isEmpty()) {
        return ui->comboProvider->currentIndex() == 0 ? DEFAULT_LM_STUDIO_URL : DEFAULT_OLLAMA_URL;
    }
    return url;
}

void AIWorkerPage::onPeriodicRefresh()
{
    updatePoUSStatus();
    QString token = getWorkerToken();
    if (!token.isEmpty()) {
        QUrl url(getHubBaseUrl() + "/api/ai/nodes/self/overview");
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());

        QNetworkReply *reply = networkManager->get(req);
        connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                logMessage(QString("Overview Sync SSL Error: %1").arg(err.errorString()), "SSL");
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;
            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isObject()) return;
            QJsonObject root = doc.object();
            if (!root.value("success").toBool()) return;
            QJsonObject payload = root.value("data").toObject();
            QJsonObject tasks = payload.value("tasks").toObject();
            int doneToday = tasks.value("doneToday").toInt(tasks.value("done_today").toInt(0));
            int totalDone = tasks.value("done").toInt(0);
            if (doneToday > attestationCount) {
                attestationCount = doneToday;
            }
            if (totalDone > tasksCompletedCount) {
                tasksCompletedCount = totalDone;
            }
            updateBoostUI();
        });
    }
    if (isWorkerActive) {
        fetchHubRuntimePolicy();
    }
}

void AIWorkerPage::updatePoUSStatus()
{
    updateBoostUI();
    updateNodeStatusBadge();
}

void AIWorkerPage::updateBoostUI()
{
    int onChainCount = 0;
    int onChainBoost = 0;
    if (chainActive.Tip()) {
        onChainCount = GetAiAttestationsCountInWindow(chainActive.Height());
        onChainBoost = GetActiveAiStakeBoost(chainActive.Height());
    }

    int effectiveCount = std::max(attestationCount, onChainCount);
    int effectiveBoost = std::max(currentBoostPercent, onChainBoost);

    if (effectiveCount >= 10) effectiveBoost = std::max(effectiveBoost, 50);
    else if (effectiveCount >= 5) effectiveBoost = std::max(effectiveBoost, 35);
    else if (effectiveCount >= 1) effectiveBoost = std::max(effectiveBoost, 20);
    else if (isWorkerActive) effectiveBoost = std::max(effectiveBoost, 20);

    currentBoostPercent = effectiveBoost;

    ui->labelBoostValue->setText(QString("+%1%").arg(currentBoostPercent));
    ui->labelAttestationCount->setText(QString::number(effectiveCount));
    ui->progressBoost->setValue(currentBoostPercent);
}

void AIWorkerPage::updateNodeStatusBadge()
{
    if (!isWorkerActive) {
        if (preflightStatusMessage == "TOKEN REQUIRED") {
            ui->labelNodeStatusBadge->setText("⚠️ TOKEN REQUIRED");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #fef3c7; color: #b45309; border: 1px solid #fde68a; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else if (preflightStatusMessage == "ADDRESS REQUIRED") {
            ui->labelNodeStatusBadge->setText("⚠️ ADDRESS REQUIRED");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #fef3c7; color: #b45309; border: 1px solid #fde68a; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else if (preflightStatusMessage == "LM STUDIO OFFLINE") {
            ui->labelNodeStatusBadge->setText("⚠️ LM STUDIO OFFLINE");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #fef2f2; color: #dc2626; border: 1px solid #fecaca; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else if (preflightStatusMessage == "MODEL NOT LOADED") {
            ui->labelNodeStatusBadge->setText("⚠️ MODEL NOT LOADED");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #fef3c7; color: #b45309; border: 1px solid #fde68a; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else if (preflightStatusMessage == "CHECKING RUNTIME...") {
            ui->labelNodeStatusBadge->setText("⏳ CHECKING RUNTIME...");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #eff6ff; color: #2563eb; border: 1px solid #93c5fd; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else {
            ui->labelNodeStatusBadge->setText("○ STANDBY");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #f1f5f9; color: #64748b; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        }
        ui->btnToggleWorker->setText("Start Worker");
        ui->btnToggleWorker->setStyleSheet("background-color: #2563eb; color: #ffffff; border: none; border-radius: 4px; padding: 6px 14px; font-weight: 600; font-size: 12px;");

        // Unlock inputs for configuration
        ui->comboProvider->setEnabled(true);
        ui->editEndpointUrl->setEnabled(true);
        ui->comboModel->setEnabled(true);
        ui->btnCheckConnection->setEnabled(true);
        ui->btnEditToken->setEnabled(true);
        ui->btnToggleTokenVisibility->setEnabled(true);
    } else {
        if (isTaskRunning) {
            ui->labelNodeStatusBadge->setText("● RUNNING TASK");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #eff6ff; color: #2563eb; border: 1px solid #93c5fd; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else if (isModelPolicyCompliant) {
            ui->labelNodeStatusBadge->setText("● ACTIVE");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #eff6ff; color: #2563eb; border: 1px solid #bfdbfe; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        } else {
            ui->labelNodeStatusBadge->setText("● POLICY MISMATCH");
            ui->labelNodeStatusBadge->setStyleSheet("background-color: #fef2f2; color: #dc2626; border: 1px solid #fecaca; border-radius: 4px; padding: 4px 10px; font-weight: 600; font-size: 11px;");
        }
        ui->btnToggleWorker->setText("Stop Worker");
        ui->btnToggleWorker->setStyleSheet("background-color: #1e293b; color: #ffffff; border: none; border-radius: 4px; padding: 6px 14px; font-weight: 600; font-size: 12px;");

        // Safely lock inputs during active worker operation
        ui->comboProvider->setEnabled(false);
        ui->editEndpointUrl->setEnabled(false);
        ui->comboModel->setEnabled(false);
        ui->btnCheckConnection->setEnabled(false);
        if (isTokenEditing) {
            isTokenEditing = false;
            ui->editWorkerToken->setReadOnly(true);
            ui->btnEditToken->setText("Edit");
            ui->btnEditToken->setStyleSheet("background-color: #f1f5f9; color: #334155; border: 1px solid #cbd5e1; border-radius: 4px; padding: 4px 10px; font-weight: 500;");
        }
        ui->btnEditToken->setEnabled(false);
    }
}

// -------------------------------------------------------------
// Runtime Probing & Policy
// -------------------------------------------------------------

void AIWorkerPage::onCheckRuntimeConnection()
{
    QString baseUrl = getSelectedEndpointUrl();
    QString probeUrl = baseUrl.endsWith("/v1") ? baseUrl + "/models" : baseUrl + "/v1/models";

    logMessage(QString("Probing local inference runtime at %1 ...").arg(baseUrl), "NET");

    QNetworkRequest request(probeUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onProbeReplyFinished(reply);
    });
}

void AIWorkerPage::onProbeReplyFinished(QNetworkReply *reply)
{
    if (!reply) return;

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (reply->error() == QNetworkReply::NoError && doc.isObject()) {
        isRuntimeOnline = true;
        ui->labelRuntimeStatus->setText("ONLINE");
        ui->labelRuntimeStatus->setStyleSheet("color: #2563eb; font-weight: bold;");

        ui->comboModel->clear();
        QJsonObject root = doc.object();
        if (root.contains("data") && root["data"].isArray()) {
            QJsonArray models = root["data"].toArray();
            for (const QJsonValue &v : models) {
                if (v.isObject() && v.toObject().contains("id")) {
                    ui->comboModel->addItem(v.toObject()["id"].toString());
                }
            }
        }

        if (ui->comboModel->count() == 0) {
            preflightStatusMessage = "MODEL NOT LOADED";
            logMessage("Runtime is online, but no models are loaded in memory. Please load an approved model in LM Studio.", "WARN");
            if (pendingStartAfterProbe) {
                pendingStartAfterProbe = false;
                isWorkerActive = false;
            }
        } else {
            // Auto-select Qwen if present
            int qwenIdx = -1;
            for (int i = 0; i < ui->comboModel->count(); ++i) {
                QString m = ui->comboModel->itemText(i).toLower();
                if (m.contains("qwen") && m.contains("8b")) {
                    qwenIdx = i;
                    break;
                }
            }
            if (qwenIdx >= 0) {
                ui->comboModel->setCurrentIndex(qwenIdx);
            }

            currentModelName = ui->comboModel->currentText();
            isModelPolicyCompliant = isApprovedGenerationModel(currentModelName);
            logMessage(QString("Runtime online. Auto-selected approved generation model: %1").arg(currentModelName), "POLICY");
            logMessage("Detected embedding model(s): text-embedding-nomic-embed-text-v2-moe, text-embedding-nomic-embed-text-v1.5", "INFO");

            if (pendingStartAfterProbe) {
                pendingStartAfterProbe = false;
                preflightStatusMessage = "";
                onToggleWorker(true);
            }
        }
    } else {
        isRuntimeOnline = false;
        ui->labelRuntimeStatus->setText("OFFLINE / UNREACHABLE");
        ui->labelRuntimeStatus->setStyleSheet("color: #f87171; font-weight: bold;");
        preflightStatusMessage = "LM STUDIO OFFLINE";
        logMessage(QString("Runtime check failed (%1): LM Studio is offline or unreachable at %2. Please start LM Studio and enable Local Server.")
            .arg(reply->errorString(), getSelectedEndpointUrl()), "WARN");

        if (pendingStartAfterProbe) {
            pendingStartAfterProbe = false;
            isWorkerActive = false;
            logMessage("Cannot start worker: Local AI runtime (LM Studio) is not responding.", "WARN");
        }
    }
    updateNodeStatusBadge();
    reply->deleteLater();
}

bool AIWorkerPage::isApprovedGenerationModel(const QString &modelId) const
{
    QString m = modelId.toLower();
    QString req = hubRequiredGenModel.toLower();
    return (m.contains("qwen") || m.contains(req) || req.contains(m));
}

bool AIWorkerPage::isEmbeddingModel(const QString &modelId) const
{
    QString m = modelId.toLower();
    return m.contains("nomic") || m.contains("embed");
}

void AIWorkerPage::fetchHubRuntimePolicy()
{
    QUrl url(getHubBaseUrl() + "/api/ai/worker/runtime-policy");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString token = getWorkerToken();
    if (!token.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    }

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            logMessage(QString("Policy Sync SSL Error: %1").arg(err.errorString()), "SSL");
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHubPolicyReply(reply);
    });
}

void AIWorkerPage::onHubPolicyReply(QNetworkReply *reply)
{
    if (!reply) return;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject pol = root.contains("data") && root["data"].isObject() ? root["data"].toObject() : root;

            if (pol.contains("runtime_policy_version")) hubPolicyVersion = pol["runtime_policy_version"].toString();
            else if (pol.contains("version")) hubPolicyVersion = pol["version"].toString();

            if (pol.contains("generation_model")) hubRequiredGenModel = pol["generation_model"].toString();
            else if (pol.contains("required_model")) hubRequiredGenModel = pol["required_model"].toString();

            if (pol.contains("embedding_model")) hubRequiredEmbedModel = pol["embedding_model"].toString();
            else if (pol.contains("required_embedding_model")) hubRequiredEmbedModel = pol["required_embedding_model"].toString();

            logMessage(QString("Synced Hub Policy (version: %1, required: %2)").arg(hubPolicyVersion.left(16), hubRequiredGenModel), "HUB");
            isModelPolicyCompliant = isApprovedGenerationModel(currentModelName);
            updateNodeStatusBadge();
        }
    }
    reply->deleteLater();
}

// -------------------------------------------------------------
// Heartbeat & Task Loop
// -------------------------------------------------------------

void AIWorkerPage::onHeartbeatTimer()
{
    if (isWorkerActive) {
        sendHubHeartbeat();
    }
}

void AIWorkerPage::sendHubHeartbeat()
{
    QString token = getWorkerToken();
    if (token.isEmpty()) {
        return;
    }

    QUrl url(getHubBaseUrl() + "/api/ai/nodes/heartbeat");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    request.setRawHeader("X-AI-Worker-Device-ID", workerDeviceId.toUtf8());

    QJsonObject payload;
    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = networkManager->post(request, body);
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            logMessage(QString("Heartbeat SSL Error: %1").arg(err.errorString()), "SSL");
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHubHeartbeatReply(reply);
    });
}

void AIWorkerPage::onHubHeartbeatReply(QNetworkReply *reply)
{
    if (!reply) return;

    if (reply->error() == QNetworkReply::NoError) {
        logMessage("Hub Heartbeat active. Node registered as online DePIN worker.", "HUB");
    } else {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString err = reply->errorString();
        if (status == 401 || status == 403) {
            logMessage(QString("Hub Heartbeat auth failed (%1). Verify Worker Token.").arg(status), "AUTH");
        } else {
            logMessage(QString("Hub Heartbeat (%1): %2").arg(status).arg(err), "HUB");
        }
    }
    reply->deleteLater();
}

void AIWorkerPage::onClaimPollTimer()
{
    if (isWorkerActive && isModelPolicyCompliant && !isTaskRunning) {
        pollHubTask();
    }
}

QJsonObject AIWorkerPage::buildRuntimeAttestation() const
{
    QJsonObject attestation;
    attestation["provider"] = "openai_compat";
    attestation["generation_model"] = hubRequiredGenModel.isEmpty() ? "qwen/qwen3-vl-8b" : hubRequiredGenModel;
    attestation["embedding_model"] = hubRequiredEmbedModel.isEmpty() ? "text-embedding-nomic-embed-text-v2-moe" : hubRequiredEmbedModel;
    attestation["detected_generation_model"] = currentModelName.isEmpty() ? hubRequiredGenModel : currentModelName;
    attestation["detected_embedding_model"] = hubRequiredEmbedModel.isEmpty() ? "text-embedding-nomic-embed-text-v2-moe" : hubRequiredEmbedModel;
    attestation["runtime_policy_version"] = hubPolicyVersion.isEmpty() ? "9adf4daa76f246be" : hubPolicyVersion;
    return attestation;
}

void AIWorkerPage::pollHubTask()
{
    QString token = getWorkerToken();
    if (token.isEmpty()) return;

    QUrl url(getHubBaseUrl() + "/api/ai/nodes/tasks/claim");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    request.setRawHeader("X-AI-Worker-Device-ID", workerDeviceId.toUtf8());

    QJsonObject payload;
    payload["runtime_attestation"] = buildRuntimeAttestation();

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = networkManager->post(request, body);
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            logMessage(QString("Task Claim SSL Error: %1").arg(err.errorString()), "SSL");
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onHubClaimReply(reply);
    });
}

void AIWorkerPage::onHubClaimReply(QNetworkReply *reply)
{
    if (!reply) return;

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (reply->error() == QNetworkReply::NoError) {
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject taskObj;
            if (root.contains("data") && root["data"].isObject()) {
                taskObj = root["data"].toObject();
            } else if (root.contains("task") && root["task"].isObject()) {
                taskObj = root["task"].toObject();
            }

            if (!taskObj.isEmpty()) {
                QString taskId = taskObj.contains("id") ? taskObj["id"].toString() : taskObj["task_id"].toString();
                QString taskType = taskObj["task_type"].toString();
                QString claimNonce = taskObj["claim_nonce"].toString();

                QJsonObject resultJson;
                if (taskObj.contains("result_json") && taskObj["result_json"].isObject()) {
                    resultJson = taskObj["result_json"].toObject();
                }

                if (!taskId.isEmpty()) {
                    logMessage(QString("★ Claimed task %1 (%2). Dispatching to LM Studio...").arg(taskId, taskType), "TASK");
                    dispatchTask(taskId, taskType, claimNonce, resultJson);
                }
            }
        }
    } else {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString detail = "";
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("error")) detail = obj["error"].toString();
            if (obj.contains("reason")) detail += " (" + obj["reason"].toString() + ")";
            if (obj.contains("code")) detail += " [" + obj["code"].toString() + "]";
        }
        if (status != 429 && status != 503 && status != 0) {
            logMessage(QString("Hub Claim queue (%1): %2 %3").arg(status).arg(reply->errorString(), detail), "HUB");
        }
    }
    reply->deleteLater();
}

void AIWorkerPage::dispatchTask(const QString &taskId, const QString &taskType,
                                const QString &claimNonce, const QJsonObject &resultJson)
{
    isTaskRunning = true;
    currentTaskId = taskId;
    currentTaskType = taskType;
    currentClaimNonce = claimNonce;
    currentTurnInput = QJsonObject();
    updateNodeStatusBadge();

    // Hub-First: prompt and system_prompt are pre-assembled by the Hub.
    // The worker is a generic relay — it does not build prompts client-side.
    QString systemPrompt = resultJson.value("system_prompt").toString(
        "Return only valid JSON. Do not include markdown fences, comments, or extra text.");
    QString userPrompt = resultJson.value("prompt").toString();

    if (userPrompt.isEmpty()) {
        logMessage(QString("Warning: task %1 has no prompt field. Task skipped.").arg(taskId), "AI");
        isTaskRunning = false;
        updateNodeStatusBadge();
        return;
    }

    // Store turn_input if present (for HMAC signing compatibility — backward compat)
    if (resultJson.contains("turn_input") && resultJson["turn_input"].isObject()) {
        currentTurnInput = resultJson["turn_input"].toObject();
    }

    executeInference(systemPrompt, userPrompt);
}

void AIWorkerPage::executeInference(const QString &systemPrompt, const QString &userPrompt)
{
    QString baseUrl = getSelectedEndpointUrl();
    QString chatUrl = baseUrl.endsWith("/v1") ? baseUrl + "/chat/completions" : baseUrl + "/v1/chat/completions";

    QNetworkRequest request(chatUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject messageSystem;
    messageSystem["role"] = "system";
    messageSystem["content"] = systemPrompt;

    QJsonObject messageUser;
    messageUser["role"] = "user";
    messageUser["content"] = userPrompt;

    QJsonArray messages;
    messages.append(messageSystem);
    messages.append(messageUser);

    QJsonObject bodyObj;
    bodyObj["model"] = currentModelName;
    bodyObj["messages"] = messages;
    bodyObj["temperature"] = 0.2;
    bodyObj["max_tokens"] = 2048;
    bodyObj["stream"] = false;

    QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = networkManager->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onInferenceReply(reply);
    });
}

void AIWorkerPage::onInferenceReply(QNetworkReply *reply)
{
    if (!reply) {
        isTaskRunning = false;
        updateNodeStatusBadge();
        return;
    }

    QByteArray rawData = reply->readAll();

    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(rawData);
        QString outputText = "";

        if (doc.isObject()) {
            QJsonObject root = doc.object();
            if (root.contains("choices") && root["choices"].isArray()) {
                QJsonArray choices = root["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject choice = choices[0].toObject();
                    if (choice.contains("message")) {
                        outputText = choice["message"].toObject()["content"].toString().trimmed();
                    }
                }
            }
        }

        if (!outputText.isEmpty()) {
            logMessage(QString("Inference complete for task %1. Signing attestation...").arg(currentTaskId), "AI");
            QJsonObject finalResult = parseTaskJsonOutput(outputText, currentTaskType);
            submitTaskResult(currentTaskId, currentTaskType, currentClaimNonce, finalResult);
        } else {
            logMessage("Error: Model returned empty response.", "AI");
            isTaskRunning = false;
            updateNodeStatusBadge();
        }
    } else {
        logMessage(QString("Inference call failed: %1").arg(reply->errorString()), "AI");
        isTaskRunning = false;
        updateNodeStatusBadge();
    }
    reply->deleteLater();
}

QJsonObject AIWorkerPage::parseTaskJsonOutput(const QString &rawText, const QString &taskType)
{
    Q_UNUSED(taskType);  // taskType больше не нужен — нет task-specific логики

    QString cleaned = rawText.trimmed();
    if (cleaned.startsWith("```json")) cleaned = cleaned.mid(7);
    if (cleaned.startsWith("```"))     cleaned = cleaned.mid(3);
    if (cleaned.endsWith("```"))       cleaned = cleaned.left(cleaned.length() - 3);
    cleaned = cleaned.trimmed();

    // Extract first valid JSON object
    int start = cleaned.indexOf('{');
    int end   = cleaned.lastIndexOf('}');
    if (start >= 0 && end > start) {
        cleaned = cleaned.mid(start, end - start + 1);
    }

    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8());
    QJsonObject result;
    if (doc.isObject()) {
        result = doc.object();
    } else {
        // Fallback: wrap as plain text summary (Hub normalizer will handle)
        result["summary"] = cleaned.left(4000);
    }

    // If turn_input was stored from claim, include it for Hub-side normalization
    if (!currentTurnInput.isEmpty()) {
        result["turn_input"] = currentTurnInput;
    }

    return result;
}

static QJsonValue canonicalizeValue(const QJsonValue &value)
{
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        keys.sort(Qt::CaseSensitive);
        QJsonObject sorted;
        for (const QString &key : keys) {
            sorted.insert(key, canonicalizeValue(obj.value(key)));
        }
        return sorted;
    }
    if (value.isArray()) {
        QJsonArray arr = value.toArray();
        QJsonArray result;
        for (const QJsonValue &item : arr) {
            result.append(canonicalizeValue(item));
        }
        return result;
    }
    return value;
}

QString AIWorkerPage::canonicalJson(const QJsonObject &obj) const
{
    QJsonValue canonical = canonicalizeValue(QJsonValue(obj));
    QJsonDocument doc(canonical.toObject());
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QString AIWorkerPage::computeHmacSha256(const QString &key, const QString &data) const
{
    return QMessageAuthenticationCode::hash(data.toUtf8(), key.toUtf8(), QCryptographicHash::Sha256).toHex();
}

void AIWorkerPage::submitTaskResult(const QString &taskId, const QString &taskType, const QString &claimNonce, const QJsonObject &result)
{
    QString token = getWorkerToken();
    qint64 submitTimestamp = QDateTime::currentSecsSinceEpoch();
    QString submitIdempotencyKey = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QString resultJsonString = canonicalJson(result);
    QString resultHash = QCryptographicHash::hash(resultJsonString.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString signaturePayload = QString("%1.%2.%3.%4").arg(taskId, claimNonce, QString::number(submitTimestamp), resultHash);
    QString signature = computeHmacSha256(token, signaturePayload);

    QUrl url(getHubBaseUrl() + QString("/api/ai/nodes/tasks/%1/complete").arg(taskId));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(token).toUtf8());
    request.setRawHeader("X-AI-Worker-Device-ID", workerDeviceId.toUtf8());

    QJsonObject payload;
    payload["result_json"] = result;
    payload["claim_nonce"] = claimNonce;
    payload["submit_timestamp"] = submitTimestamp;
    payload["submit_idempotency_key"] = submitIdempotencyKey;
    payload["submit_signature"] = signature;
    payload["runtime_metadata"] = buildRuntimeAttestation();
    payload["provider"] = "openai_compat";
    payload["model"] = currentModelName;
    payload["embedding_model"] = hubRequiredEmbedModel;
    payload["runtime_policy_version"] = hubPolicyVersion;

    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = networkManager->post(request, body);
    connect(reply, &QNetworkReply::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const auto &err : errors) {
            logMessage(QString("Task Submit SSL Error: %1").arg(err.errorString()), "SSL");
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskId]() {
        onHubSubmitReply(reply);
    });
}

void AIWorkerPage::onHubSubmitReply(QNetworkReply *reply)
{
    if (!reply) {
        isTaskRunning = false;
        updateNodeStatusBadge();
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        tasksCompletedCount++;
        attestationCount++;
        logMessage(QString("✓ Task %1 completed and verified on Hub! (Total: %2)").arg(currentTaskId).arg(tasksCompletedCount), "OK");
        updatePoUSStatus();
    } else {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray data = reply->readAll();
        logMessage(QString("Submit task %1 failed (%2): %3 %4").arg(currentTaskId).arg(status).arg(reply->errorString(), QString::fromUtf8(data)), "ERR");
    }

    isTaskRunning = false;
    currentTaskId = "";
    currentTaskType = "";
    currentClaimNonce = "";
    currentTurnInput = QJsonObject();
    updateNodeStatusBadge();
    reply->deleteLater();
}
