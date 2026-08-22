// SPDX-License-Identifier: MIT
// @author zhouwr

#include <QImage>
#include <QTest>

#include "../src/app/AboutDialog.h"
#include "../src/icon/IconLoader.h"

class TestLogo : public QObject {
    Q_OBJECT
private slots:
    void schemeColorsAreCorrect();
    void aboutUsesUnifiedLogo();
};

namespace {

int averageOpaqueChannel(const QImage& image) {
    qint64 sum = 0;
    qint64 count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() < 128) continue;
            sum += color.red() + color.green() + color.blue();
            count += 3;
        }
    }
    return count > 0 ? static_cast<int>(sum / count) : -1;
}

}  // namespace

void TestLogo::schemeColorsAreCorrect() {
    const QImage light = dsh::icon::pixmapForScheme(QStringLiteral("light"), 96).toImage();
    const QImage dark = dsh::icon::pixmapForScheme(QStringLiteral("dark"), 96).toImage();
    QVERIFY(!light.isNull());
    QVERIFY(!dark.isNull());
    QVERIFY(averageOpaqueChannel(light) < 16);
    QVERIFY(averageOpaqueChannel(dark) > 239);
}

void TestLogo::aboutUsesUnifiedLogo() {
    const QIcon logo = dsh::icon::iconForScheme(QStringLiteral("dark"));
    dsh::app::AboutDialog dialog;
    dialog.applyLogo(logo, QStringLiteral("dark"));
    QCOMPARE(dialog.appliedLogoTheme(), QStringLiteral("dark"));
    QVERIFY(!dialog.windowIcon().isNull());
}

QTEST_MAIN(TestLogo)
#include "test_logo.moc"
