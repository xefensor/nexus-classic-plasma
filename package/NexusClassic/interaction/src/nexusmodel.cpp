// SPDX-License-Identifier: Apache-2.0
// Original pulse constants, allocation, directions and delays: AOSP 2009.
#include "nexusmodel.h"
#include <QDateTime>
#include <QtMath>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace {
void putFloat(QImage &image, int x, float value)
{
    quint32 bits = std::bit_cast<quint32>(value);
    auto *p = image.scanLine(0) + x * 4;
    for (int i = 0; i < 4; ++i) p[i] = uchar(bits >> (8 * i));
}

void putU16(uchar *bytes, qreal value)
{
    quint16 n = quint16(qBound(0, qRound(value), 65535));
    bytes[0] = uchar(n);
    bytes[1] = uchar(n >> 8);
}
}

NexusModel::NexusModel(QSizeF size, qint64 nowMs, quint32 seed)
    : m_size(size), m_random(seed)
{
    for (int i = 0; i < NormalCount; ++i) initialize(m_pulses[i], nowMs);
}

void NexusModel::initialize(NexusPulse &p, qint64 nowMs)
{
    auto random = [this] { return m_random.generateDouble(); };
    const qreal half = Cell / 2.0;
    if (random() > 0.5) {
        qreal x = qFloor(random() * m_size.width() / Cell) * Cell + half;
        qreal speed = 200.0 * (0.7 + 0.6 * random());
        if (random() > 0.5) {
            p.origin = {x, m_size.height() - half};
            p.velocity = {0, -speed};
        } else {
            p.origin = {x, -half};
            p.velocity = {0, speed};
        }
    } else {
        qreal y = m_size.height() - (qFloor(random() * m_size.height() / Cell) * Cell + half);
        qreal speed = 200.0 * (0.7 + 0.6 * random());
        if (random() > 0.5) {
            p.origin = {half, y};
            p.velocity = {speed, 0};
        } else {
            p.origin = {m_size.width() + half, y};
            p.velocity = {-speed, 0};
        }
    }
    p.startMs = nowMs + m_random.bounded(2000);
    p.color = int(m_random.bounded(4));
    p.active = true;
}

bool NexusModel::offscreen(const NexusPulse &p, qint64 nowMs) const
{
    if (!p.active || nowMs < p.startMs) return false;
    QPointF head = p.origin + p.velocity * ((nowMs - p.startMs) / 1000.0);
    const qreal tail = Trail - Cell / 2.0;
    if (p.velocity.x() < 0) return head.x() + tail <= 0;
    if (p.velocity.x() > 0) return head.x() - tail >= m_size.width();
    if (p.velocity.y() < 0) return head.y() + tail <= 0;
    return head.y() - tail >= m_size.height();
}

bool NexusModel::advance(qint64 nowMs)
{
    bool changed = false;
    for (int i = 0; i < int(m_pulses.size()); ++i) {
        auto &p = m_pulses[i];
        if (!offscreen(p, nowMs)) continue;
        if (i < NormalCount) initialize(p, nowMs);
        else p.active = false;
        changed = true;
    }
    return changed;
}

int NexusModel::tap(QPointF position, qint64 nowMs)
{
    advance(nowMs);
    const QPointF origin(qFloor(position.x() / Cell) * Cell + Cell / 2.0,
        m_size.height() - (qFloor((m_size.height() - position.y()) / Cell) * Cell + Cell / 2.0));
    const int firstColor = int(m_random.bounded(4));
    const std::array<QPointF, 4> directions = {{{300, 0}, {-300, 0}, {0, -300}, {0, 300}}};
    int added = 0;
    for (int i = NormalCount; i < int(m_pulses.size()) && added < 4; ++i) {
        auto &p = m_pulses[i];
        if (p.active) continue;
        p = {origin, directions[added], nowMs, (firstColor + added) % 4, true};
        ++added;
    }
    return added;
}

int NexusModel::extraCount() const
{
    return int(std::count_if(m_pulses.begin() + NormalCount, m_pulses.end(),
        [](const NexusPulse &p) { return p.active; }));
}

QImage NexusModel::image() const
{
    QImage image(ImageWidth, 1, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    putFloat(image, 0, ImageMagic);
    putFloat(image, 1, float(m_size.width()));
    putFloat(image, 2, float(m_size.height()));
    int count = 0;
    for (const auto &p : m_pulses) {
        if (!p.active) continue;
        const int x = 4 + 3 * count++;
        uchar *position = image.scanLine(0) + x * 4;
        putU16(position, (p.origin.x() + Cell) / (m_size.width() + 2 * Cell) * 65535.0);
        putU16(position + 2, (p.origin.y() + Cell) / (m_size.height() + 2 * Cell) * 65535.0);
        float seconds = float(QDateTime::fromMSecsSinceEpoch(p.startMs).time().msecsSinceStartOfDay() / 1000.0);
        putFloat(image, x + 1, seconds);
        uchar *details = image.scanLine(0) + (x + 2) * 4;
        putU16(details, std::hypot(p.velocity.x(), p.velocity.y()) * 128.0);
        int direction = p.velocity.x() > 0 ? 0 : p.velocity.x() < 0 ? 1 : p.velocity.y() < 0 ? 2 : 3;
        details[2] = uchar(p.color + 4 * direction);
        details[3] = 255;
    }
    putFloat(image, 3, float(count));
    return image;
}
