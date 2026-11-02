#include "Theme.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace arc::theme {
namespace {
bool g_dark = true;
QString g_fontFamily = QStringLiteral("Microsoft YaHei UI");
int g_bundledFontId = -1;
} // namespace

bool installBundledFont()
{
    if (g_bundledFontId >= 0)
        return true;
    const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/resources/HarmonyOS_SansSC_TW.ttf"));
    if (id < 0) {
        qWarning().noquote() << "[theme] 内置字体加载失败，回退 Microsoft YaHei UI";
        return false;
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qWarning().noquote() << "[theme] 内置字体没有可用字体族，回退 Microsoft YaHei UI";
        return false;
    }
    g_bundledFontId = id;
    g_fontFamily = families.first();
    qInfo().noquote() << "[theme] 已加载内置字体:" << g_fontFamily;
    return true;
}

QString uiFontFamily()
{
    return g_fontFamily;
}

bool isDark() { return g_dark; }
void setDark(bool dark) { g_dark = dark; }

QColor background() { return g_dark ? QColor(8, 9, 11) : QColor(244, 246, 248); }
QColor surface() { return g_dark ? QColor(15, 17, 20) : QColor(255, 255, 255); }
QColor surfaceAlt() { return g_dark ? QColor(20, 22, 26) : QColor(240, 242, 245); }
QColor border() { return g_dark ? QColor(35, 38, 44) : QColor(214, 219, 226); }
QColor text() { return g_dark ? QColor(242, 243, 245) : QColor(24, 26, 30); }
QColor textMuted() { return g_dark ? QColor(154, 160, 168) : QColor(96, 102, 112); }
QColor textDim() { return g_dark ? QColor(111, 116, 125) : QColor(130, 136, 146); }
QColor green() { return g_dark ? QColor(52, 211, 153) : QColor(14, 159, 110); }
QColor lime() { return g_dark ? QColor(170, 214, 74) : QColor(118, 160, 32); }
QColor yellow()
{
    // 深色保持金黄；浅色改为偏橙的琥珀色（在原 #D58908 基础上提亮约 10%）。
    return g_dark ? QColor(245, 185, 66) : QColor(234, 151, 9);
}
QColor red() { return QColor(242, 85, 90); }
QColor blue() { return g_dark ? QColor(96, 165, 250) : QColor(37, 99, 235); }
QColor track() { return g_dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22); }

QColor warningDot()
{
    // 橙色警示点：深色主题偏琥珀橙，浅色主题更接近橙色。
    return g_dark ? QColor(245, 158, 11) : QColor(234, 88, 12);
}

QColor resetCreditColor(ResetCreditUrgency urgency)
{
    switch (urgency) {
    case ResetCreditUrgency::Critical: return red();
    case ResetCreditUrgency::Near: return yellow();
    case ResetCreditUrgency::Far: return green();
    case ResetCreditUrgency::Unknown: break;
    }
    // 未知到期时间：沿用原来的黄色提示，不误报红/绿。
    return yellow();
}

QColor quotaColor(int usedPercent)
{
    // 颜色按“剩余量”分档，这样数值接近的两个窗口不会出现一绿一黄。
    const int remaining = 100 - qBound(0, usedPercent, 100);
    if (remaining >= 75)
        return green();
    if (remaining >= 50)
        return lime();
    if (remaining >= 20)
        return yellow();
    return red();
}

QString rgba(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(color.alpha());
}

QString progressBarStyle(const QColor &chunk)
{
    return QStringLiteral("QProgressBar{background:%1;border:none;border-radius:4px;}"
                          "QProgressBar::chunk{background:%2;border-radius:4px;}")
            .arg(rgba(track()), chunk.name());
}

void applyApplicationTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, background());
    palette.setColor(QPalette::WindowText, text());
    palette.setColor(QPalette::Base, surface());
    palette.setColor(QPalette::AlternateBase, surfaceAlt());
    palette.setColor(QPalette::ToolTipBase, surface());
    palette.setColor(QPalette::ToolTipText, text());
    palette.setColor(QPalette::Text, text());
    palette.setColor(QPalette::Button, surface());
    palette.setColor(QPalette::ButtonText, text());
    palette.setColor(QPalette::BrightText, red());
    palette.setColor(QPalette::Highlight, green());
    palette.setColor(QPalette::HighlightedText, g_dark ? QColor(10, 12, 15) : QColor(255, 255, 255));
    palette.setColor(QPalette::Link, green());
    palette.setColor(QPalette::Disabled, QPalette::Text, textDim());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDim());
    app.setPalette(palette);

    QFont font = app.font();
    font.setFamilies({uiFontFamily(), QStringLiteral("Microsoft YaHei UI")});
    font.setPointSize(9);
    app.setFont(font);

    const QString bg = background().name();
    const QString surfaceColor = surface().name();
    const QString surfaceAltColor = surfaceAlt().name();
    const QString borderColor = border().name();
    const QString textColor = text().name();
    const QString mutedColor = textMuted().name();
    const QString dimColor = textDim().name();
    const QString selection = g_dark ? QStringLiteral("#143026") : QStringLiteral("#D1FAE5");
    const QString selectionText = g_dark ? textColor : QStringLiteral("#0B3B2E");

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QWidget { color: %2; }
QToolTip { background: %3; color: %2; border: 1px solid %4; }
QTableView {
    background: %5; alternate-background-color: %6;
    gridline-color: %4; border: 1px solid %4; border-radius: 6px;
    selection-background-color: %7; selection-color: %8;
}
QHeaderView::section {
    background: %3; color: %9; border: none;
    border-bottom: 1px solid %4; padding: 4px 6px;
}
QPushButton {
    background: %6; border: 1px solid %4; border-radius: 6px;
    padding: 5px 12px;
}
QPushButton:hover { background: %10; border-color: %9; }
QPushButton:pressed { background: %5; }
QPushButton:disabled { color: %11; background: %5; border-color: %4; }
QLineEdit, QComboBox, QSpinBox {
    background: %5; border: 1px solid %4; border-radius: 6px; padding: 4px 6px;
}
QProgressBar { background: %6; border: none; border-radius: 5px; height: 10px; text-align: center; }
QProgressBar::chunk { border-radius: 5px; }
QPlainTextEdit { background: %6; border: 1px solid %4; border-radius: 6px; }
QMenu { background: %3; border: 1px solid %4; }
QMenu::item:selected { background: %10; }
QSplitter::handle { background: %4; }
QScrollBar:vertical { background: %1; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %4; border-radius: 5px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
)")
            .arg(bg, textColor, surfaceColor, borderColor, surfaceColor, surfaceAltColor, selection,
                 selectionText, mutedColor,
                 g_dark ? QStringLiteral("#1F2229") : QStringLiteral("#E9EDF2"),
                 dimColor));
}

} // namespace arc::theme