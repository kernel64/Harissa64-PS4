#version 450
// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
// - smoothing: FXAA 2 (Timothy Lottes, public domain);
//   Timothy Lottes's CRT shader, public domain; no phosphor mask), 4 LCD grid;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 0, binding = 1) uniform sampler2D prevTex;
layout(push_constant) uniform Push
{
    vec4 rect;
    vec4 uvRect;
    vec4 rcp;
    vec4 prm;
    ivec4 mode;
} pc;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 tap(vec2 p)
{
    p = clamp(p, 0.5 * pc.rcp.xy, 1.0 - 0.5 * pc.rcp.xy);
    vec3 c = textureLod(tex, p, 0.0).rgb;
    if (pc.prm.w > 0.0) c = mix(c, textureLod(prevTex, p, 0.0).rgb, pc.prm.w);
    return c;
}

vec3 base(vec2 p)
{
    if (pc.mode.x == 0) return tap(p);
    vec3 rgbM = tap(p);
    float lNW = luma(tap(p + vec2(-0.5, -0.5) * pc.rcp.xy));
    float lNE = luma(tap(p + vec2(0.5, -0.5) * pc.rcp.xy));
    float lSW = luma(tap(p + vec2(-0.5, 0.5) * pc.rcp.xy));
    float lSE = luma(tap(p + vec2(0.5, 0.5) * pc.rcp.xy));
    float lM = luma(rgbM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, -8.0, 8.0) * pc.rcp.xy;
    vec3 a = 0.5 * (tap(p + dir * (1.0 / 3.0 - 0.5)) + tap(p + dir * (2.0 / 3.0 - 0.5)));
    vec3 b = a * 0.5 + 0.25 * (tap(p - dir * 0.5) + tap(p + dir * 0.5));
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}

vec3 picture(vec2 p)
{
    vec3 c = base(p);
    if (pc.mode.z != 0)
    {
        vec2 d = vec2(1.0) / pc.prm.xy * (pc.mode.z == 1 ? 0.75 : 1.5);
        c = (4.0 * c + 2.0 * (tap(p + vec2(d.x, 0.0)) + tap(p - vec2(d.x, 0.0)) + tap(p + vec2(0.0, d.y)) + tap(p - vec2(0.0, d.y))) +
             tap(p + d) + tap(p - d) + tap(p + vec2(d.x, -d.y)) + tap(p + vec2(-d.x, d.y))) / 16.0;
    }
    else if (pc.mode.y != 0)
    {
        vec3 n = tap(p - vec2(0.0, pc.rcp.y)), so = tap(p + vec2(0.0, pc.rcp.y));
        vec3 w = tap(p - vec2(pc.rcp.x, 0.0)), e = tap(p + vec2(pc.rcp.x, 0.0));
        vec3 mn = min(c, min(min(n, so), min(w, e))), mx = max(c, max(max(n, so), max(w, e)));
        vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, vec3(0.0001)), 0.0, 1.0));
        vec3 wt = amp * (pc.mode.y == 1 ? -0.125 : -0.2);
        c = clamp((c + wt * (n + so + w + e)) / (1.0 + 4.0 * wt), 0.0, 1.0);
    }
    return c;
}

void main()
{
    int screen = pc.mode.w;
    vec2 n = uv;
    vec2 edge = vec2(1.0);
    vec3 c;
    if (screen == 3)
    {
        vec2 cc = n * 2.0 - 1.0;
        cc *= 1.0 + vec2(0.045, 0.06) * (cc.yx * cc.yx);
        n = cc * 0.5 + 0.5;
        edge = clamp((0.5 - abs(n - 0.5)) * vec2(80.0, 60.0), 0.0, 1.0);
        if (edge.x * edge.y <= 0.0)
        {
            color = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
    }
    if (screen == 2 || screen == 3)
    {
        float lines = pc.prm.y;
        float y = n.y * lines - 0.5;
        float y0 = floor(y), f = y - y0;
        vec2 px = vec2(0.5 / pc.prm.x, 0.0);
        vec2 uA = vec2(n.x, (y0 + 0.5) / lines);
        vec2 uB = vec2(n.x, (y0 + 1.5) / lines);
        vec3 cA = 0.25 * (tap(uA - px) + tap(uA + px)) + 0.5 * tap(uA);
        vec3 cB = 0.25 * (tap(uB - px) + tap(uB + px)) + 0.5 * tap(uB);
        float kA = mix(18.0, 6.0, luma(cA)), kB = mix(18.0, 6.0, luma(cB));
        c = cA * exp(-f * f * kA) + cB * exp(-(1.0 - f) * (1.0 - f) * kB);
        vec3 glow = 0.25 * (tap(n + vec2(4.0, 0.0) * pc.rcp.xy) + tap(n - vec2(4.0, 0.0) * pc.rcp.xy) +
                            tap(n + vec2(0.0, 4.0) * pc.rcp.xy) + tap(n - vec2(0.0, 4.0) * pc.rcp.xy));
        c = c * 1.3 + glow * 0.12;
        if (screen == 3)
        {
            vec2 v = n * (1.0 - n);
            c *= pow(clamp(v.x * v.y * 24.0, 0.0, 1.0), 0.18) * edge.x * edge.y;
        }
    }
    else
    {
        c = picture(n);
        if (screen == 1)
        {
            float d = fract(n.y * pc.prm.y) - 0.5;
            c *= exp(-d * d * mix(10.0, 4.0, luma(c))) * 1.3;
        }
        else if (screen == 5)
        {
            float d = fract(n.y * pc.prm.y) - 0.5;
            c *= mix(0.78, 1.08, exp(-d * d * 8.0));
        }
        else if (screen == 4)
        {
            vec2 g = fract(n * pc.prm.xy);
            vec2 gap = smoothstep(0.0, 0.18, g) * smoothstep(1.0, 0.82, g);
            c *= mix(0.55, 1.08, gap.x * gap.y);
        }
    }
    color = vec4(pow(clamp(c, 0.0, 1.0), vec3(pc.prm.z)), 1.0);
}
