#include "ui/AvatarUtils.h"

#include "ui/Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace arc::avatar {
namespace {

QPixmap makeCanvas(int size, qreal devicePixelRatio)
{
    const qreal dpr = qMax<qreal>(1.0, devicePixelRatio);
    QPixmap canvas(qMax(1, qRound(size * dpr)), qMax(1, qRound(size * dpr)));
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);
    return canvas;
}

QPixmap circularize(const QPixmap &source, int size, qreal devicePixelRatio)
{
    QPixmap canvas = makeCanvas(size, devicePixelRatio);
    if (source.isNull())
        return canvas;

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath path;
    path.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(path);

    // 按 cover 方式铺满圆形（短边对齐）。
    const qreal shortSide = qMin(source.width(), source.height());
    const qreal factor = size / shortSide;
    const QSizeF scaled(source.width() * factor, source.height() * factor);
    const QRectF target((size - scaled.width()) / 2.0, (size - scaled.height()) / 2.0,
                        scaled.width(), scaled.height());
    painter.drawPixmap(target, source, QRectF(source.rect()));
    return canvas;
}

} // namespace

QPixmap letterPixmap(const QString &name, int size, qreal devicePixelRatio)
{
    QPixmap canvas = makeCanvas(size, devicePixelRatio);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF circle(0.5, 0.5, size - 1.0, size - 1.0);
    QLinearGradient gradient(circle.topLeft(), circle.bottomRight());
    QColor background = theme::surfaceAlt();
    gradient.setColorAt(0.0, background.lighter(118));
    gradient.setColorAt(1.0, background);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(circle);

    QColor ring = theme::green();
    ring.setAlpha(160);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ring, qMax<qreal>(1.0, size * 0.06)));
    painter.drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));

    const QString trimmed = name.trimmed();
    const QString initial = trimmed.isEmpty() ? QStringLiteral("?")
                                              : trimmed.left(1).toUpper();
    QFont font = QGuiApplication::font();
    font.setPixelSize(qMax(8, qRound(size * 0.52)));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(theme::green());
    painter.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initial);
    return canvas;
}

QPixmap pixmapFor(const AccountProfile &profile, int size, qreal devicePixelRatio)
{
    if (!profile.avatar.isNull())
        return circularize(profile.avatar, size, devicePixelRatio);
    return letterPixmap(profile.name, size, devicePixelRatio);
}

} // namespace arc::avatar
