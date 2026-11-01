#pragma once

#include "core/AccountProfile.h"

#include <QPixmap>

namespace arc::avatar {

// 头像圆形裁切（有真实头像时）或按昵称生成字母头像；devicePixelRatio 用于高 DPI。
QPixmap pixmapFor(const AccountProfile &profile, int size, qreal devicePixelRatio);
QPixmap letterPixmap(const QString &name, int size, qreal devicePixelRatio);

} // namespace arc::avatar
