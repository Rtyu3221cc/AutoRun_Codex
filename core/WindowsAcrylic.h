#pragma once

#include <QColor>
#include <QWidget>

namespace arc {

// Windows 10/11 亚克力模糊（SetWindowCompositionAttribute）。
// 返回 false 表示当前平台/系统不支持，调用方保留半透明深色兜底。
bool applyWindowAcrylic(QWidget *widget, const QColor &tint, int tintAlpha = 210);

} // namespace arc
