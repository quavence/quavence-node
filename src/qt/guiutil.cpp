// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "guiutil.h"

#include <QApplication>
#include "guiconstants.h"

#include <QPalette>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

#include "bitcoinaddressvalidator.h"
#include "bitcoinunits.h"
#include "qvalidatedlineedit.h"
#include "walletmodel.h"

#include "primitives/transaction.h"
#include "cashaddr.h"
#include "config.h"
#include "dstencode.h"
#include "init.h"
#include "main.h" // For minRelayTxFee
#include "protocol.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include "utilstrencodings.h"

#ifdef WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "shellapi.h"
#include "shlobj.h"
#include "shlwapi.h"
#endif

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#if BOOST_FILESYSTEM_VERSION >= 3
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>
#endif
#include <boost/scoped_array.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDesktopWidget>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QSettings>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>

#if QT_VERSION < 0x050000
#include <QUrl>
#else
#include <QUrlQuery>
#endif

#if QT_VERSION >= 0x50200
#include <QFontDatabase>
#endif

#if BOOST_FILESYSTEM_VERSION >= 3
static boost::filesystem::detail::utf8_codecvt_facet utf8;
#endif

#if defined(Q_OS_MAC)

#include <QProcess>
extern double NSAppKitVersionNumber;
#if !defined(NSAppKitVersionNumber10_8)
#define NSAppKitVersionNumber10_8 1187
#endif
#if !defined(NSAppKitVersionNumber10_9)
#define NSAppKitVersionNumber10_9 1265
#endif
void ForceActivation();
#endif

namespace GUIUtil {

QString dateTimeStr(const QDateTime &date)
{
    return date.date().toString(Qt::SystemLocaleShortDate) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromTime_t((qint32)nTime));
}

QFont fixedPitchFont()
{
#if QT_VERSION >= 0x50200
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
#else
    QFont font("Monospace");
#if QT_VERSION >= 0x040800
    font.setStyleHint(QFont::Monospace);
#else
    font.setStyleHint(QFont::TypeWriter);
#endif
    return font;
#endif
}

static std::string MakeAddrInvalid(std::string addr)
{
    if (addr.size() < 2)
    {
        return "";
    }

    // Checksum is at the end of the address. Swapping chars to make it invalid.
    std::swap(addr[addr.size() - 1], addr[addr.size() - 2]);
    if (!IsValidDestinationString(addr))
    {
        return addr;
    }
    return "";
}

std::string DummyAddress(const CChainParams &params, const Config &cfg)
{
    // Just some dummy data to generate an convincing random-looking (but
    // consistent) address
    static const std::vector<uint8_t> dummydata = {0xeb, 0x15, 0x23, 0x1d, 0xfc, 0xeb, 0x60, 0x92, 0x58, 0x86, 0xb6,
        0x7d, 0x06, 0x52, 0x99, 0x92, 0x59, 0x15, 0xae, 0xb1};

    const CTxDestination dstKey = CKeyID(uint160(dummydata));
    return MakeAddrInvalid(EncodeDestination(dstKey, params, cfg));
}

void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent)
{
    parent->setFocusProxy(widget);

    widget->setFont(fixedPitchFont());
    const CChainParams &params = Params();
#if QT_VERSION >= 0x040700
    // We don't want translators to use own addresses in translations
    // and this is the only place, where this address is supplied.
    widget->setPlaceholderText(QObject::tr("Enter a Quavence address (e.g. %1)")
                                   .arg(QString::fromStdString(DummyAddress(params, GetConfig()))));
#endif
    widget->setValidator(new BitcoinAddressEntryValidator(params.CashAddrPrefix(), parent));
    widget->setCheckValidator(new BitcoinAddressCheckValidator(parent));
}

void setupAmountWidget(QLineEdit *widget, QWidget *parent)
{
    QDoubleValidator *amountValidator = new QDoubleValidator(parent);
    amountValidator->setDecimals(8);
    amountValidator->setBottom(0.0);
    widget->setValidator(amountValidator);
    widget->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
}

QString bitcoinURIScheme(const CChainParams &params, bool useCashAddr)
{
    if (!useCashAddr)
    {
        return "quavence";
    }
    return QString::fromStdString(params.CashAddrPrefix());
}

QString bitcoinURIScheme(const Config &cfg)
{
    return bitcoinURIScheme(cfg.GetChainParams(), cfg.UseCashAddrEncoding());
}

static bool IsCashAddrEncoded(const QUrl &uri)
{
    const std::string addr = (uri.scheme() + ":" + uri.path()).toStdString();
    auto decoded = cashaddr::Decode(addr, "");
    return !decoded.first.empty();
}

bool parseBitcoinURI(const QString &scheme, const QUrl &uri, SendCoinsRecipient *out)
{
    // return if URI has wrong scheme.
    if (!uri.isValid() || uri.scheme() != scheme)
    {
        return false;
    }

    SendCoinsRecipient rv;
    if (IsCashAddrEncoded(uri))
    {
        rv.address = uri.scheme() + ":" + uri.path();
    }
    else
    {
        // strip out uri scheme for base58 encoded addresses
        rv.address = uri.path();
    }
    // Trim any following forward slash which may have been added by the OS
    if (rv.address.endsWith("/"))
    {
        rv.address.truncate(rv.address.length() - 1);
    }
    rv.amount = 0;

#if QT_VERSION < 0x050000
    QList<QPair<QString, QString> > items = uri.queryItems();
#else
    QUrlQuery uriQuery(uri);
    QList<QPair<QString, QString> > items = uriQuery.queryItems();
#endif
    for (QList<QPair<QString, QString> >::iterator i = items.begin(); i != items.end(); i++)
    {
        bool fShouldReturnFalse = false;
        if (i->first.startsWith("req-"))
        {
            i->first.remove(0, 4);
            fShouldReturnFalse = true;
        }

        if (i->first == "label")
        {
            rv.label = i->second;
            fShouldReturnFalse = false;
        }
        if (i->first == "message")
        {
            rv.message = i->second;
            fShouldReturnFalse = false;
        }
        else if (i->first == "amount")
        {
            if (!i->second.isEmpty())
            {
                if (!BitcoinUnits::parse(BitcoinUnits::BTC, i->second, &rv.amount))
                {
                    return false;
                }
            }
            fShouldReturnFalse = false;
        }

        if (fShouldReturnFalse)
            return false;
    }
    if (out)
    {
        *out = rv;
    }
    return true;
}

