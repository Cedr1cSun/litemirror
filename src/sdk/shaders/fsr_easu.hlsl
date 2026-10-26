// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 LiteMirror contributors
//
// AMD FidelityFX Super Resolution 1.0, first pass: EASU enlarges the RGB
// picture into a texture of the output size. The algorithm itself is
// AMD's ffx_a.h / ffx_fsr1.h (MIT, third_party/fsr); this file only wires
// it to our texture and constants. Compiled by fxc at build time (ps_5_0,
// Gather needs feature level 11).
#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D<float4> src : register(t0);
SamplerState smp : register(s0);
cbuffer C : register(b0) { uint4 con0, con1, con2, con3; };   // FsrEasuCon()

#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return src.GatherRed(smp, p, int2(0, 0)); }
AF4 FsrEasuGF(AF2 p) { return src.GatherGreen(smp, p, int2(0, 0)); }
AF4 FsrEasuBF(AF2 p) { return src.GatherBlue(smp, p, int2(0, 0)); }
#include "ffx_fsr1.h"

float4 ps(float4 pos : SV_Position) : SV_Target
{
	AF3 c;
	FsrEasuF(c, AU2(pos.xy), con0, con1, con2, con3);
	return float4(c, 1.0);
}
