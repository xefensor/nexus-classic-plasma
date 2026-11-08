// SPDX-License-Identifier: Apache-2.0
// Nexus Classic — Android 2.1 / 2.2 live wallpaper, adapted for Plasma.
// Original visuals and pulse parameters: Copyright (C) 2009 The Android
// Open Source Project. See NOTICE and LICENSE for source and attribution.
// Retro wallpaper with one continuous, clock-synchronised desktop canvas.
// Enable Advanced > Experimental > Span all screens on each monitor.
// @mouse
// @channels tex0, tex1, tex2, tex3
// @texture0 Shaders/Retro/NexusClassic/assets/pyramid_background.png
// @texture1 Shaders/Retro/NexusClassic/assets/pulse.png
// @texture2 Shaders/Retro/NexusClassic/assets/glow.png

const float CELL = 14.0;
const float TRAIL = 560.0;
const float GLOW = 64.0;
const float CLOCK_LOOP = 120.0; // Divides a day: no jump at midnight.
const int PULSES = 20;
const int STATE_WIDTH = 184;

uvec4 nexusBytes(int index)
{
    return uvec4(round(texelFetch(iChannel3, ivec2(index, 0), 0) * 255.0));
}

float nexusFloat(int index)
{
    uvec4 b = nexusBytes(index);
    return uintBitsToFloat(b.x | (b.y << 8) | (b.z << 16) | (b.w << 24));
}

uint nexusHash(uint n)
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    return n ^ (n >> 16);
}

float nexusRandom(uint n)
{
    return float(nexusHash(n) >> 8) * (1.0 / 16777216.0);
}

vec3 nexusColor(int n)
{
    if (n == 0) return vec3(1.0, 0.0, 0.0);
    if (n == 1) return vec3(0.0, 0.8, 0.0);
    if (n == 2) return vec3(0.0, 0.4, 0.9);
    return vec3(1.0, 0.8, 0.0);
}

