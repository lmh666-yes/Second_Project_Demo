#include "theme.h"

#include "settings.h"

#include <QApplication>
#include <QString>

namespace {

struct Palette {
    QString bg, surface, surface2, border, text, textDim;
    QString accent, accentText, accentDim;
    QString ok, warn, err;
    QString sel, hover;
};

Palette darkPalette()
{
    Palette p;
    p.bg        = QStringLiteral("#0f1115");
    p.surface   = QStringLiteral("#161a21");
    p.surface2  = QStringLiteral("#1d222b");
    p.border    = QStringLiteral("#262c37");
    p.text      = QStringLiteral("#e6e9ef");
    p.textDim   = QStringLiteral("#8b93a3");
    p.accent    = QStringLiteral("#22d3ee");
    p.accentText= QStringLiteral("#0b1216");
    p.accentDim = QStringLiteral("#0e7490");
    p.ok        = QStringLiteral("#34d399");
    p.warn      = QStringLiteral("#fbbf24");
    p.err       = QStringLiteral("#f87171");
    p.sel       = QStringLiteral("#1f3a44");
    p.hover     = QStringLiteral("#20262f");
    return p;
}

Palette lightPalette()
{
    Palette p;
    p.bg        = QStringLiteral("#f5f7fa");
    p.surface   = QStringLiteral("#ffffff");
    p.surface2  = QStringLiteral("#eef1f6");
    p.border    = QStringLiteral("#d5dbe5");
    p.text      = QStringLiteral("#1a1f2b");
    p.textDim   = QStringLiteral("#5b667a");
    p.accent    = QStringLiteral("#0891b2");
    p.accentText= QStringLiteral("#ffffff");
    p.accentDim = QStringLiteral("#0e7490");
    p.ok        = QStringLiteral("#059669");
    p.warn      = QStringLiteral("#d97706");
    p.err       = QStringLiteral("#dc2626");
    p.sel       = QStringLiteral("#d9edf3");
    p.hover     = QStringLiteral("#e7ecf3");
    return p;
}

QString buildQss(const Palette &p)
{
    QString q = QStringLiteral(R"QSS(
* { outline: none; }
QWidget { background: @BG@; color: @TEXT@;
    font-family: "Microsoft YaHei UI","Segoe UI",sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: @BG@; }

QLabel { background: transparent; }
QLabel#pageTitle { font-size: 18px; font-weight: 600; }
QLabel#hint { color: @DIM@; }

QGroupBox { background: @SURF@; border: 1px solid @BORDER@; border-radius: 8px;
    margin-top: 14px; padding: 10px 10px 8px 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px;
    color: @DIM@; font-weight: 600; }

QPushButton { background: @SURF2@; color: @TEXT@; border: 1px solid @BORDER@;
    border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { background: @HOVER@; border-color: @ACCENT@; }
QPushButton:pressed { background: @SEL@; }
QPushButton:disabled { color: @DIM@; border-color: @BORDER@; background: @SURF@; }
QPushButton#primary { background: @ACCENT@; color: @ACCTEXT@; border: none; font-weight: 600; }
QPushButton#primary:hover { background: @ACCDIM@; color: @ACCTEXT@; }

QLineEdit, QPlainTextEdit, QTextBrowser, QTextEdit, QSpinBox, QComboBox {
    background: @SURF@; color: @TEXT@; border: 1px solid @BORDER@;
    border-radius: 6px; padding: 4px 6px; selection-background-color: @ACCENT@;
    selection-color: @ACCTEXT@; }
QLineEdit:focus, QPlainTextEdit:focus, QTextBrowser:focus, QSpinBox:focus, QComboBox:focus {
    border-color: @ACCENT@; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: @SURF2@; border: 1px solid @BORDER@;
    selection-background-color: @SEL@; color: @TEXT@; }
QSpinBox::up-button, QSpinBox::down-button { background: @SURF2@; border: none; width: 16px; }

QCheckBox { background: transparent; spacing: 8px; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid @BORDER@;
    border-radius: 4px; background: @SURF@; }
QCheckBox::indicator:checked { background: @ACCENT@; border-color: @ACCENT@; }

QListWidget { background: @SURF@; border: 1px solid @BORDER@; border-radius: 8px; padding: 4px; }
QListWidget::item { padding: 7px 10px; border-radius: 6px; color: @TEXT@; }
QListWidget::item:hover { background: @HOVER@; }
QListWidget::item:selected { background: @SEL@; color: @TEXT@;
    border-left: 3px solid @ACCENT@; }

QTableWidget { background: @SURF@; border: 1px solid @BORDER@; border-radius: 8px;
    gridline-color: @BORDER@; }
QTableWidget::item { padding: 3px 6px; }
QTableWidget::item:selected { background: @SEL@; color: @TEXT@; }
QHeaderView::section { background: @SURF2@; color: @DIM@; border: none;
    border-right: 1px solid @BORDER@; border-bottom: 1px solid @BORDER@; padding: 5px 8px;
    font-weight: 600; }

QTabWidget::pane { border: 1px solid @BORDER@; border-radius: 8px; top: -1px; }
QTabBar::tab { background: transparent; color: @DIM@; padding: 7px 16px;
    border-top-left-radius: 6px; border-top-right-radius: 6px; }
QTabBar::tab:hover { color: @TEXT@; }
QTabBar::tab:selected { color: @ACCENT@; border-bottom: 2px solid @ACCENT@; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @BORDER@; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @ACCENT@; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @BORDER@; border-radius: 5px; min-width: 30px; }

QSplitter::handle { background: @BORDER@; }
QToolTip { background: @SURF2@; color: @TEXT@; border: 1px solid @BORDER@; padding: 4px 6px; }
)QSS");

    q.replace(QStringLiteral("@BG@"), p.bg);
    q.replace(QStringLiteral("@SURF2@"), p.surface2);   /* 先替换长的，避免被 @SURF@ 命中 */
    q.replace(QStringLiteral("@SURF@"), p.surface);
    q.replace(QStringLiteral("@BORDER@"), p.border);
    q.replace(QStringLiteral("@TEXT@"), p.text);
    q.replace(QStringLiteral("@DIM@"), p.textDim);
    q.replace(QStringLiteral("@ACCENT@"), p.accent);
    q.replace(QStringLiteral("@ACCTEXT@"), p.accentText);
    q.replace(QStringLiteral("@ACCDIM@"), p.accentDim);
    q.replace(QStringLiteral("@SEL@"), p.sel);
    q.replace(QStringLiteral("@HOVER@"), p.hover);
    return q;
}

} // namespace

namespace Theme {

Mode mode()
{
    Settings s;
    const int v = s.value(QStringLiteral("ui/theme"), int(Dark)).toInt();
    return v == int(Light) ? Light : Dark;
}

void apply()
{
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        app->setStyleSheet(buildQss(mode() == Dark ? darkPalette() : lightPalette()));
}

void setMode(Mode m)
{
    Settings().setValue(QStringLiteral("ui/theme"), int(m));
    apply();
}

Mode toggle()
{
    const Mode next = (mode() == Dark) ? Light : Dark;
    setMode(next);
    return next;
}

QString modeName()
{
    return mode() == Dark ? QStringLiteral("深色") : QStringLiteral("浅色");
}

} // namespace Theme
