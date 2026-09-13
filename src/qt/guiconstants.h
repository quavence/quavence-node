// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_GUICONSTANTS_H
#define BITCOIN_QT_GUICONSTANTS_H

/* Milliseconds between model updates */
static const int MODEL_UPDATE_DELAY = 250;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* BitcoinGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Invalid field background style */
#define STYLE_INVALID "background:#FF8080"

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor(128, 128, 128)
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE QColor(255, 0, 0)
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor(140, 140, 140)

/* Quavence brand — aligned with TG miniapp (styles.css) */
#define COLOR_BRAND_PRIMARY QColor(37, 99, 235)   /* #2563eb Logo5 / headings */
#define COLOR_BRAND_ACCENT QColor(47, 125, 246)   /* #2f7df6 miniapp --accent */
#define COLOR_BRAND_TEXT QColor(17, 20, 24)       /* #111418 miniapp --text */
#define COLOR_BRAND_MUTED QColor(95, 102, 120)    /* #5f6678 miniapp --text-muted */
#define COLOR_BRAND_COPYRIGHT_LEGACY QColor(139, 149, 166) /* #8b95a6 miniapp --text-dim */
#define COLOR_BRAND_BORDER QColor(223, 227, 234)    /* #dfe3ea miniapp --card-border */
#define COLOR_BRAND_DIVIDER QColor(232, 235, 240)   /* #e8ebf0 miniapp --divider */
#define COLOR_TX_INCOMING QColor(22, 163, 74)       /* #16a34a receive */

/* Compact tx list markers (overview history dots) */
static const int TX_TYPE_DOT_SIZE = 10;

/* Transaction list -- TX status decoration - open until date */
#define COLOR_TX_STATUS_OPENUNTILDATE COLOR_BRAND_PRIMARY
/* Transaction list -- TX status decoration - offline */
#define COLOR_TX_STATUS_OFFLINE QColor(192, 192, 192)
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER QColor(200, 100, 100)
/* Transaction list -- TX status decoration - default color */
#define COLOR_BLACK QColor(0, 0, 0)

/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Maximum allowed URI length */
static const int MAX_URI_LENGTH = 255;

/* QRCodeDialog -- size of exported QR Code image */
#define QR_IMAGE_SIZE 350

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "Quavence"
#define QAPP_ORG_DOMAIN "quavence.org"
#define QAPP_APP_NAME_DEFAULT "Quavence-Qt"
#define QAPP_APP_NAME_TESTNET "Quavence-Qt-testnet"

#endif // BITCOIN_QT_GUICONSTANTS_H
