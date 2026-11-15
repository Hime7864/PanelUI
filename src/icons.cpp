#include "icons.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	// Icons are authored in a unit square and mapped into the target bounds, so any size stays crisp.
	struct pen
	{
		vec2 origin;
		float size;
		float stroke;
		color tint;

		vec2 at(float x, float y) const { return vec2(origin.x + x * size, origin.y + y * size); }

		void line(float x0, float y0, float x1, float y1) const
		{
			renderer::draw_rounded_line(at(x0, y0), at(x1, y1), stroke, tint);
		}

		void polyline(const float* points, int count, bool closed) const
		{
			std::vector<vec2> path;
			path.reserve(static_cast<size_t>(count) + 1);
			for (int i = 0; i < count; i++)
				path.push_back(at(points[i * 2], points[i * 2 + 1]));
			if (closed)
				path.push_back(path.front());

			renderer::draw_polyline(path, stroke, tint);
		}

		// Circular arc as a polyline: avoids needing a background colour to cut a ring out of a disc.
		void arc(float cx, float cy, float radius, float from, float to) const
		{
			const int steps = std::max(8, static_cast<int>(std::fabs(to - from) / Pi * 18.f));
			std::vector<vec2> path;
			path.reserve(static_cast<size_t>(steps) + 1);
			for (int i = 0; i <= steps; i++)
			{
				const float angle = from + (to - from) * (static_cast<float>(i) / steps);
				path.push_back(at(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius));
			}

			renderer::draw_polyline(path, stroke, tint);
		}

		void box(float x0, float y0, float x1, float y1) const
		{
			const float points[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
			polyline(points, 4, true);
		}

		void disc(float cx, float cy, float radius) const
		{
			renderer::draw_filled_circle(at(cx, cy), radius * size, tint);
		}

		void solid(float x0, float y0, float x1, float y1, float radius) const
		{
			// rect(point, size) would read the second corner as a size; build from explicit edges.
			const vec2 A = at(x0, y0);
			const vec2 B = at(x1, y1);
			renderer::draw_filled_rounded_rect(rect(A.x, A.y, B.x, B.y), radius * size, tint);
		}
	};
}

