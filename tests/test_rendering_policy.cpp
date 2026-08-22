// SPDX-License-Identifier: MIT
// @author zhouwr

#include <QTest>

#include "../src/platform/RenderingPolicy.h"

class TestRenderingPolicy : public QObject {
    Q_OBJECT
private slots:
    void missingGlxUsesSoftware();
    void missingDri3UsesSoftware();
    void physicalX11KeepsHardware();
    void waylandKeepsHardware();
    void offscreenUsesSoftware();
    void explicitOverrideWins();
};

void TestRenderingPolicy::missingGlxUsesSoftware() {
    QVERIFY(dsh::platform::shouldUseSoftwareRendering(
        {}, "x11", true, false, true));
}

void TestRenderingPolicy::missingDri3UsesSoftware() {
    QVERIFY(dsh::platform::shouldUseSoftwareRendering(
        {}, "x11", true, true, false));
}

void TestRenderingPolicy::physicalX11KeepsHardware() {
    QVERIFY(!dsh::platform::shouldUseSoftwareRendering(
        {}, "x11", true, true, true));
}

void TestRenderingPolicy::waylandKeepsHardware() {
    QVERIFY(!dsh::platform::shouldUseSoftwareRendering(
        {}, "wayland", true, false, false));
}

void TestRenderingPolicy::offscreenUsesSoftware() {
    QVERIFY(dsh::platform::shouldUseSoftwareRendering(
        "offscreen", {}, false, true, true));
}

void TestRenderingPolicy::explicitOverrideWins() {
    QVERIFY(dsh::platform::shouldUseSoftwareRendering(
        {}, "x11", true, true, true, "1"));
    QVERIFY(!dsh::platform::shouldUseSoftwareRendering(
        {}, "x11", true, false, false, "0"));
}

QTEST_GUILESS_MAIN(TestRenderingPolicy)
#include "test_rendering_policy.moc"
