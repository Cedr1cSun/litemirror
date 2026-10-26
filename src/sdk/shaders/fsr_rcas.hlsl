// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// AMD FidelityFX Super Resolution 1.0, second pass: RCAS sharpens the
// enlarged picture into the letterbox area of the back buffer. See
// fsr_easu.hlsl for where the algorithm comes from.
#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D<float4> src : register(t0);
cbuffer C : register(b0) { uint4 con0; uint4 area; };   // FsrRcasCon(); area = viewport left, top, width - 1, height - 1

#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) { return src.Load(int3(clamp(p, ASU2(0, 0), ASU2(area.zw)), 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#include "ffx_fsr1.h"

float4 ps(float4 pos : SV_Position) : SV_Target
{
	AF3 c;
	FsrRcasF(c.r, c.g, c.b, AU2(pos.xy) - area.xy, con0);
	return float4(c, 1.0);
}
