// SPDX-License-Identifier: MIT
// @author zhouwr

#pragma once

#include <QByteArray>

namespace dsh::platform {

bool shouldUseSoftwareRendering(const QByteArray& platformName,
                                const QByteArray& sessionType,
                                bool displayAvailable,
                                bool glxAvailable,
                                bool dri3Available,
                                const QByteArray& overrideValue = {});

/// 必须在 QApplication 构造前调用。
void configureRendering();

}  // namespace dsh::platform
