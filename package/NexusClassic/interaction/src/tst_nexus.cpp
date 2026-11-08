// SPDX-License-Identifier: Apache-2.0
#include "nexusinput.h"
#include "nexusmodel.h"
#include <QBuffer>
#include <QDateTime>
#include <QMouseEvent>
#include <QSet>
#include <QtTest>
#include <bit>
#include <cmath>

namespace {
qint64 startTime()
{
    return QDateTime(QDate(2026, 11, 8), QTime(12, 0)).toMSecsSinceEpoch();
}
float readFloat(const QImage &image, int x)
{
    const uchar *p = image.constScanLine(0) + 4 * x;
    quint32 bits = 0;
    for (int i = 0; i < 4; ++i) bits |= quint32(p[i]) << (8 * i);
    return std::bit_cast<float>(bits);
}
QPointF readOrigin(const QImage &image, int record)
{
    const uchar *p = image.constScanLine(0) + 4 * (4 + 3 * record);
    float x = float(p[0] + 256 * p[1]) / 65535;
    float y = float(p[2] + 256 * p[3]) / 65535;
    return {x * (readFloat(image, 1) + 28) - 14, y * (readFloat(image, 2) + 28) - 14};
}
void mouse(QQuickWindow &window, QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, p, p, p, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &event);
}
void tap(QQuickWindow &window, QPointF p, Qt::MouseButton button = Qt::LeftButton)
{
    mouse(window, QEvent::MouseButtonPress, p, button, button);
    mouse(window, QEvent::MouseButtonRelease, p, button, Qt::NoButton);
}
}

class NexusTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void originalAmbientParameters()
    {
        NexusModel model({4196, 1280}, startTime(), 42);
        int count = 0;
        for (const auto &p : model.pulses()) {
            if (!p.active) continue;
            ++count;
            QVERIFY(p.startMs >= startTime() && p.startMs < startTime() + 2000);
            qreal speed = std::hypot(p.velocity.x(), p.velocity.y());
            QVERIFY(speed >= 140 && speed <= 260);
            QVERIFY(p.velocity.x() == 0 || p.velocity.y() == 0);
            QVERIFY(p.color >= 0 && p.color < 4);
        }
        QCOMPARE(count, 20);
        QCOMPARE(model.extraCount(), 0);
    }
    void tapCreatesOriginalFourDirections()
    {
        NexusModel model({4196, 1280}, startTime(), 42);
        QCOMPARE(model.tap({100, 600}, startTime()), 4);
        const std::array<QPointF, 4> expected = {{{300, 0}, {-300, 0}, {0, -300}, {0, 300}}};
        QSet<int> colors;
        for (int i = 0; i < 4; ++i) {
            const auto &p = model.pulses()[20 + i];
            QVERIFY(p.active);
            QCOMPARE(p.origin, QPointF(105, 601));
            QCOMPARE(p.velocity, expected[i]);
            QCOMPARE(p.startMs, startTime());
            colors.insert(p.color);
        }
        QCOMPARE(colors, QSet<int>({0, 1, 2, 3}));
    }
    void poolPreservesExistingBurstsAndReusesExpiredDirections()
    {
        NexusModel model({4196, 1280}, startTime(), 42);
        for (int i = 0; i < 10; ++i) QCOMPARE(model.tap({100, 600}, startTime()), 4);
        auto oldest = model.pulses()[20];
        QCOMPARE(model.extraCount(), 40);
        QCOMPARE(model.tap({500, 500}, startTime() + 20), 0);
        QCOMPARE(model.pulses()[20].startMs, oldest.startMs);
        QCOMPARE(model.pulses()[20].origin, oldest.origin);
        QVERIFY(model.advance(startTime() + 3000));
        // Only left-going pulses have fully exited (100 + 560) / 300 sec.
        QCOMPARE(model.extraCount(), 30);
        QCOMPARE(model.tap({500, 500}, startTime() + 3000), 4);
        QCOMPARE(model.extraCount(), 34);
        model.advance(startTime() + 30000);
        QCOMPARE(model.extraCount(), 0);
    }
    void pixelDataSurvivesPngAndMidnight()
    {
        qint64 now = QDateTime(QDate(2026, 11, 8), QTime(23, 59, 59, 500)).toMSecsSinceEpoch();
        NexusModel model({4196, 1280}, now, 42);
        model.tap({1919, 600}, now);
        QImage image = model.image();
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        QImage loaded = QImage::fromData(png).convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(loaded.size(), image.size());
        QCOMPARE(QByteArrayView(reinterpret_cast<const char *>(loaded.constBits()), loaded.sizeInBytes()),
            QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
        QCOMPARE(readFloat(loaded, 0), NexusModel::ImageMagic);
        QCOMPARE(readFloat(loaded, 3), 24.0f);
        QVERIFY(QLineF(readOrigin(loaded, 20), model.pulses()[20].origin).length() < 0.05);
        float seconds = readFloat(loaded, 4 + 3 * 20 + 1);
        QVERIFY(seconds == 86399.5f);
        float age = 0.5f - seconds;
        if (age < -43200.0f) age += 86400.0f;
        if (age > 43200.0f) age -= 86400.0f;
        QVERIFY(age == 1.0f);
    }
    void clicksShareBothMonitorsAndDoNotRepeatOnHoldOrDrag()
    {
        QQuickWindow left, right;
        left.setGeometry(0, 0, 400, 300);
        right.setGeometry(400, 0, 400, 300);
        left.show(); right.show();
        QQuickItem leftItem(left.contentItem()), rightItem(right.contentItem());
        leftItem.setSize({400, 300}); rightItem.setSize({400, 300});
        NexusState a, b;
        a.setShared(true); b.setShared(true);
        a.setDesktopPixels({800, 300});
        b.setPixelRatio(1.125);
        b.setDesktopPixels({900, 337.5});
        b.setScreenOffset({450, 0});
        a.setTarget(&leftItem); b.setTarget(&rightItem);
        QTRY_VERIFY(!a.textureUrl().isEmpty() && !b.textureUrl().isEmpty());
        QCOMPARE(a.textureUrl(), b.textureUrl());
        mouse(left, QEvent::MouseButtonPress, {200, 150}, Qt::LeftButton, Qt::LeftButton);
        QCOMPARE(a.extraCount(), 0);
        QTest::qWait(60);
        QCOMPARE(a.extraCount(), 0);
        mouse(left, QEvent::MouseButtonRelease, {200, 150}, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(a.extraCount(), 4);
        QCOMPARE(b.extraCount(), 4);
        QCOMPARE(a.textureUrl(), b.textureUrl());
        QImage image(a.textureUrl().toLocalFile());
        image = image.convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(QLineF(readOrigin(image, 20), QPointF(203, 153)).length() < 0.02);
        tap(right, {100, 100});
        QCOMPARE(a.extraCount(), 8);
        QCOMPARE(b.extraCount(), 8);
        image.load(a.textureUrl().toLocalFile());
        image = image.convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(QLineF(readOrigin(image, 24), QPointF(497, 195)).length() < 0.02);
        tap(left, {100, 100}, Qt::RightButton);
        QCOMPARE(a.extraCount(), 8);
        mouse(left, QEvent::MouseButtonPress, {100, 100}, Qt::LeftButton, Qt::LeftButton);
        mouse(left, QEvent::MouseMove, {200, 200}, Qt::NoButton, Qt::LeftButton);
        mouse(left, QEvent::MouseButtonRelease, {200, 200}, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(a.extraCount(), 8);
        a.setMouseEnabled(false);
        tap(left, {100, 100});
        QCOMPARE(a.extraCount(), 8);
        a.setMouseEnabled(true);
        mouse(left, QEvent::MouseButtonDblClick, {100, 100}, Qt::LeftButton, Qt::LeftButton);
        mouse(left, QEvent::MouseButtonRelease, {100, 100}, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(a.extraCount(), 12);
        mouse(left, QEvent::MouseButtonPress, {100, 100}, Qt::LeftButton, Qt::LeftButton);
        mouse(left, QEvent::MouseButtonRelease, {200, 200}, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(a.extraCount(), 12);
    }
};

QTEST_MAIN(NexusTest)
#include "tst_nexus.moc"
