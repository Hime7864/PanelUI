#pragma once

#include "graphics.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

struct UiTheme
{
	const wchar_t* Name;
	color Window;
	color Workspace;
	color Panel;
	color Header;
	color TabHover;
	color TabActive;
	color Border;
	color Text;
	color Muted;
	color Accent;
	color AccentSoft;
	color Shadow;
	color Overlay;
	color Danger;
	color CloseHover;
	color Menu;
	color MenuHover;
	color MenuBorder;
};

inline constexpr float UiScaleBase = 96.f;
inline constexpr float UiAppBarHeight = 46.f;
inline constexpr float UiHeaderHeight = 36.f;
inline constexpr float UiTabTextSize = 12.5f;
inline constexpr float UiBodyTextSize = 13.f;
inline constexpr float UiTitleTextSize = 15.f;
inline constexpr float UiRadius = 5.f;
// Tab strip metrics shared by the layout (workspace.cpp) and the painter (ui.cpp).
inline constexpr float UiTabTextInset = 12.f;
inline constexpr float UiTabCloseSize = 18.f;
inline constexpr float UiTabCloseInset = 6.f;

// Blend between two theme colours; used for derived tones such as control outlines.
inline color UiMix(color A, color B, float T)
{
	return color(A.r + (B.r - A.r) * T, A.g + (B.g - A.g) * T, A.b + (B.b - A.b) * T, A.a + (B.a - A.a) * T);
}

// Rounds every edge to a whole pixel. Layout snaps its rects so 1 px borders and separators cover one
// pixel row fully instead of straddling two at half intensity (which reads as a blurred, doubled line).
// Edges round independently, so rects that share an edge still share it after snapping.
inline rect UiSnap(rect Value)
{
	return rect(std::round(Value.left), std::round(Value.top), std::round(Value.right), std::round(Value.bottom));
}

// A rounded shape with an outline, drawn as two fills: the edge colour, then the fill inset by Edge.
// Fill-then-stroke puts two separately anti-aliased silhouettes on every curved edge, which on small
// shapes (pills, swatches, indicator boxes) shows as a faint halo or a doubled rim.
inline void UiFramedRoundedRect(rect Bounds, float Radius, float Edge, color EdgeColor, color FillColor)
{
	renderer::draw_filled_rounded_rect(Bounds, Radius, EdgeColor);
	renderer::draw_filled_rounded_rect(rect(Bounds.left + Edge, Bounds.top + Edge, Bounds.right - Edge, Bounds.bottom - Edge),
		std::max(0.f, Radius - Edge), FillColor);
}

inline color UiHex(uint32_t Value, float Alpha = 1.f)
{
	const float R = static_cast<float>((Value >> 16) & 0xff) / 255.f;
	const float G = static_cast<float>((Value >> 8) & 0xff) / 255.f;
	const float B = static_cast<float>(Value & 0xff) / 255.f;
	return color(R, G, B, Alpha);
}

