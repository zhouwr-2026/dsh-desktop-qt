// SPDX-License-Identifier: MIT
// @author zhouwr
#include "IconLoader.h"

#include <QIcon>
#include <QImageReader>
#include <QPixmap>
#include <QSize>
#include <QSvgRenderer>
#include <QPainter>

namespace dsh::icon {

namespace {

// 根据系统主题决定鲸鱼颜色：暗色主题配白色图标，亮色主题配黑色图标。
inline QString colorFor(const QString& scheme) {
    return (scheme == "dark") ? QStringLiteral("white") : QStringLiteral("black");
}

// 从嵌入 SVG 渲染指定尺寸的 QPixmap。
QPixmap renderSvg(const QString& color, int size) {
    const QString path = QStringLiteral(":/dsh/dsh-whale-%1.svg").arg(color);
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) return {};
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);
    painter.end();
    return pix;
}

}  // namespace

QIcon iconForScheme(const QString& scheme) {
    const QString color = colorFor(scheme);
    QIcon icon;
    // QSvgIconEngine 由 Qt 内置支持：直接把 SVG 资源作为多尺寸图标源，
    // 任一尺寸都能原生渲染（无需预生成 PNG）。
    icon.addFile(QStringLiteral(":/dsh/dsh-whale-%1.svg").arg(color));
    // 额外预渲染几个尺寸确保 KDE 面板/任务栏拿到锐利的位图。
    for (int size : {22, 32, 48, 64, 128, 256}) {
        QPixmap pix = renderSvg(color, size);
        if (!pix.isNull()) icon.addPixmap(pix);
    }
    return icon;
}

QPixmap pixmapForScheme(const QString& scheme, int size) {
    return renderSvg(colorFor(scheme), size);
}

QPixmap trayPixmap(const QString& scheme) {
    return pixmapForScheme(scheme, 64);
}

}  // namespace dsh::icon