bool parseBitcoinURI(const QString &scheme, QString uri, SendCoinsRecipient *out)
{
    //
    //    Cannot handle this later, because quavence://
    //    will cause Qt to see the part after // as host,
    //    which will lower-case it (and thus invalidate the address).
    if (uri.startsWith(scheme + "://", Qt::CaseInsensitive))
    {
        uri.replace(0, scheme.length() + 3, scheme + ":");
    }
    QUrl uriInstance(uri);
    return parseBitcoinURI(scheme, uriInstance, out);
}

QString formatBitcoinURI(const Config &cfg, const SendCoinsRecipient &info)
{
    QString ret = info.address;
    if (!cfg.UseCashAddrEncoding())
    {
        // prefix address with uri scheme for base58 encoded addresses.
        ret = (bitcoinURIScheme(cfg) + ":%1").arg(ret);
    }
    int paramCount = 0;

    if (info.amount)
    {
        ret += QString("?amount=%1").arg(BitcoinUnits::format(BitcoinUnits::BTC, info.amount, false, BitcoinUnits::separatorNever));
        paramCount++;
    }

    if (!info.label.isEmpty())
    {
        QString lbl(QUrl::toPercentEncoding(info.label));
        ret += QString("%1label=%2").arg(paramCount == 0 ? "?" : "&").arg(lbl);
        paramCount++;
    }

    if (!info.message.isEmpty())
    {
        QString msg(QUrl::toPercentEncoding(info.message));
        ret += QString("%1message=%2").arg(paramCount == 0 ? "?" : "&").arg(msg);
        paramCount++;
    }

    return ret;
}

QString formatQvncDepositURI(const QString &address)
{
    return QStringLiteral("qvnc:%1").arg(address.trimmed());
}

bool isDust(const QString& address, const CAmount& amount)
{
    CTxDestination dest = DecodeDestination(address.toStdString());
    CScript script = GetScriptForDestination(dest);
    CTxOut txOut(amount, script);
    return txOut.IsDust(::minRelayTxFee);
}

QString HtmlEscape(const QString& str, bool fMultiLine)
{
#if QT_VERSION < 0x050000
    QString escaped = Qt::escape(str);
#else
    QString escaped = str.toHtmlEscaped();
#endif
    if(fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString HtmlEscape(const std::string& str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

void copyEntryData(QAbstractItemView *view, int column, int role)
{
    if(!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if(!selection.isEmpty())
    {
        // Copy first item
        setClipboard(selection.at(0).data(role).toString());
    }
}

QVariant getEntryData(QAbstractItemView *view, int column, int role)
{
    if(!view || !view->selectionModel())
        return QVariant();
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if(!selection.isEmpty()) {
        // Return first item
        return (selection.at(0).data(role));
    }
    return QVariant();
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
#if QT_VERSION < 0x050000
        myDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter));

    /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
    QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
    QString selectedSuffix;
    if(filter_re.exactMatch(selectedFilter))
    {
        selectedSuffix = filter_re.cap(1);
    }

    /* Add suffix if needed */
    QFileInfo info(result);
    if(!result.isEmpty())
    {
        if(info.suffix().isEmpty() && !selectedSuffix.isEmpty())
        {
            /* No suffix specified, add selected suffix */
            if(!result.endsWith("."))
                result.append(".");
            result.append(selectedSuffix);
        }
    }

    /* Return selected suffix if asked to */
    if(selectedSuffixOut)
    {
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
#if QT_VERSION < 0x050000
        myDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#else
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#endif
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getOpenFileName(parent, caption, myDir, filter, &selectedFilter));

    if(selectedSuffixOut)
    {
        /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
        QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
        QString selectedSuffix;
        if(filter_re.exactMatch(selectedFilter))
        {
            selectedSuffix = filter_re.cap(1);
        }
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

Qt::ConnectionType blockingGUIThreadConnection()
{
    if(QThread::currentThread() != qApp->thread())
    {
        return Qt::BlockingQueuedConnection;
    }
    else
    {
        return Qt::DirectConnection;
    }
}

bool checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
    if (!atW) return false;
    return atW->topLevelWidget() == w;
}

bool isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w)
        && checkPoint(QPoint(w->width() - 1, 0), w)
        && checkPoint(QPoint(0, w->height() - 1), w)
        && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
        && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

void openDebugLogfile()
{
    boost::filesystem::path pathDebug = GetDataDir() / "debug.log";

    /* Open debug.log with the associated application */
    if (boost::filesystem::exists(pathDebug))
        QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathDebug)));
}

