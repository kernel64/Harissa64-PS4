#version 450
// Harissa64 - Nintendo 64 emulator for the PlayStation 4
// Copyright (C) 2026 Mohamed Aymen
// SPDX-License-Identifier: GPL-2.0-only
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 col;
layout(location = 2) in vec4 t;
layout(location = 3) in vec4 p;
layout(location = 0) out vec4 vcol;
layout(location = 1) out vec4 vt;
layout(location = 2) out vec4 vp;
void main()
{
    vcol = col;
    vt = t;
    vp = p;
    gl_Position = vec4(pos.x, -pos.y, 0.0, 1.0);
}
