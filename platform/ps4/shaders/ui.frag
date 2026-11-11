#version 450
// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(set = 0, binding = 1) uniform sampler2D image;
layout(location = 0) in vec4 col;
layout(location = 1) in vec4 t;
layout(location = 2) in vec4 p;
layout(location = 0) out vec4 color;

float symbol()
{
    vec2 q = t.xy;
    float d;
    if (p.x < 0.5)
    {
        float u = (q.x + q.y) * 0.70710678, v = (q.x - q.y) * 0.70710678;
        float d1 = length(vec2(max(abs(u) - p.y, 0.0), v));
        float d2 = length(vec2(max(abs(v) - p.y, 0.0), u));
        d = min(d1, d2) - p.z;
    }
    else
    {
        const float k = 1.7320508;
        vec2 r = vec2(abs(q.x) - p.y, -q.y + p.y / k);
        if (r.x + k * r.y > 0.0) r = vec2(r.x - k * r.y, -k * r.x - r.y) * 0.5;
        r.x -= clamp(r.x, -2.0 * p.y, 0.0);
        d = abs(-length(r) * sign(r.y)) - p.z;
    }
    float a = clamp(0.5 - d / p.w, 0.0, 1.0);
    return a * a * (3.0 - 2.0 * a);
}

void main()
{
    vec2 q = abs(t.xy) - p.xy + p.z;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - p.z;
    d = t.w > 0.0 ? abs(d + t.w) - t.w : d;
    float shape = clamp(0.5 - d / p.w, 0.0, 1.0);
    shape = shape * shape * (3.0 - 2.0 * shape);
    float text = clamp((texture(atlas, t.xy).x - 0.5) * p.x + p.y, 0.0, 1.0);
    vec4 img = (t.z > 1.5 && t.z < 2.5) ? texture(image, t.xy / (2.0 * p.xy) + 0.5) : vec4(1.0);
    float cover = t.z > 2.5 ? symbol() : (t.z > 0.5 && t.z < 1.5) ? text : shape;
    color = vec4(col.rgb * img.rgb, col.a * img.a * cover);
}