bool openBitcoinConf()
{
    boost::filesystem::path pathConfig = GetConfigFile();

    /* Create the file */
    boost::filesystem::ofstream configFile(pathConfig, std::ios_base::app);
    
    if (!configFile.good())
        return false;
    
    configFile.close();
    
    /* Open bitcoin.conf with the associated application */
    return QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathConfig)));
}

void SubstituteFonts(const QString& language)
{
#if defined(Q_OS_MAC)
// Background:
// OSX's default font changed in 10.9 and Qt is unable to find it with its
// usual fallback methods when building against the 10.7 sdk or lower.
// The 10.8 SDK added a function to let it find the correct fallback font.
// If this fallback is not properly loaded, some characters may fail to
// render correctly.
//
// The same thing happened with 10.10. .Helvetica Neue DeskInterface is now default.
//
// Solution: If building with the 10.7 SDK or lower and the user's platform
// is 10.9 or higher at runtime, substitute the correct font. This needs to
// happen before the QApplication is created.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_8
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_8)
    {
        if (floor(NSAppKitVersionNumber) <= NSAppKitVersionNumber10_9)
            /* On a 10.9 - 10.9.x system */
            QFont::insertSubstitution(".Lucida Grande UI", "Lucida Grande");
        else
        {
            /* 10.10 or later system */
            if (language == "zh_CN" || language == "zh_TW" || language == "zh_HK") // traditional or simplified Chinese
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Heiti SC");
            else if (language == "ja") // Japanesee
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Songti SC");
            else
              QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Lucida Grande");
        }
    }
#endif
#endif
}

static QString brandStyleSheet()
{
    const QString primary = COLOR_BRAND_PRIMARY.name();
    const QString accent = COLOR_BRAND_ACCENT.name();
    const QString muted = COLOR_BRAND_MUTED.name();
    const QString border = COLOR_BRAND_BORDER.name();
    const QString divider = COLOR_BRAND_DIVIDER.name();
    const QString highlightBg = QString("rgba(%1,%2,%3,0.12)")
        .arg(COLOR_BRAND_PRIMARY.red()).arg(COLOR_BRAND_PRIMARY.green()).arg(COLOR_BRAND_PRIMARY.blue());

    return QString(
        "QMainWindow, QDialog, QWidget#centralWidget { background: #ffffff; color: #111418; }"
        "QWidget { color: #111418; }"
        "QLabel { color: #111418; background: transparent; }"
        "QLabel:disabled { color: %1; }"
        "QMenuBar { background: #ffffff; color: %1; border-bottom: 1px solid %2; padding: 1px 0; }"
        "QMenuBar::item { padding: 3px 8px; color: %1; background: transparent; }"
        "QMenuBar::item:selected { background: %3; color: %4; }"
        "QMenu { background: #ffffff; color: #111418; border: 1px solid %5; padding: 4px 0; }"
        "QMenu::item { padding: 4px 24px 4px 20px; color: #111418; background: transparent; }"
        "QMenu::item:selected { background: %3; color: %4; }"
        "QMenu::item:disabled { color: %1; }"
        "QMenu::separator { height: 1px; background: %2; margin: 4px 8px; }"
        "QToolBar { background: #ffffff; border-bottom: 1px solid %2; spacing: 4px; padding: 1px 4px; }"
        "QToolBar QToolButton { color: %1; padding: 3px 10px; border: none; border-radius: 0; background: transparent; }"
        "QToolBar QToolButton:hover { background: %3; color: %4; }"
        "QToolBar QToolButton:checked { color: %4; background: %3; border: none; border-bottom: 2px solid %4; border-radius: 0; margin-bottom: -1px; }"
        "QStatusBar { background: #ffffff; color: %1; border-top: 1px solid %2; }"
        "QStatusBar QLabel { color: %1; }"
        "QTabBar::tab { color: %1; padding: 6px 12px; border-radius: 0; background: transparent; }"
        "QTabBar::tab:selected { color: %4; border-bottom: 2px solid %4; border-radius: 0; }"
        "QGroupBox { font-weight: bold; color: %4; border: 1px solid %5; border-radius: 8px; margin-top: 8px; padding-top: 12px; background: #ffffff; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: %4; background: #ffffff; }"
        "QPushButton { padding: 5px 14px; border-radius: 6px; border: 1px solid %5; background: #ffffff; color: #111418; }"
        "QPushButton:hover { border-color: %4; color: %4; background: %3; }"
        "QPushButton:default { background: %4; color: #ffffff; border: 1px solid %4; }"
        "QPushButton:default:hover { background: %6; border-color: %6; }"
        "QPushButton:disabled { background: #f8f9fa; color: %1; border-color: %2; }"
        "QHeaderView::section { background: #ffffff; color: %1; padding: 4px; border: none; border-bottom: 1px solid %2; }"
        "QTableView, QTreeView, QListView { background: #ffffff; color: #111418; gridline-color: %2; selection-background-color: %3; selection-color: #111418; alternate-background-color: #f8f9fa; }"
        "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox { border: 1px solid %5; border-radius: 6px; padding: 3px 6px; background: #ffffff; color: #111418; selection-background-color: %3; selection-color: #111418; }"
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: %4; }"
        "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled { background: #f8f9fa; color: %1; border-color: %2; }"
        "QComboBox QAbstractItemView { background: #ffffff; color: #111418; selection-background-color: %3; selection-color: %4; border: 1px solid %5; }"
        "QToolTip { background: #111418; color: #ffffff; border: 1px solid #111418; border-radius: 4px; padding: 4px 8px; }"
        "QProgressBar { border: 1px solid %5; border-radius: 6px; background: %2; text-align: center; color: #ffffff; font-weight: bold; }"
        "QProgressBar::chunk { background: %4; border-radius: 5px; }"
        "QScrollBar:vertical { background: #ffffff; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %5; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: %1; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    ).arg(muted, divider, highlightBg, primary, border, accent);
}

