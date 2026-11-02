#pragma once

#include "core/Types.h"

#include <QColor>
#include <QString>
#include <QtGlobal>

class QApplication;

namespace arc::theme {

// 加载随程序打包的 HarmonyOS Sans SC TW 字体（QApplication 构造后调用一次）。
bool installBundledFont();
// 当前 UI 字体族（加载失败时为 Microsoft YaHei UI）。
QString uiFontFamily();

bool isDark();
void setDark(bool dark);

QColor background();
QColor surface();
QColor surfaceAlt();
QColor border();
QColor text();
QColor textMuted();
QColor textDim();
QColor green();
QColor lime(); // 绿黄（剩余 50%~75% 档）
QColor yellow();
QColor red();
QColor blue(); // “对话正常结束”的活动指示灯（深色亮蓝 / 浅色深蓝）
QColor track();

// 重置卡临期小点（橙色，深浅主题通用）。
QColor warningDot();

// 重置卡到期预警取色：<2 天红、<7 天黄、>=7 天绿；到期时间未知时保持黄色。
QColor resetCreditColor(ResetCreditUrgency urgency);

// 全项目唯一的额度取色口径：入参是“已用百分比”，颜色按“剩余量”分档：
//   剩余 >=75% 绿、>=50% 绿黄、>=20% 黄、<20% 红。
QColor quotaColor(int usedPercent);

// 颜色转 QSS 可用的 rgba(...) 字符串（保留 alpha）。
QString rgba(const QColor &color);

QString progressBarStyle(const QColor &chunk);

// 应用全局调色板 + QSS（深浅色主题切换时重新调用）。
void applyApplicationTheme(QApplication &app);

} // namespace arc::theme