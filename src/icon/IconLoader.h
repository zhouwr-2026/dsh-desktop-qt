// SPDX-License-Identifier: MIT
// @author zhouwr
// 解析 DSH 官方鲸鱼图标（黑 / 白两版），覆盖任意标准托盘、窗口、任务栏
// 尺寸。所有图标以 Qt 资源（``:/dsh/...``）形式嵌入二进制，运行时不再
// 做磁盘查找；资源 BASE 路径在根 ``CMakeLists.txt`` 中定义。

#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace dsh::icon {

/// \param scheme "light" 或 "dark"，分别对应黑色 / 白色鲸鱼。
/// \return 一个包含多种分辨率的 ``QIcon``。
QIcon iconForScheme(const QString& scheme);

/// \param scheme "light" 或 "dark"。
/// \param size 像素边长（22 / 32 / 48 / 64 / 128 / 256）。
QPixmap pixmapForScheme(const QString& scheme, int size);

/// KDE Plasma 6 的 StatusNotifierItem 通常用 ~22-32 像素；我们提供 64 像
/// 素的高清母版，由 Plasma 自行缩放即可。
QPixmap trayPixmap(const QString& scheme);

}  // namespace dsh::icon