void InitBrandPalette()
{
    QPalette pal = QApplication::palette();
    const QColor textPrimary = COLOR_BRAND_TEXT;
    const QColor textMuted = COLOR_BRAND_MUTED;
    const QColor bgWhite(Qt::white);
    const QColor bgAlt(248, 249, 250);
    const QColor bgHighlight(240, 247, 255);

    // Active & Inactive states
    pal.setColor(QPalette::Window, bgWhite);
    pal.setColor(QPalette::WindowText, textPrimary);
    pal.setColor(QPalette::Base, bgWhite);
    pal.setColor(QPalette::AlternateBase, bgAlt);
    pal.setColor(QPalette::Text, textPrimary);
    pal.setColor(QPalette::Button, bgWhite);
    pal.setColor(QPalette::ButtonText, textPrimary);
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, textPrimary);
    pal.setColor(QPalette::ToolTipText, Qt::white);
    pal.setColor(QPalette::Highlight, bgHighlight);
    pal.setColor(QPalette::HighlightedText, COLOR_BRAND_PRIMARY);
    pal.setColor(QPalette::Link, COLOR_BRAND_ACCENT);
    pal.setColor(QPalette::LinkVisited, COLOR_BRAND_PRIMARY.darker(115));

    // Disabled state
    pal.setColor(QPalette::Disabled, QPalette::Window, bgWhite);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Base, bgWhite);
    pal.setColor(QPalette::Disabled, QPalette::Text, textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Button, bgWhite);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, textMuted);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(220, 225, 230));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, textMuted);

    QApplication::setPalette(pal);
    qApp->setStyleSheet(brandStyleSheet());
}

QIcon txTypeDotIcon(const QColor &color, int diameter)
{
    const int d = qMax(4, diameter);
    QPixmap pm(d, d);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(0, 0, d - 1, d - 1);
    painter.end();
    return QIcon(pm);
}

namespace {


static QPainterPath toolbarAIWorkerShape()
{
    QPainterPath path;
    // Central chip body (rounded rectangle)
    path.addRoundedRect(QRectF(7, 7, 10, 10), 2, 2);
    // Pins top
    path.addRect(QRectF(9, 4, 1.5, 3));
    path.addRect(QRectF(13.5, 4, 1.5, 3));
    // Pins bottom
    path.addRect(QRectF(9, 17, 1.5, 3));
    path.addRect(QRectF(13.5, 17, 1.5, 3));
    // Pins left
    path.addRect(QRectF(4, 9, 3, 1.5));
    path.addRect(QRectF(4, 13.5, 3, 1.5));
    // Pins right
    path.addRect(QRectF(17, 9, 3, 1.5));
    path.addRect(QRectF(17, 13.5, 3, 1.5));
    return path;
}

static QPainterPath toolbarGlyphsShape()
{
    QPainterPath path;
    // Brilliant cut faceted diamond / PoUS Glyph
    // Table facet (top center)
    QPainterPath table;
    table.moveTo(8.8, 4.8);
    table.lineTo(15.2, 4.8);
    table.lineTo(13.6, 8.8);
    table.lineTo(10.4, 8.8);
    table.closeSubpath();
    path.addPath(table);

    // Top left facet
    QPainterPath topL;
    topL.moveTo(7.8, 5.0);
    topL.lineTo(9.4, 8.8);
    topL.lineTo(4.6, 9.2);
    topL.closeSubpath();
    path.addPath(topL);

    // Top right facet
    QPainterPath topR;
    topR.moveTo(16.2, 5.0);
    topR.lineTo(19.4, 9.2);
    topR.lineTo(14.6, 8.8);
    topR.closeSubpath();
    path.addPath(topR);

    // Bottom center facet (kite/shield)
    QPainterPath botCenter;
    botCenter.moveTo(10.4, 10.2);
    botCenter.lineTo(13.6, 10.2);
    botCenter.lineTo(12.0, 18.8);
    botCenter.closeSubpath();
    path.addPath(botCenter);

    // Bottom left facet
    QPainterPath botL;
    botL.moveTo(4.6, 10.2);
    botL.lineTo(9.4, 10.2);
    botL.lineTo(11.0, 18.0);
    botL.closeSubpath();
    path.addPath(botL);

    // Bottom right facet
    QPainterPath botR;
    botR.moveTo(19.4, 10.2);
    botR.lineTo(12.8, 18.0);
    botR.lineTo(14.6, 10.2);
    botR.closeSubpath();
    path.addPath(botR);

    return path;
}

static QPainterPath toolbarOverviewShape()
{
    QPainterPath path;
    path.addRect(QRectF(4, 8.5, 16, 11));
    path.addRoundedRect(QRectF(6, 5.5, 12, 1.5), 1, 1);
    path.addRect(QRectF(8, 18, 8, 1.5));
    return path;
}

static QPainterPath toolbarSendShape()
{
    QPainterPath path;
    QPainterPath top;
    top.moveTo(5, 7.5);
    top.lineTo(16.2, 7.5);
    top.lineTo(14, 5.3);
    top.lineTo(15.4, 3.9);
    top.lineTo(20, 8.5);
    top.lineTo(15.4, 13.1);
    top.lineTo(14, 11.7);
    top.lineTo(16.2, 9.5);
    top.lineTo(5, 9.5);
    top.closeSubpath();

    QPainterPath bottom;
    bottom.moveTo(19, 16.5);
    bottom.lineTo(7.8, 16.5);
    bottom.lineTo(10, 18.7);
    bottom.lineTo(8.6, 20.1);
    bottom.lineTo(4, 15.5);
    bottom.lineTo(8.6, 10.9);
    bottom.lineTo(10, 12.3);
    bottom.lineTo(7.8, 14.5);
    bottom.lineTo(19, 14.5);
    bottom.closeSubpath();

    path.addPath(top);
    path.addPath(bottom);
    return path;
}

static QPainterPath toolbarReceiveShape()
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    QPainterPath house;
    house.moveTo(12, 3.5);
    house.lineTo(5, 8.5);
    house.lineTo(5, 17.5);
    house.lineTo(19, 17.5);
    house.lineTo(19, 8.5);
    house.closeSubpath();
    path.addPath(house);

