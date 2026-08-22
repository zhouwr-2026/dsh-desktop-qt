// SPDX-License-Identifier: MIT
// @author zhouwr

#include "RenderingPolicy.h"

#include <QCoreApplication>

#include <xcb/xcb.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dsh::platform {

namespace {

bool x11ExtensionAvailable(const char* name) {
    int screen = 0;
    xcb_connection_t* connection = xcb_connect(nullptr, &screen);
    if (!connection || xcb_connection_has_error(connection)) {
        if (connection) xcb_disconnect(connection);
        return false;
    }
    const auto cookie = xcb_query_extension(
        connection, static_cast<uint16_t>(std::strlen(name)), name);
    xcb_query_extension_reply_t* reply =
        xcb_query_extension_reply(connection, cookie, nullptr);
    const bool available = reply && reply->present;
    std::free(reply);
    xcb_disconnect(connection);
    return available;
}

void appendChromiumFlag(const QByteArray& flag) {
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").trimmed();
    if (!flags.split(' ').contains(flag)) {
        if (!flags.isEmpty()) flags.append(' ');
        flags.append(flag);
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
    }
}

}  // namespace

bool shouldUseSoftwareRendering(const QByteArray& platformName,
                                const QByteArray& sessionType,
                                bool displayAvailable,
                                bool glxAvailable,
                                bool dri3Available,
                                const QByteArray& overrideValue) {
    if (overrideValue == "1" || overrideValue.compare("true", Qt::CaseInsensitive) == 0)
        return true;
    if (overrideValue == "0" || overrideValue.compare("false", Qt::CaseInsensitive) == 0)
        return false;
    if (platformName == "offscreen" || platformName == "minimal") return true;
    return displayAvailable
        && sessionType.compare("x11", Qt::CaseInsensitive) == 0
        && (!glxAvailable || !dri3Available);
}

void configureRendering() {
    const QByteArray platformName = qgetenv("QT_QPA_PLATFORM");
    const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");
    const bool displayAvailable = !qgetenv("DISPLAY").isEmpty();
    const bool queryX11 = displayAvailable
        && platformName != "offscreen" && platformName != "minimal"
        && sessionType.compare("x11", Qt::CaseInsensitive) == 0;
    const bool glxAvailable = !queryX11 || x11ExtensionAvailable("GLX");
    const bool dri3Available = !queryX11 || x11ExtensionAvailable("DRI3");
    const bool software = shouldUseSoftwareRendering(
        platformName, sessionType, displayAvailable, glxAvailable, dri3Available,
        qgetenv("DSH_DESKTOP_SOFTWARE_RENDERING"));

    if (!software) return;
    QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    if (qgetenv("QT_OPENGL").isEmpty()) qputenv("QT_OPENGL", "software");
    if (qgetenv("QT_QUICK_BACKEND").isEmpty()) qputenv("QT_QUICK_BACKEND", "software");
    appendChromiumFlag(QByteArrayLiteral("--disable-gpu"));
    if (platformName == "offscreen" || platformName == "minimal") {
        std::fprintf(stderr,
                     "dsh-desktop: 无窗口平台已启用软件渲染。\n");
    } else {
        std::fprintf(stderr,
                     "dsh-desktop: 当前 X11 显示缺少 GLX/DRI3，已启用软件渲染。\n");
    }
}

}  // namespace dsh::platform
