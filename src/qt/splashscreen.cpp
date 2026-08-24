// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include "splashscreen.h"

#include "guiconstants.h"
#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>

SplashScreen::SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0)
{
    // set reference point, paddings
    const int iconAreaWidth     = 220;
    int paddingRight            = 20;
    int paddingTop              = 52;
    int titleVersionGap         = 10;
    int titleCopyrightGap       = 28;

    float fontFactor            = 1.0;
    float devicePixelRatio      = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = ((QGuiApplication*)QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText       = tr(PACKAGE_NAME);
    QString versionText     = QString("Version v%1.%2.%3")
        .arg(CLIENT_VERSION_MAJOR)
        .arg(CLIENT_VERSION_MINOR)
        .arg(CLIENT_VERSION_BUILD);

    const QChar copySign(0xA9);
    QString copyrightTextLegacy = copySign + QString(" 2009-%1 The Bitcoin Core developers  ·  2014-2024 Blackcoin / Blackmore").arg(COPYRIGHT_YEAR);
    QString copyrightTextQuavence = copySign + QString(" %1 The Quavence developers").arg(COPYRIGHT_YEAR);

    QString titleAddText    = networkStyle->getTitleAddText();

    QString font            = QApplication::font().toString();

    // create a bitmap according to device pixelratio
    QSize splashSize(620*devicePixelRatio, 360*devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    // change to HiDPI if it makes sense
    pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);

    // draw a slightly radial gradient
    QRadialGradient gradient(QPoint(0,0), splashSize.width()/devicePixelRatio);
    gradient.setColorAt(0, Qt::white);
    gradient.setColorAt(1, QColor(247,247,247));
    QRect rGradient(QPoint(0,0), splashSize);
    pixPaint.fillRect(rGradient, gradient);

    // App icon (left): Logo2 brand on splash; Logo5 tray/window icon stays in NetworkStyle.
    const int splashW = pixmap.width() / devicePixelRatio;
    const int textLeft = iconAreaWidth + 10;
    const int textWidth = splashW - textLeft - paddingRight;

    const int iconDrawSize = 168;
    const int iconPad = 6;
    const int iconX = (iconAreaWidth - iconDrawSize) / 2;
    const int iconY = 72;

    // White pad so Logo2 reads cleanly on the gradient (see QUAVENCE_BRAND_ASSETS.md).
    pixPaint.setPen(Qt::NoPen);
    pixPaint.setBrush(Qt::white);
    pixPaint.drawEllipse(iconX - iconPad, iconY - iconPad, iconDrawSize + iconPad * 2, iconDrawSize + iconPad * 2);

    const QSize iconSize(iconDrawSize * devicePixelRatio, iconDrawSize * devicePixelRatio);
    QPixmap iconPixmap(":/icons/quavence-brand");
    if (iconPixmap.isNull()) {
        iconPixmap = networkStyle->getAppIcon().pixmap(iconSize);
    } else {
        iconPixmap = iconPixmap.scaled(
            iconSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    iconPixmap.setDevicePixelRatio(devicePixelRatio);
    pixPaint.drawPixmap(iconX, iconY, iconDrawSize, iconDrawSize, iconPixmap);

    // Title and version (right column)
    pixPaint.setPen(COLOR_BRAND_PRIMARY);
    pixPaint.setFont(QFont(font, 33*fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = fm.width(titleText);
    if (titleTextWidth > textWidth) {
        fontFactor = fontFactor * textWidth / titleTextWidth;
    }

    QFont titleFont(font, int(32 * fontFactor), QFont::Bold);
    pixPaint.setFont(titleFont);
    pixPaint.setPen(QColor(15, 23, 42)); // Modern deep slate
    fm = pixPaint.fontMetrics();
    const int textX = textLeft + 10;
    const int titleBaseline = paddingTop + 16 + fm.ascent();
    pixPaint.drawText(textX, titleBaseline, titleText);

    pixPaint.setPen(QColor(100, 116, 139)); // Slate muted
    int versionFontSize = qMax(10, int(14 * fontFactor));
    pixPaint.setFont(QFont(font, versionFontSize, QFont::Medium));
    QFontMetrics versionFm = pixPaint.fontMetrics();

    // if the version string is too long, reduce size
    int versionTextWidth = versionFm.width(versionText);
    if (versionTextWidth > textWidth) {
        versionFontSize = qMax(10, int(10 * fontFactor));
        pixPaint.setFont(QFont(font, versionFontSize));
        versionFm = pixPaint.fontMetrics();
        versionTextWidth = versionFm.width(versionText);
    }
    const int versionBaseline = titleBaseline + fm.descent() + 6 + versionFm.ascent();
    pixPaint.drawText(textX, versionBaseline, versionText);

    // Copyright (Clean left aligned with Title & Version)
    {
        const int crFontSize = qMax(7, int(8 * fontFactor));
        pixPaint.setFont(QFont(font, crFontSize));
        const int y = versionBaseline + versionFm.descent() + 20;
        const int lineH = crFontSize + 4;
        pixPaint.setPen(QColor(37, 99, 235));
        pixPaint.drawText(QRect(textX, y, textWidth, lineH), Qt::AlignLeft | Qt::AlignTop, copyrightTextQuavence);
        pixPaint.setPen(QColor(148, 163, 184));
        pixPaint.drawText(QRect(textX, y + lineH, textWidth, lineH * 2), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, copyrightTextLegacy);
    }

    // draw additional text if special network
    if(!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 10*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth  = fm.width(titleAddText);
        pixPaint.drawText(splashW - titleAddTextWidth - 10, 15, titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
static void ConnectWallet(SplashScreen *splash, CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, splash, _1, _2));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(ConnectWallet, this, _1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
#ifdef ENABLE_WALLET
    if(pwalletMain)
        pwalletMain->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