    QPainterPath inner;
    inner.moveTo(12, 6.1);
    inner.lineTo(16.5, 9.3);
    inner.lineTo(16.5, 16);
    inner.lineTo(7.5, 16);
    inner.lineTo(7.5, 9.3);
    inner.closeSubpath();
    path.addPath(inner);

    path.addRect(QRectF(11, 11, 2, 5));
    return path;
}

static void appendArcPolyline(QPainterPath &path, qreal cx, qreal cy, qreal r,
                              qreal startDeg, qreal sweepDeg)
{
    const int steps = qMax(10, int(qAbs(sweepDeg) / 8));
    for (int i = 1; i <= steps; ++i) {
        const qreal deg = startDeg + sweepDeg * qreal(i) / qreal(steps);
        const qreal rad = qDegreesToRadians(deg);
        path.lineTo(cx + r * qCos(rad), cy + r * qSin(rad));
    }
}

static QPainterPath toolbarTransactionsShape()
{
    QPainterPath path;
    // filled/transactions.svg — ring + arrow (sampled arcs, same geometry as preview)
    path.moveTo(12, 4);
    appendArcPolyline(path, 12, 12, 8, -90, -270);
    path.lineTo(18, 12);
    appendArcPolyline(path, 12, 12, 6, 0, 309);
    path.lineTo(14, 10);
    path.lineTo(20, 10);
    path.lineTo(20, 4);
    path.lineTo(17.5, 6.5);
    appendArcPolyline(path, 12, 12, 7.9, 315, -45);
    path.closeSubpath();

    path.addRect(QRectF(11, 8, 2, 4.2));
    QPainterPath hour;
    hour.moveTo(13, 12.2);
    hour.lineTo(16.2, 14.1);
    hour.lineTo(15.3, 15.7);
    hour.lineTo(11.5, 13.4);
    hour.closeSubpath();
    path.addPath(hour);
    return path;
}

} // namespace

QIcon brandToolbarIcon(BrandToolbarIcon icon, const QColor &color)
{
    static const int kPx = 40;
    QPixmap pm(kPx, kPx);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.scale(kPx / 24.0, kPx / 24.0);

    switch (icon) {
    case BrandToolbarIcon::Overview:
        painter.drawPath(toolbarOverviewShape());
        break;
    case BrandToolbarIcon::Send:
        painter.drawPath(toolbarSendShape());
        break;
    case BrandToolbarIcon::Receive:
        painter.drawPath(toolbarReceiveShape());
        break;
    case BrandToolbarIcon::Transactions:
        painter.drawPath(toolbarTransactionsShape());
        break;
    case BrandToolbarIcon::AIWorker:
        painter.drawPath(toolbarAIWorkerShape());
        break;
    case BrandToolbarIcon::Glyphs:
        painter.drawPath(toolbarGlyphsShape());
        break;
    }

    painter.end();
    return QIcon(pm);
}

ToolTipToRichTextFilter::ToolTipToRichTextFilter(int size_threshold, QObject *parent) :
    QObject(parent),
    size_threshold(size_threshold)
{

}

bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt)
{
    if(evt->type() == QEvent::ToolTipChange)
    {
        QWidget *widget = static_cast<QWidget*>(obj);
        QString tooltip = widget->toolTip();
        if(tooltip.size() > size_threshold && !tooltip.startsWith("<qt") && !Qt::mightBeRichText(tooltip))
        {
            // Envelop with <qt></qt> to make sure Qt detects this as rich text
            // Escape the current message as HTML and replace \n by <br>
            tooltip = "<qt>" + HtmlEscape(tooltip, true) + "</qt>";
            widget->setToolTip(tooltip);
            return true;
        }
    }
    return QObject::eventFilter(obj, evt);
}

void TableViewLastColumnResizingFixer::connectViewHeadersSignals()
{
    connect(tableView->horizontalHeader(), SIGNAL(sectionResized(int,int,int)), this, SLOT(on_sectionResized(int,int,int)));
    connect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// We need to disconnect these while handling the resize events, otherwise we can enter infinite loops.
void TableViewLastColumnResizingFixer::disconnectViewHeadersSignals()
{
    disconnect(tableView->horizontalHeader(), SIGNAL(sectionResized(int,int,int)), this, SLOT(on_sectionResized(int,int,int)));
    disconnect(tableView->horizontalHeader(), SIGNAL(geometriesChanged()), this, SLOT(on_geometriesChanged()));
}

// Setup the resize mode, handles compatibility for Qt5 and below as the method signatures changed.
// Refactored here for readability.
void TableViewLastColumnResizingFixer::setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode)
{
#if QT_VERSION < 0x050000
    tableView->horizontalHeader()->setResizeMode(logicalIndex, resizeMode);
#else
    tableView->horizontalHeader()->setSectionResizeMode(logicalIndex, resizeMode);
#endif
}