void DrawUiIcon(UiIcon Icon, rect Bounds, color Tint, float StrokeWidth)
{
	if (Icon == UiIcon::None || Tint.a <= 0.f)
		return;

	const float Extent = std::min(Bounds.width(), Bounds.height());
	if (Extent < 3.f)
		return;

	// Default weight follows common 24-unit icon sets (2 units on 24): ~1.35 px at 16 px.
	const float Stroke = StrokeWidth > 0.f ? StrokeWidth : std::max(1.25f, Extent * 0.085f);
	const float Margin = Stroke * 0.5f + 0.5f;
	// Centre the unit square in the bounds so non-square hit rects do not push the glyph to a corner.
	const vec2 Center = Bounds.center();
	const float Size = Extent - Margin * 2.f;
	const vec2 Origin(Center.x - Size * 0.5f, Center.y - Size * 0.5f);
	const pen Pen{ Origin, Size, Stroke, Tint };

	switch (Icon)
	{
	case UiIcon::Close:
		Pen.line(0.16f, 0.16f, 0.84f, 0.84f);
		Pen.line(0.84f, 0.16f, 0.16f, 0.84f);
		break;

	case UiIcon::Plus:
		Pen.line(0.15f, 0.5f, 0.85f, 0.5f);
		Pen.line(0.5f, 0.15f, 0.5f, 0.85f);
		break;

	case UiIcon::Minus:
		Pen.line(0.15f, 0.5f, 0.85f, 0.5f);
		break;

	case UiIcon::Check:
	{
		const float points[6] = { 0.15f, 0.55f, 0.40f, 0.78f, 0.85f, 0.25f };
		Pen.polyline(points, 3, false);
		break;
	}

	case UiIcon::ChevronDown:
	{
		const float points[6] = { 0.20f, 0.40f, 0.50f, 0.68f, 0.80f, 0.40f };
		Pen.polyline(points, 3, false);
		break;
	}

	case UiIcon::ChevronUp:
	{
		const float points[6] = { 0.20f, 0.60f, 0.50f, 0.32f, 0.80f, 0.60f };
		Pen.polyline(points, 3, false);
		break;
	}

	case UiIcon::ChevronLeft:
	{
		const float points[6] = { 0.60f, 0.20f, 0.32f, 0.50f, 0.60f, 0.80f };
		Pen.polyline(points, 3, false);
		break;
	}

	case UiIcon::ChevronRight:
	{
		const float points[6] = { 0.40f, 0.20f, 0.68f, 0.50f, 0.40f, 0.80f };
		Pen.polyline(points, 3, false);
		break;
	}

	case UiIcon::Search:
		Pen.arc(0.44f, 0.44f, 0.28f, 0.f, Pi * 2.f);
		Pen.line(0.64f, 0.64f, 0.86f, 0.86f);
		break;

	case UiIcon::Settings:
	{
		// Ring plus detached teeth: a gear silhouette with a visible hole, and no overlapping strokes
		// (overlap is what made the old hand-drawn gear look doubled).
		Pen.arc(0.5f, 0.5f, 0.27f, 0.f, Pi * 2.f);
		for (int i = 0; i < 8; i++)
		{
			const float angle = static_cast<float>(i) / 8.f * Pi * 2.f + Pi / 8.f;
			const float in_x = 0.5f + std::cos(angle) * 0.34f;
			const float in_y = 0.5f + std::sin(angle) * 0.34f;
			const float out_x = 0.5f + std::cos(angle) * 0.46f;
			const float out_y = 0.5f + std::sin(angle) * 0.46f;
			Pen.line(in_x, in_y, out_x, out_y);
		}
		break;
	}

	case UiIcon::Folder:
	{
		const float tab[8] = { 0.10f, 0.30f, 0.38f, 0.30f, 0.46f, 0.20f, 0.10f, 0.20f };
		Pen.polyline(tab, 4, false);
		Pen.box(0.10f, 0.30f, 0.90f, 0.82f);
		break;
	}

	case UiIcon::File:
		Pen.box(0.18f, 0.10f, 0.72f, 0.90f);
		Pen.line(0.72f, 0.10f, 0.86f, 0.28f);
		Pen.line(0.72f, 0.10f, 0.60f, 0.28f);
		Pen.line(0.60f, 0.28f, 0.86f, 0.28f);
		break;

	case UiIcon::Image:
		Pen.box(0.10f, 0.16f, 0.90f, 0.84f);
		Pen.arc(0.32f, 0.36f, 0.08f, 0.f, Pi * 2.f);
	{
		const float ridge[6] = { 0.16f, 0.76f, 0.40f, 0.50f, 0.58f, 0.68f };
		Pen.polyline(ridge, 3, false);
		const float ridge2[4] = { 0.58f, 0.68f, 0.84f, 0.42f };
		Pen.polyline(ridge2, 2, false);
		break;
	}

	case UiIcon::Play:
		renderer::draw_filled_triangle(Pen.at(0.28f, 0.14f), Pen.at(0.28f, 0.86f), Pen.at(0.86f, 0.5f), Tint);
		break;

	case UiIcon::Pause:
		Pen.solid(0.26f, 0.16f, 0.42f, 0.84f, 0.06f);
		Pen.solid(0.58f, 0.16f, 0.74f, 0.84f, 0.06f);
		break;

	case UiIcon::Stop:
		Pen.solid(0.20f, 0.20f, 0.80f, 0.80f, 0.12f);
		break;

	case UiIcon::Refresh:
		Pen.arc(0.5f, 0.5f, 0.32f, -Pi * 0.15f, Pi * 1.35f);
		Pen.line(0.82f, 0.18f, 0.82f, 0.44f);
		Pen.line(0.82f, 0.18f, 0.56f, 0.22f);
		break;

	case UiIcon::Trash:
		Pen.line(0.14f, 0.26f, 0.86f, 0.26f);
		Pen.line(0.36f, 0.26f, 0.36f, 0.16f);
		Pen.line(0.64f, 0.26f, 0.64f, 0.16f);
		Pen.line(0.36f, 0.16f, 0.64f, 0.16f);
		Pen.box(0.22f, 0.26f, 0.78f, 0.88f);
		Pen.line(0.42f, 0.42f, 0.42f, 0.72f);
		Pen.line(0.58f, 0.42f, 0.58f, 0.72f);
		break;

	case UiIcon::Info:
		Pen.arc(0.5f, 0.5f, 0.36f, 0.f, Pi * 2.f);
		Pen.disc(0.5f, 0.30f, 0.045f);
		Pen.line(0.5f, 0.44f, 0.5f, 0.72f);
		break;

	case UiIcon::Warning:
	{
		const float triangle[6] = { 0.5f, 0.12f, 0.92f, 0.86f, 0.08f, 0.86f };
		Pen.polyline(triangle, 3, true);
		Pen.line(0.5f, 0.40f, 0.5f, 0.62f);
		Pen.disc(0.5f, 0.73f, 0.045f);
		break;
	}

	case UiIcon::Error:
		Pen.arc(0.5f, 0.5f, 0.36f, 0.f, Pi * 2.f);
		Pen.line(0.36f, 0.36f, 0.64f, 0.64f);
		Pen.line(0.64f, 0.36f, 0.36f, 0.64f);
		break;

	case UiIcon::Star:
	{
		float points[20];
		for (int i = 0; i < 10; i++)
		{
			const float angle = -Pi * 0.5f + static_cast<float>(i) * Pi / 5.f;
			const float radius = (i % 2 == 0) ? 0.40f : 0.17f;
			points[i * 2] = 0.5f + std::cos(angle) * radius;
			points[i * 2 + 1] = 0.5f + std::sin(angle) * radius;
		}
		Pen.polyline(points, 10, true);
		break;
	}

	case UiIcon::Lock:
		Pen.box(0.18f, 0.44f, 0.82f, 0.88f);
		Pen.arc(0.5f, 0.44f, 0.20f, Pi, Pi * 2.f);
		Pen.disc(0.5f, 0.62f, 0.05f);
		break;

	case UiIcon::Menu:
		Pen.line(0.14f, 0.28f, 0.86f, 0.28f);
		Pen.line(0.14f, 0.50f, 0.86f, 0.50f);
		Pen.line(0.14f, 0.72f, 0.86f, 0.72f);
		break;

	case UiIcon::Grid:
		Pen.box(0.12f, 0.12f, 0.45f, 0.45f);
		Pen.box(0.55f, 0.12f, 0.88f, 0.45f);
		Pen.box(0.12f, 0.55f, 0.45f, 0.88f);
		Pen.box(0.55f, 0.55f, 0.88f, 0.88f);
		break;

	case UiIcon::List:
		for (int i = 0; i < 3; i++)
		{
			const float y = 0.24f + static_cast<float>(i) * 0.26f;
			Pen.disc(0.18f, y, 0.05f);
			Pen.line(0.34f, y, 0.86f, y);
		}
		break;

	case UiIcon::WindowMaximize:
		Pen.box(0.16f, 0.16f, 0.84f, 0.84f);
		break;

	case UiIcon::WindowRestore:
		Pen.box(0.12f, 0.30f, 0.66f, 0.88f);
		Pen.box(0.34f, 0.12f, 0.88f, 0.70f);
		break;

	default:
		break;
	}
}

const wchar_t* UiIconName(UiIcon Icon)
{
	static const wchar_t* const Names[] =
	{
		L"none", L"close", L"plus", L"minus", L"check",
		L"chevron-down", L"chevron-up", L"chevron-left", L"chevron-right",
		L"search", L"settings", L"folder", L"file", L"image",
		L"play", L"pause", L"stop", L"refresh", L"trash",
		L"info", L"warning", L"error", L"star", L"lock",
		L"menu", L"grid", L"list", L"window-maximize", L"window-restore"
	};

	const int Index = static_cast<int>(Icon);
	if (Index < 0 || Index >= static_cast<int>(std::size(Names)))
		return L"none";

	return Names[Index];
}
