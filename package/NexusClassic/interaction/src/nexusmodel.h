// SPDX-License-Identifier: Apache-2.0
// Pulse behaviour adapted from AOSP's Android 2.2 res/raw/nexus.rs.
#pragma once

#include <QImage>
#include <QPointF>
#include <QRandomGenerator>
#include <QSizeF>
#include <array>

struct NexusPulse {
    QPointF origin;
    QPointF velocity;
    qint64 startMs = 0;
    int color = 0;
    bool active = false;
};

class NexusModel {
public:
    static constexpr int NormalCount = 20;
    static constexpr int ExtraCount = 40;
    static constexpr int Cell = 14;
    static constexpr int Trail = 560;
    static constexpr int Glow = 64;
    static constexpr int ImageWidth = 4 + 3 * (NormalCount + ExtraCount);
    static constexpr float ImageMagic = 21022.0f;

    NexusModel(QSizeF size, qint64 nowMs, quint32 seed);
    void resize(QSizeF size) { m_size = size; }
    bool advance(qint64 nowMs);
    int tap(QPointF position, qint64 nowMs);
    int extraCount() const;
    QImage image() const;
    QSizeF size() const { return m_size; }
    const std::array<NexusPulse, NormalCount + ExtraCount> &pulses() const { return m_pulses; }

private:
    void initialize(NexusPulse &pulse, qint64 nowMs);
    bool offscreen(const NexusPulse &pulse, qint64 nowMs) const;
    QSizeF m_size;
    QRandomGenerator m_random;
    std::array<NexusPulse, NormalCount + ExtraCount> m_pulses;
};