void TableViewLastColumnResizingFixer::resizeColumn(int nColumnIndex, int width)
{
    tableView->setColumnWidth(nColumnIndex, width);
    tableView->horizontalHeader()->resizeSection(nColumnIndex, width);
}

int TableViewLastColumnResizingFixer::getColumnsWidth()
{
    int nColumnsWidthSum = 0;
    for (int i = 0; i < columnCount; i++)
    {
        nColumnsWidthSum += tableView->horizontalHeader()->sectionSize(i);
    }
    return nColumnsWidthSum;
}

int TableViewLastColumnResizingFixer::getAvailableWidthForColumn(int column)
{
    int nResult = lastColumnMinimumWidth;
    int nTableWidth = tableView->horizontalHeader()->width();

    if (nTableWidth > 0)
    {
        int nOtherColsWidth = getColumnsWidth() - tableView->horizontalHeader()->sectionSize(column);
        nResult = std::max(nResult, nTableWidth - nOtherColsWidth);
    }

    return nResult;
}

// Make sure we don't make the columns wider than the tables viewport width.
void TableViewLastColumnResizingFixer::adjustTableColumnsWidth()
{
    disconnectViewHeadersSignals();
    resizeColumn(lastColumnIndex, getAvailableWidthForColumn(lastColumnIndex));
    connectViewHeadersSignals();

    int nTableWidth = tableView->horizontalHeader()->width();
    int nColsWidth = getColumnsWidth();
    if (nColsWidth > nTableWidth)
    {
        resizeColumn(secondToLastColumnIndex,getAvailableWidthForColumn(secondToLastColumnIndex));
    }
}

// Make column use all the space available, useful during window resizing.
void TableViewLastColumnResizingFixer::stretchColumnWidth(int column)
{
    disconnectViewHeadersSignals();
    resizeColumn(column, getAvailableWidthForColumn(column));
    connectViewHeadersSignals();
}

// When a section is resized this is a slot-proxy for ajustAmountColumnWidth().
void TableViewLastColumnResizingFixer::on_sectionResized(int logicalIndex, int oldSize, int newSize)
{
    adjustTableColumnsWidth();
    int remainingWidth = getAvailableWidthForColumn(logicalIndex);
    if (newSize > remainingWidth)
    {
       resizeColumn(logicalIndex, remainingWidth);
    }
}

// When the tabless geometry is ready, we manually perform the stretch of the "Message" column,
// as the "Stretch" resize mode does not allow for interactive resizing.
void TableViewLastColumnResizingFixer::on_geometriesChanged()
{
    if ((getColumnsWidth() - this->tableView->horizontalHeader()->width()) != 0)
    {
        disconnectViewHeadersSignals();
        resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
        connectViewHeadersSignals();
    }
}

/**
 * Initializes all internal variables and prepares the
 * the resize modes of the last 2 columns of the table and
 */
TableViewLastColumnResizingFixer::TableViewLastColumnResizingFixer(QTableView* table, int lastColMinimumWidth, int allColsMinimumWidth, QObject *parent) :
    QObject(parent),
    tableView(table),
    lastColumnMinimumWidth(lastColMinimumWidth),
    allColumnsMinimumWidth(allColsMinimumWidth)
{
    columnCount = tableView->horizontalHeader()->count();
    lastColumnIndex = columnCount - 1;
    secondToLastColumnIndex = columnCount - 2;
    tableView->horizontalHeader()->setMinimumSectionSize(allColumnsMinimumWidth);
    setViewHeaderResizeMode(secondToLastColumnIndex, QHeaderView::Interactive);
    setViewHeaderResizeMode(lastColumnIndex, QHeaderView::Interactive);
}

#ifdef WIN32
boost::filesystem::path static StartupShortcutPath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Quavence-Qt.lnk";
    if (chain == CBaseChainParams::TESTNET)
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Quavence-Qt (testnet).lnk";
    return GetSpecialFolderPath(CSIDL_STARTUP) / strprintf("Quavence-Qt (%s).lnk", chain);
}

bool GetStartOnSystemStartup()
{
    // check for Bitcoin*.lnk
    return boost::filesystem::exists(StartupShortcutPath());
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    // If the shortcut exists already, remove it for updating
    boost::filesystem::remove(StartupShortcutPath());

    if (fAutoStart)
    {
        CoInitialize(NULL);

        // Get a pointer to the IShellLink interface.
        IShellLink* psl = NULL;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL,
            CLSCTX_INPROC_SERVER, IID_IShellLink,
            reinterpret_cast<void**>(&psl));

        if (SUCCEEDED(hres))
        {
            // Get the current executable path
            TCHAR pszExePath[MAX_PATH];
            GetModuleFileName(NULL, pszExePath, sizeof(pszExePath));

            // Start client minimized
            QString strArgs = "-min";
            // Set -testnet /-regtest options
            strArgs += QString::fromStdString(strprintf(" -testnet=%d -regtest=%d", GetBoolArg("-testnet", false), GetBoolArg("-regtest", false)));

#ifdef UNICODE
            boost::scoped_array<TCHAR> args(new TCHAR[strArgs.length() + 1]);
            // Convert the QString to TCHAR*
            strArgs.toWCharArray(args.get());
            // Add missing '\0'-termination to string
            args[strArgs.length()] = '\0';
#endif

            // Set the path to the shortcut target
            psl->SetPath(pszExePath);
            PathRemoveFileSpec(pszExePath);
            psl->SetWorkingDirectory(pszExePath);
            psl->SetShowCmd(SW_SHOWMINNOACTIVE);
#ifndef UNICODE
            psl->SetArguments(strArgs.toStdString().c_str());
#else
            psl->SetArguments(args.get());
#endif

            // Query IShellLink for the IPersistFile interface for
            // saving the shortcut in persistent storage.
            IPersistFile* ppf = NULL;
            hres = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
            if (SUCCEEDED(hres))
            {
                WCHAR pwsz[MAX_PATH];
                // Ensure that the string is ANSI.
                MultiByteToWideChar(CP_ACP, 0, StartupShortcutPath().string().c_str(), -1, pwsz, MAX_PATH);
                // Save the link by calling IPersistFile::Save.
                hres = ppf->Save(pwsz, TRUE);
                ppf->Release();
                psl->Release();
                CoUninitialize();
                return true;
            }
            psl->Release();
        }
        CoUninitialize();
        return false;
    }
    return true;
}
#elif defined(Q_OS_LINUX)

