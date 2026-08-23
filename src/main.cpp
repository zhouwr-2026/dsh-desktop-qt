// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop 入口：解析命令行，构造顶层 ``DshDesktopApp``，按模式（
// smoke / self-test / 正式运行）分发。

#include "app/DshDesktopApp.h"
#include "platform/RenderingPolicy.h"

#include "BuildVersion.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QResource>

#include <cstdlib>
#include <cstdio>
#include <memory>
#include <unistd.h>

// 强制把 icons.qrc 链接进来。Qt 6 的 rcc 把 ``qInitResources_icons`` 放在
// 全局命名空间（当 ``QT_NAMESPACE`` 宏未定义时）——链接器看不到这个符号
// 就会 strip 整个 qrc_icons.cpp，资源也就没了。显式 extern 一下即可。
extern int qInitResources_icons();

int main(int argc, char* argv[]) {
    // 立即注册嵌入资源（必须在构造任何 QIcon 之前）。
    qInitResources_icons();

    // 把 IME 集成所需的环境变量补齐——尤其是 XMODIFIERS / QT_IM_MODULE /
    // GTK_IM_MODULE / SDL_IM_MODULE。QtWebEngine 内部 Chromium 进程靠
    // XMODIFIERS=@im=fcitx 跟 X server 上的输入法通讯；如果 dsh-desktop 是
    // 由 systemd / dbus-run-session / 自启动拉起的，session 环境可能没带
    // 这些变量，结果就是嵌入式 webview 里的输入框无法切换中英文。Qt 自身
    // 输入控件靠 QT_IM_MODULE。两者必须都在，且在 QApplication 构造前
    // 设好（之后 Chromium 子进程会继承）。
    auto ensureEnvVar = [](const char* name, const char* defaultValue) {
        if (qEnvironmentVariableIsEmpty(name)) {
            qputenv(name, defaultValue);
        }
    };
    // 用户的桌面常用 fcitx5；fcitx5 同时响应 @im=fcitx（XIM 协议）。
    // 如果用户用 ibus，fcitx 也能正确指向实际 agent（fcitx5 兼容 ibus
    // 客户端）。设置成 fcitx 不影响 ibus 用户。
    ensureEnvVar("XMODIFIERS", "@im=fcitx");
    ensureEnvVar("QT_IM_MODULE", "fcitx");
    ensureEnvVar("GTK_IM_MODULE", "fcitx");
    ensureEnvVar("SDL_IM_MODULE", "fcitx");

    QCoreApplication::setOrganizationName(QStringLiteral("anywhere-labs"));
    QCoreApplication::setApplicationName(QStringLiteral("dsh-desktop"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(DSH_DESKTOP_VERSION));

    // xrdp/Xvnc 等 X11 服务可能不提供可用的 GLX/DRI3。必须在
    // QApplication 和 QtWebEngine 初始化前切到软件渲染，否则 Chromium
    // GPU 线程会因 GLOzone 初始化失败而 SIGABRT。实体机保留硬件加速。
    dsh::platform::configureRendering();

    // 无 GUI 的诊断模式必须能够在没有 DISPLAY/WAYLAND_DISPLAY 的机器上运行。
    // --self-test 仍需构造 QApplication，但在无显式平台配置时强制使用 offscreen。
    bool selfTestRequested = false;
    bool guiRequested = true;
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--self-test") selfTestRequested = true;
        if (arg == "--smoke" || arg == "--probe" || arg == "--help"
            || arg == "-h" || arg == "--version" || arg == "-v") {
            guiRequested = false;
        }
    }
    if (selfTestRequested) {
        guiRequested = true;
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
    }

    // QtWebEngine 以 root 运行时只能关闭 Chromium 沙箱，这会让任何 Web
    // 内容漏洞直接获得 root 权限。无 GUI 的诊断模式不受此限制。
    if (guiRequested && ::geteuid() == 0) {
        if (qgetenv("DSH_DESKTOP_ALLOW_ROOT") != QByteArrayLiteral("1")) {
            std::fprintf(stderr,
                         "dsh-desktop: 拒绝以 root 运行 QtWebEngine。请改用普通用户；"
                         "确需运行时设置 DSH_DESKTOP_ALLOW_ROOT=1。\n");
            return 5;
        }
        qputenv("QTWEBENGINE_DISABLE_SANDBOX", "1");
        QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
        if (!flags.split(' ').contains(QByteArrayLiteral("--no-sandbox"))) {
            if (!flags.trimmed().isEmpty()) flags.append(' ');
            flags.append("--no-sandbox");
            qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags);
        }
    }

    std::unique_ptr<QCoreApplication> app;
    if (guiRequested) {
        app = std::make_unique<QApplication>(argc, argv);
    } else {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("DSH Desktop — KDE Plasma 6 上 DSH Web 的原生包装器"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption urlOpt(QStringList{"u", "url"},
                              QStringLiteral("dsh web 地址（默认 http://127.0.0.1:3080）"),
                              QStringLiteral("url"));
    QCommandLineOption logOpt(QStringList{"log-file"},
                              QStringLiteral("可选的日志文件路径"),
                              QStringLiteral("path"));
    QCommandLineOption themeOpt(QStringList{"t", "theme"},
                               QStringLiteral("强制主题：dark 或 light（默认自动检测）"),
                               QStringLiteral("theme"));
    QCommandLineOption smokeOpt(QStringList{"smoke"},
                                QStringLiteral("冒烟模式：仅探测后端是否可达"));
    QCommandLineOption selfTestOpt(QStringList{"self-test"},
                                   QStringLiteral("完整启动后输出结构化报告再退出"));
    QCommandLineOption probeOpt(QStringList{"probe"},
                                QStringLiteral("环境探测：打印 DISPLAY / D-Bus / 托盘 watcher / 后端可达性"));
    parser.addOption(urlOpt);
    parser.addOption(logOpt);
    parser.addOption(themeOpt);
    parser.addOption(smokeOpt);
    parser.addOption(selfTestOpt);
    parser.addOption(probeOpt);
    parser.process(*app);

    dsh::app::AppArgs args;
    args.url = parser.value(urlOpt);
    args.logFile = parser.value(logOpt);
    args.forceTheme = parser.value(themeOpt);
    if (!args.forceTheme.isEmpty()
        && args.forceTheme != QStringLiteral("dark")
        && args.forceTheme != QStringLiteral("light")) {
        std::fprintf(stderr, "dsh-desktop: --theme 必须是 dark 或 light，收到 '%s'\n",
                     args.forceTheme.toLocal8Bit().constData());
        return 4;
    }
    args.smoke = parser.isSet(smokeOpt);
    args.selfTest = parser.isSet(selfTestOpt);

    const bool smokeRequested = args.smoke;
    const bool selfTest = args.selfTest;
    dsh::app::DshDesktopApp desktopApp(std::move(args));
    if (parser.isSet(probeOpt)) return desktopApp.probe();
    if (smokeRequested) return desktopApp.smoke();
    if (selfTest) return desktopApp.selfTest();
    return desktopApp.run();
}