inline const std::vector<UiTheme>& UiThemes()
{
	static const std::vector<UiTheme> List =
	{
		{
			L"Dark",
			UiHex(0x0d0f13),
			UiHex(0x0a0c10),
			UiHex(0x14181e),
			UiHex(0x191e26),
			UiHex(0x222833),
			UiHex(0x1b212b),
			UiHex(0x262d38),
			UiHex(0xe8ecf3),
			UiHex(0x8b95a7),
			UiHex(0x5b9dff),
			UiHex(0x5b9dff, 0.16f),
			UiHex(0x000000, 0.45f),
			UiHex(0x000000, 0.50f),
			UiHex(0xff6b6b),
			UiHex(0xd94a4a, 0.92f),
			UiHex(0x1a1f27),
			UiHex(0x262d38),
			UiHex(0x2c3442)
		},
		{
			L"Graphite",
			UiHex(0x151515),
			UiHex(0x101010),
			UiHex(0x1c1c1c),
			UiHex(0x232323),
			UiHex(0x2c2c2c),
			UiHex(0x262626),
			UiHex(0x333333),
			UiHex(0xf2f2f2),
			UiHex(0x9c9c9c),
			UiHex(0xc7ccd4),
			UiHex(0xc7ccd4, 0.16f),
			UiHex(0x000000, 0.50f),
			UiHex(0x000000, 0.55f),
			UiHex(0xff6b6b),
			UiHex(0xd94a4a, 0.92f),
			UiHex(0x202020),
			UiHex(0x2c2c2c),
			UiHex(0x3a3a3a)
		},
		{
			L"Midnight",
			UiHex(0x080d18),
			UiHex(0x060a12),
			UiHex(0x0e1728),
			UiHex(0x132039),
			UiHex(0x1b2b49),
			UiHex(0x16223a),
			UiHex(0x20314e),
			UiHex(0xdce8ff),
			UiHex(0x7f93b6),
			UiHex(0x63a3ff),
			UiHex(0x63a3ff, 0.18f),
			UiHex(0x000000, 0.50f),
			UiHex(0x000000, 0.55f),
			UiHex(0xff7070),
			UiHex(0xe05555, 0.92f),
			UiHex(0x101a2d),
			UiHex(0x1b2b49),
			UiHex(0x243a5e)
		},
		{
			L"Cyber",
			UiHex(0x05060a),
			UiHex(0x03040a),
			UiHex(0x0a0f18),
			UiHex(0x0d1522),
			UiHex(0x122033),
			UiHex(0x102236),
			UiHex(0x133a4a),
			UiHex(0xe6feff),
			UiHex(0x6d8fa3),
			UiHex(0x00e5ff),
			UiHex(0x00e5ff, 0.16f),
			UiHex(0x000000, 0.55f),
			UiHex(0x000000, 0.55f),
			UiHex(0xff4d6d),
			UiHex(0xff4d6d, 0.92f),
			UiHex(0x081018),
			UiHex(0x102838),
			UiHex(0x155064)
		},
		{
			L"Light",
			UiHex(0xe9edf4),
			UiHex(0xf2f4f8),
			UiHex(0xffffff),
			UiHex(0xf6f8fb),
			UiHex(0xedf1f7),
			UiHex(0xf4f7fb),
			UiHex(0xdbe1ea),
			UiHex(0x121722),
			UiHex(0x6a7484),
			UiHex(0x2f6fed),
			UiHex(0x2f6fed, 0.14f),
			UiHex(0x1a2436, 0.18f),
			UiHex(0x000000, 0.30f),
			UiHex(0xd93a3a),
			UiHex(0xd93a3a, 0.92f),
			UiHex(0xffffff),
			UiHex(0xedf1f7),
			UiHex(0xdbe1ea)
		},
		{
			L"Arctic",
			UiHex(0xeef4fa),
			UiHex(0xf4f8fc),
			UiHex(0xffffff),
			UiHex(0xf0f6fc),
			UiHex(0xe4eef8),
			UiHex(0xeaf3fb),
			UiHex(0xcfdde9),
			UiHex(0x0d1b26),
			UiHex(0x627b8d),
			UiHex(0x1f7fa8),
			UiHex(0x1f7fa8, 0.14f),
			UiHex(0x1a2436, 0.18f),
			UiHex(0x000000, 0.30f),
			UiHex(0xd93a3a),
			UiHex(0xd93a3a, 0.92f),
			UiHex(0xffffff),
			UiHex(0xe4eef8),
			UiHex(0xcfdde9)
		},
		{
			L"Sand",
			UiHex(0xf3ead9),
			UiHex(0xf8f1e4),
			UiHex(0xfffcf6),
			UiHex(0xf7efe1),
			UiHex(0xefe4d2),
			UiHex(0xfff7ea),
			UiHex(0xded0b8),
			UiHex(0x2a2118),
			UiHex(0x8a7962),
			UiHex(0xb8742f),
			UiHex(0xb8742f, 0.14f),
			UiHex(0x2a2118, 0.18f),
			UiHex(0x000000, 0.30f),
			UiHex(0xc94b3a),
			UiHex(0xc94b3a, 0.92f),
			UiHex(0xfffcf6),
			UiHex(0xefe4d2),
			UiHex(0xded0b8)
		},
		{
			L"Lavender",
			UiHex(0xefecf8),
			UiHex(0xf5f3fb),
			UiHex(0xffffff),
			UiHex(0xf3f0fa),
			UiHex(0xe9e4f6),
			UiHex(0xf7f4ff),
			UiHex(0xd8d1e8),
			UiHex(0x1e1830),
			UiHex(0x7b7093),
			UiHex(0x7c5cff),
			UiHex(0x7c5cff, 0.14f),
			UiHex(0x1e1830, 0.18f),
			UiHex(0x000000, 0.30f),
			UiHex(0xd93a3a),
			UiHex(0xd93a3a, 0.92f),
			UiHex(0xffffff),
			UiHex(0xe9e4f6),
			UiHex(0xd8d1e8)
		}
	};

	return List;
}
