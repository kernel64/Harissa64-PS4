#version 450
// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
layout(push_constant) uniform Push { vec4 rect; vec4 uvRect; } pc;
layout(location = 0) out vec2 uv;
void main()
{
    vec2 c = vec2((gl_VertexIndex & 1) != 0 ? 1.0 : 0.0, (gl_VertexIndex & 2) != 0 ? 1.0 : 0.0);
    uv = mix(pc.uvRect.xy, pc.uvRect.zw, c);
    gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, c), 0.0, 1.0);
}