// Follow the Desktop Application Autostart Spec:
// http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

boost::filesystem::path static GetAutostartDir()
{
    namespace fs = boost::filesystem;

    char* pszConfigHome = getenv("XDG_CONFIG_HOME");
    if (pszConfigHome) return fs::path(pszConfigHome) / "autostart";
    char* pszHome = getenv("HOME");
    if (pszHome) return fs::path(pszHome) / ".config" / "autostart";
    return fs::path();
}

boost::filesystem::path static GetAutostartFilePath()
{
    std::string chain = ChainNameFromCommandLine();
    if (chain == CBaseChainParams::MAIN)
        return GetAutostartDir() / "quavence-qt.desktop";
    return GetAutostartDir() / strprintf("quavence-qt-%s.desktop", chain);
}

bool GetStartOnSystemStartup()
{
    boost::filesystem::ifstream optionFile(GetAutostartFilePath());
    if (!optionFile.good())
        return false;
    // Scan through file for "Hidden=true":
    std::string line;
    while (!optionFile.eof())
    {
        getline(optionFile, line);
        if (line.find("Hidden") != std::string::npos &&
            line.find("true") != std::string::npos)
            return false;
    }
    optionFile.close();

    return true;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    if (!fAutoStart)
        boost::filesystem::remove(GetAutostartFilePath());
    else
    {
        char pszExePath[MAX_PATH+1];
        memset(pszExePath, 0, sizeof(pszExePath));
        if (readlink("/proc/self/exe", pszExePath, sizeof(pszExePath)-1) == -1)
            return false;

        boost::filesystem::create_directories(GetAutostartDir());

        boost::filesystem::ofstream optionFile(GetAutostartFilePath(), std::ios_base::out|std::ios_base::trunc);
        if (!optionFile.good())
            return false;
        std::string chain = ChainNameFromCommandLine();
        // Write a bitcoin.desktop file to the autostart directory:
        optionFile << "[Desktop Entry]\n";
        optionFile << "Type=Application\n";
        if (chain == CBaseChainParams::MAIN)
            optionFile << "Name=Quavence-Qt\n";
        else
            optionFile << strprintf("Name=Quavence-Qt (%s)\n", chain);
        optionFile << "Exec=" << pszExePath << strprintf(" -min -testnet=%d -regtest=%d\n", GetBoolArg("-testnet", false), GetBoolArg("-regtest", false));
        optionFile << "Terminal=false\n";
        optionFile << "Hidden=false\n";
        optionFile.close();
    }
    return true;
}


#elif defined(Q_OS_MAC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// based on: https://github.com/Mozketo/LaunchAtLoginController/blob/master/LaunchAtLoginController.m

#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl);
LSSharedFileListItemRef findStartupItemInList(LSSharedFileListRef list, CFURLRef findUrl)
{
    // loop through the list of startup items and try to find the bitcoin app
    CFArrayRef listSnapshot = LSSharedFileListCopySnapshot(list, NULL);
    if (listSnapshot == NULL) {
        return nullptr;
    }
    
    // loop through the list of startup items and try to find the bitcoin app
    for(int i = 0; i < CFArrayGetCount(listSnapshot); i++) {
        LSSharedFileListItemRef item = (LSSharedFileListItemRef)CFArrayGetValueAtIndex(listSnapshot, i);
        UInt32 resolutionFlags = kLSSharedFileListNoUserInteraction | kLSSharedFileListDoNotMountVolumes;
        CFURLRef currentItemURL = NULL;

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED >= 10100
    if(&LSSharedFileListItemCopyResolvedURL)
        currentItemURL = LSSharedFileListItemCopyResolvedURL(item, resolutionFlags, NULL);
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && MAC_OS_X_VERSION_MIN_REQUIRED < 10100
    else
        LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, NULL);
#endif
#else
    LSSharedFileListItemResolve(item, resolutionFlags, &currentItemURL, NULL);
#endif

        if(currentItemURL) {
            if (CFEqual(currentItemURL, findUrl)) {
                // found
                CFRelease(listSnapshot);
                CFRelease(currentItemURL);
                return item;
            }
            CFRelease(currentItemURL);
        }
    }

    CFRelease(listSnapshot);
    return NULL;
}