vec3 nexusPulse(vec2 pixel, vec2 head, vec2 direction, vec3 tint, float aa)
{
    vec2 delta = pixel - head;
    float behind = -dot(delta, direction);
    float across = abs(dot(delta, vec2(-direction.y, direction.x)));
    float body = (1.0 - smoothstep(CELL * 0.5 - aa, CELL * 0.5 + aa, across))
               * smoothstep(-CELL * 0.5 - aa, -CELL * 0.5 + aa, behind)
               * (1.0 - smoothstep(TRAIL - CELL * 0.5 - aa, TRAIL - CELL * 0.5 + aa, behind));
    float tail = 0.0;
    if (body > 0.0) {
        float u = clamp((behind + CELL * 0.5) / TRAIL, 0.5 / 128.0, 127.5 / 128.0);
        tail = textureLod(iChannel1, vec2(u, 0.5), 0.0).a * body;
    }
    vec3 halo = vec3(0.0);
    if (max(abs(delta.x), abs(delta.y)) < GLOW * 0.5) {
        halo = textureLod(iChannel2, delta / GLOW + 0.5, 0.0).rgb;
    }
    // The original RenderScript wallpaper uses SRC_ALPHA, ONE blending.
    return 0.8 * tint * (tail + halo);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 desktop = iDesktopResolution;
    if (desktop.x <= 0.0 || desktop.y <= 0.0) desktop = iResolution.xy;

    bool haveState = false;
    if (iChannelResolution[3].x >= float(STATE_WIDTH)) {
        haveState = abs(nexusFloat(0) - 21022.0) < 0.5;
    }
    // The companion supplies Qt logical dimensions: 14 logical pixels per
    // cell/trail, including when adjacent monitors use different DPI.
    // Old hosts without the companion still show a smaller ambient preview.
    vec2 canvas = haveState ? vec2(nexusFloat(1), nexusFloat(2))
                           : desktop * (1280.0 / max(desktop.y, 1.0));
    vec2 pixel = (fragCoord + iScreenOffset) / desktop * canvas;
    float aa = max(fwidth(pixel.x), fwidth(pixel.y));

    // Preserve the original pyramid texture AND its baked lighting. The
    // detail repeats at its original 14-pixel scale; a mip level extracts
    // the original broad lighting without stretching the tiny pyramids.
    vec2 tilePixel = mod(vec2(pixel.x, canvas.y - pixel.y), CELL);
    vec2 texel = vec2(476.0, 392.0) + tilePixel;
    vec3 pyramid = textureLod(iChannel0, vec2(texel.x / 960.0, 1.0 - texel.y / 800.0), 0.0).rgb;
    vec3 tileMean = textureLod(iChannel0, vec2(483.0 / 960.0, 1.0 - 399.0 / 800.0), 4.0).rgb;
    vec2 uv = pixel / canvas;
    vec3 light = textureLod(iChannel0, clamp(uv, vec2(0.001), vec2(0.999)), 4.0).rgb;
    vec3 color = pyramid / max(tileMean, vec3(0.02)) * light;

    if (haveState) {
        // The same small state texture is shared by every desktop instance.
        // 20 ambient trails plus the original pool of up to 40 tap trails.
        int count = clamp(int(nexusFloat(3) + 0.5), 0, 60);
        for (int i = 0; i < 60; ++i) {
            if (i >= count) break;
            int record = 4 + 3 * i;
            float age = iDate.w - nexusFloat(record + 1);
            if (age < -43200.0) age += 86400.0;
            if (age > 43200.0) age -= 86400.0;
            if (age < 0.0) continue;
            uvec4 originBytes = nexusBytes(record);
            vec2 origin = vec2(originBytes.x | (originBytes.y << 8),
                               originBytes.z | (originBytes.w << 8));
            origin = origin / 65535.0 * (canvas + 2.0 * CELL) - CELL;
            uvec4 details = nexusBytes(record + 2);
            float speed = float(details.x | (details.y << 8)) / 128.0;
            int directionId = int(details.z >> 2);
            vec2 direction = directionId == 0 ? vec2(1.0, 0.0)
                           : directionId == 1 ? vec2(-1.0, 0.0)
                           : directionId == 2 ? vec2(0.0, -1.0) : vec2(0.0, 1.0);
            vec2 head = origin + direction * speed * age;
            color += nexusPulse(pixel, head, direction, nexusColor(int(details.z & 3u)), aa);
        }
        fragColor = vec4(color, 1.0);
        return;
    }

    // iTime belongs to each renderer. iDate.w is the shared system clock:
    // every monitor evaluates the same pulses, even after opening/resuming.
    float clock = mod(iDate.w, CLOCK_LOOP);
    for (int i = 0; i < PULSES; ++i) {
        uint seed = uint(i + 1) * 977u;
        bool horizontal = nexusRandom(seed) >= 0.5;
        float signDir = nexusRandom(seed + 1u) >= 0.5 ? 1.0 : -1.0;
        float extent = horizontal ? canvas.x : canvas.y;
        float speed = 200.0 * mix(0.7, 1.3, nexusRandom(seed + 2u));
        float delay = 2.0 * nexusRandom(seed + 3u);
        float travel = extent + TRAIL + GLOW;
        float cycles = max(1.0, floor(CLOCK_LOOP / (travel / speed + delay)));
        float period = CLOCK_LOOP / cycles;
        float phase = clock / period + nexusRandom(seed + 4u);
        uint generation = uint(mod(floor(phase), cycles));
        float age = fract(phase) * period - delay;
        float distance = age * travel / (period - delay) - GLOW * 0.5;
        float crossExtent = horizontal ? canvas.y : canvas.x;
        float laneCount = max(1.0, floor(crossExtent / CELL));
        float lane = (floor(nexusRandom(seed + 11u + generation * 41u) * laneCount) + 0.5) * CELL;
        float along = signDir > 0.0 ? distance : extent - distance;
        vec2 head = horizontal ? vec2(along, lane) : vec2(lane, along);
        vec2 direction = horizontal ? vec2(signDir, 0.0) : vec2(0.0, signDir);
        int c = int(nexusRandom(seed + 23u + generation * 41u) * 4.0);
        color += nexusPulse(pixel, head, direction, nexusColor(c), aa);
    }
    fragColor = vec4(color, 1.0);
}