bool GetStartOnSystemStartup()
{
    CFURLRef bitcoinAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());

    if (bitcoinAppUrl == nullptr) {
        return false;
    }
    
    LSSharedFileListRef loginItems = LSSharedFileListCreate(nullptr, kLSSharedFileListSessionLoginItems, nullptr);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, bitcoinAppUrl);

    CFRelease(bitcoinAppUrl);
    return !!foundItem; // return boolified object
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    CFURLRef bitcoinAppUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());

    if (bitcoinAppUrl == nullptr) {
        return false;
    }
    
    LSSharedFileListRef loginItems = LSSharedFileListCreate(nullptr, kLSSharedFileListSessionLoginItems, nullptr);
    LSSharedFileListItemRef foundItem = findStartupItemInList(loginItems, bitcoinAppUrl);

    if(fAutoStart && !foundItem) {
        // add bitcoin app to startup item list
        LSSharedFileListInsertItemURL(loginItems, kLSSharedFileListItemBeforeFirst, NULL, NULL, bitcoinAppUrl, NULL, NULL);
    }
    else if(!fAutoStart && foundItem) {
        // remove item
        LSSharedFileListItemRemove(loginItems, foundItem);
    }
    
    CFRelease(bitcoinAppUrl);
    return true;
}
#pragma GCC diagnostic pop
#else

bool GetStartOnSystemStartup() { return false; }
bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

void saveWindowGeometry(const QString& strSetting, QWidget *parent)
{
    QSettings settings;
    settings.setValue(strSetting + "Pos", parent->pos());
    settings.setValue(strSetting + "Size", parent->size());
}

void restoreWindowGeometry(const QString& strSetting, const QSize& defaultSize, QWidget *parent)
{
    QSettings settings;
    QPoint pos = settings.value(strSetting + "Pos").toPoint();
    QSize size = settings.value(strSetting + "Size", defaultSize).toSize();

    parent->resize(size);
    parent->move(pos);

    if ((!pos.x() && !pos.y()) || (QApplication::desktop()->screenNumber(parent) == -1))
    {
        QRect screen = QApplication::desktop()->screenGeometry();
        QPoint defaultPos((screen.width() - defaultSize.width()) / 2,
                          (screen.height() - defaultSize.height()) / 2);
        parent->resize(defaultSize);
        parent->move(defaultPos);
    }
}

void setClipboard(const QString& str)
{
    QApplication::clipboard()->setText(str, QClipboard::Clipboard);
    QApplication::clipboard()->setText(str, QClipboard::Selection);
}

#if BOOST_FILESYSTEM_VERSION >= 3
boost::filesystem::path qstringToBoostPath(const QString &path)
{
    return boost::filesystem::path(path.toStdString(), utf8);
}

QString boostPathToQString(const boost::filesystem::path &path)
{
    return QString::fromStdString(path.string(utf8));
}
#else
#warning Conversion between boost path and QString can use invalid character encoding with boost_filesystem v2 and older
boost::filesystem::path qstringToBoostPath(const QString &path)
{
    return boost::filesystem::path(path.toStdString());
}

QString boostPathToQString(const boost::filesystem::path &path)
{
    return QString::fromStdString(path.string());
}
#endif

QString formatDurationStr(int secs)
{
    QStringList strList;
    int days = secs / 86400;
    int hours = (secs % 86400) / 3600;
    int mins = (secs % 3600) / 60;
    int seconds = secs % 60;

    if (days)
        strList.append(QString(QObject::tr("%1 d")).arg(days));
    if (hours)
        strList.append(QString(QObject::tr("%1 h")).arg(hours));
    if (mins)
        strList.append(QString(QObject::tr("%1 m")).arg(mins));
    if (seconds || (!days && !hours && !mins))
        strList.append(QString(QObject::tr("%1 s")).arg(seconds));

    return strList.join(" ");
}

QString formatServicesStr(quint64 mask)
{
    QStringList strList;

    // Just scan the last 8 bits for now.
    for (int i = 0; i < 8; i++) {
        uint64_t check = 1 << i;
        if (mask & check)
        {
            switch (check)
            {
            case NODE_NETWORK:
                strList.append("NETWORK");
                break;
            case NODE_GETUTXO:
                strList.append("GETUTXO");
                break;
            case NODE_BLOOM:
                strList.append("BLOOM");
                break;
            default:
                strList.append(QString("%1[%2]").arg("UNKNOWN").arg(check));
            }
        }
    }

    if (strList.size())
        return strList.join(" & ");
    else
        return QObject::tr("None");
}

QString formatPingTime(double dPingTime)
{
    return dPingTime == 0 ? QObject::tr("N/A") : QString(QObject::tr("%1 ms")).arg(QString::number((int)(dPingTime * 1000), 10));
}

QString formatTimeOffset(int64_t nTimeOffset)
{
  return QString(QObject::tr("%1 s")).arg(QString::number((int)nTimeOffset, 10));
}

QString formateNiceTimeOffset(qint64 secs)
{
    // Represent time from last generated block in human readable text
    QString timeBehindText;
    const int HOUR_IN_SECONDS = 60*60;
    const int DAY_IN_SECONDS = 24*60*60;
    const int WEEK_IN_SECONDS = 7*24*60*60;
    const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
    if(secs < 60)
    {
        timeBehindText = QObject::tr("%n second(s)","",secs);
    }
    else if(secs < 2*HOUR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n minute(s)","",secs/60);
    }
    else if(secs < 2*DAY_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
    }
    else if(secs < 2*WEEK_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n day(s)","",secs/DAY_IN_SECONDS);
    }
    else if(secs < YEAR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n week(s)","",secs/WEEK_IN_SECONDS);
    }
    else
    {
        qint64 years = secs / YEAR_IN_SECONDS;
        qint64 remainder = secs % YEAR_IN_SECONDS;
        timeBehindText = QObject::tr("%1 and %2").arg(QObject::tr("%n year(s)", "", years)).arg(QObject::tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
    }
    return timeBehindText;
}
} // namespace GUIUtil
