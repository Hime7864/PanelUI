#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "windowscodecs.lib")

struct vec2;
struct vec3;
struct vec4;

template <typename t>
using enable_primitive = typename std::enable_if<std::is_arithmetic<t>::value>::type;

struct vec2
{
	float x;
	float y;

	vec2() : x(0.f), y(0.f) {}
	vec2(const POINT& point) : x(static_cast<float>(point.x)), y(static_cast<float>(point.y)) {}
	vec2(const vec2& other) : x(other.x), y(other.y) {}
	vec2(const vec3& other);
	vec2(const vec4& other);

	template <typename t0, typename = enable_primitive<t0>>
	vec2(t0 value) : x(static_cast<float>(value)), y(static_cast<float>(value)) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>>
	vec2(t0 px, t1 py) : x(static_cast<float>(px)), y(static_cast<float>(py)) {}

	vec2 operator+(const vec2& rhs) const { return vec2(x + rhs.x, y + rhs.y); }
	vec2 operator-(const vec2& rhs) const { return vec2(x - rhs.x, y - rhs.y); }
	vec2 operator*(const vec2& rhs) const { return vec2(x * rhs.x, y * rhs.y); }
	vec2 operator/(const vec2& rhs) const { return vec2(x / rhs.x, y / rhs.y); }
	vec2 operator%(const vec2& rhs) const { return vec2(std::fmod(x, rhs.x), std::fmod(y, rhs.y)); }
	vec2& operator+=(const vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }
	vec2& operator-=(const vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
	vec2& operator*=(const vec2& rhs) { x *= rhs.x; y *= rhs.y; return *this; }
	vec2& operator/=(const vec2& rhs) { x /= rhs.x; y /= rhs.y; return *this; }
	bool operator==(const vec2& rhs) const { return x == rhs.x && y == rhs.y; }
	bool operator!=(const vec2& rhs) const { return x != rhs.x || y != rhs.y; }

	float dot(const vec2& rhs) const { return x * rhs.x + y * rhs.y; }
	float length() const { return std::sqrt(x * x + y * y); }
	float distance(const vec2& rhs) const { return (rhs - *this).length(); }
	vec2 normalize() const { const float len = length(); return vec2(x / len, y / len); }
	vec2 rotate(float angle) const;
	vec2 rotate(float angle, const vec2& origin) const;
	vec2 lerp(const vec2& rhs, float t) const { return vec2(x + t * (rhs.x - x), y + t * (rhs.y - y)); }
	vec2 reflect(const vec2& normal) const;
	vec2 project(const vec2& rhs) const;
	vec2 perpendicular() const { return vec2(-y, x); }
	vec2 perpendicular(const vec2& rhs) const { return vec2(-rhs.y, rhs.x); }
};

struct vec3
{
	float x;
	float y;
	float z;

	vec3() : x(0.f), y(0.f), z(0.f) {}
	vec3(const POINT& point) : x(static_cast<float>(point.x)), y(static_cast<float>(point.y)), z(0.f) {}
	vec3(const vec2& other) : x(other.x), y(other.y), z(0.f) {}
	vec3(const vec3& other) : x(other.x), y(other.y), z(other.z) {}
	vec3(const vec4& other);

	template <typename t0, typename = enable_primitive<t0>>
	vec3(t0 value) : x(static_cast<float>(value)), y(static_cast<float>(value)), z(static_cast<float>(value)) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>>
	vec3(t0 px, t1 py) : x(static_cast<float>(px)), y(static_cast<float>(py)), z(0.f) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>, typename t2, typename = enable_primitive<t2>>
	vec3(t0 px, t1 py, t2 pz) : x(static_cast<float>(px)), y(static_cast<float>(py)), z(static_cast<float>(pz)) {}

	vec3 operator+(const vec3& rhs) const { return vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
	vec3 operator-(const vec3& rhs) const { return vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
	vec3 operator*(const vec3& rhs) const { return vec3(x * rhs.x, y * rhs.y, z * rhs.z); }
	vec3 operator/(const vec3& rhs) const { return vec3(x / rhs.x, y / rhs.y, z / rhs.z); }
	vec3 operator%(const vec3& rhs) const { return vec3(std::fmod(x, rhs.x), std::fmod(y, rhs.y), std::fmod(z, rhs.z)); }
	vec3& operator+=(const vec3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
	vec3& operator-=(const vec3& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
	vec3& operator*=(const vec3& rhs) { x *= rhs.x; y *= rhs.y; z *= rhs.z; return *this; }
	vec3& operator/=(const vec3& rhs) { x /= rhs.x; y /= rhs.y; z /= rhs.z; return *this; }
	bool operator==(const vec3& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
	bool operator!=(const vec3& rhs) const { return x != rhs.x || y != rhs.y || z != rhs.z; }

	float dot(const vec3& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z; }
	float length() const { return std::sqrt(x * x + y * y + z * z); }
	float distance(const vec3& rhs) const { return (rhs - *this).length(); }
	vec3 cross(const vec3& rhs) const { return vec3(y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x); }
	vec3 normalize() const { const float len = length(); return vec3(x / len, y / len, z / len); }
	vec3 rotate(float angle, const vec3& axis) const;
	vec3 lerp(const vec3& rhs, float t) const { return vec3(x + t * (rhs.x - x), y + t * (rhs.y - y), z + t * (rhs.z - z)); }
	vec3 reflect(const vec3& normal) const;
	vec3 project(const vec3& rhs) const;
	vec3 perpendicular() const { return vec3(-y, x, z); }
	vec3 perpendicular(const vec3& rhs) const { return vec3(-rhs.y, rhs.x, rhs.z); }
};

struct vec4
{
	float x;
	float y;
	float z;
	float w;

	vec4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
	vec4(const POINT& point) : x(static_cast<float>(point.x)), y(static_cast<float>(point.y)), z(0.f), w(0.f) {}
	vec4(const vec2& other) : x(other.x), y(other.y), z(0.f), w(0.f) {}
	vec4(const vec2& xy, const vec2& zw) : x(xy.x), y(xy.y), z(zw.x), w(zw.y) {}
	vec4(const vec3& other) : x(other.x), y(other.y), z(other.z), w(0.f) {}
	vec4(const vec4& other) : x(other.x), y(other.y), z(other.z), w(other.w) {}

	template <typename t0, typename = enable_primitive<t0>>
	vec4(t0 value) : x(static_cast<float>(value)), y(static_cast<float>(value)), z(static_cast<float>(value)), w(static_cast<float>(value)) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>>
	vec4(t0 px, t1 py) : x(static_cast<float>(px)), y(static_cast<float>(py)), z(0.f), w(0.f) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>, typename t2, typename = enable_primitive<t2>>
	vec4(t0 px, t1 py, t2 pz) : x(static_cast<float>(px)), y(static_cast<float>(py)), z(static_cast<float>(pz)), w(0.f) {}

	template <typename t0, typename = enable_primitive<t0>, typename t1, typename = enable_primitive<t1>, typename t2, typename = enable_primitive<t2>, typename t3, typename = enable_primitive<t3>>
	vec4(t0 px, t1 py, t2 pz, t3 pw) : x(static_cast<float>(px)), y(static_cast<float>(py)), z(static_cast<float>(pz)), w(static_cast<float>(pw)) {}

	vec4 operator+(const vec4& rhs) const { return vec4(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w); }
	vec4 operator-(const vec4& rhs) const { return vec4(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w); }
	vec4 operator*(const vec4& rhs) const { return vec4(x * rhs.x, y * rhs.y, z * rhs.z, w * rhs.w); }
	vec4 operator/(const vec4& rhs) const { return vec4(x / rhs.x, y / rhs.y, z / rhs.z, w / rhs.w); }
	vec4 operator%(const vec4& rhs) const { return vec4(std::fmod(x, rhs.x), std::fmod(y, rhs.y), std::fmod(z, rhs.z), std::fmod(w, rhs.w)); }
	vec4& operator+=(const vec4& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; w += rhs.w; return *this; }
	vec4& operator-=(const vec4& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; w -= rhs.w; return *this; }
	vec4& operator*=(const vec4& rhs) { x *= rhs.x; y *= rhs.y; z *= rhs.z; w *= rhs.w; return *this; }
	vec4& operator/=(const vec4& rhs) { x /= rhs.x; y /= rhs.y; z /= rhs.z; w /= rhs.w; return *this; }
	bool operator==(const vec4& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
	bool operator!=(const vec4& rhs) const { return x != rhs.x || y != rhs.y || z != rhs.z || w != rhs.w; }

	float dot(const vec4& rhs) const { return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w; }
	float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
	vec4 normalize() const { const float len = length(); return vec4(x / len, y / len, z / len, w / len); }
	vec4 cross(const vec4& rhs) const { return vec4(y * rhs.z - z * rhs.y, z * rhs.x - x * rhs.z, x * rhs.y - y * rhs.x, 0.f); }
};

enum class text_align
{
	left,
	center,
	right
};

enum class font_id : int
{
	segoe_ui,
	segoe_ui_semibold,
	tahoma,
	consolas,
	arial,
	roboto,
	count
};

struct rect
{
	float left;
	float top;
	float right;
	float bottom;

	rect() : left(0.f), top(0.f), right(0.f), bottom(0.f) {}
	rect(float all) : left(all), top(all), right(all), bottom(all) {}
	rect(float left, float top, float right, float bottom) : left(left), top(top), right(right), bottom(bottom) {}
	rect(vec2 point, vec2 size) : left(point.x), top(point.y), right(point.x + size.x), bottom(point.y + size.y) {}

	rect operator+(vec2 point) const { return rect(left + point.x, top + point.y, right + point.x, bottom + point.y); }
	rect operator-(vec2 point) const { return rect(left - point.x, top - point.y, right - point.x, bottom - point.y); }

	vec2 point() const { return vec2(left, top); }
	vec2 size() const { return vec2(right - left, bottom - top); }
	vec2 center() const { return vec2((left + right) * 0.5f, (top + bottom) * 0.5f); }
	float width() const { return right - left; }
	float height() const { return bottom - top; }
};

struct color
{
	float r;
	float g;
	float b;
	float a;

	color() : r(0.f), g(0.f), b(0.f), a(0.f) {}
	color(float r, float g, float b) : r(r), g(g), b(b), a(1.f) {}
	color(float r, float g, float b, float alpha) : r(r), g(g), b(b), a(alpha) {}

	static color hsv(float hue, float saturation, float value, float alpha = 1.f);
	static color hsl(float hue, float saturation, float lum, float alpha = 1.f);
	static color lerp(color from, color to, float t);
};

using texture_id = uint32_t;

struct renderer
{
	static bool create(HWND window);
	static void destroy();
	static bool set_font(font_id id);
	static bool select_font(const wchar_t* name);
	static bool import_font(const wchar_t* name, const wchar_t* filename);
	static void next_font();
	static font_id font();
	static const wchar_t* font_name(font_id id);
	static void begin_scene();
	static void end_scene(bool vsync = false);

	static texture_id create_texture(const wchar_t* filename);
	static texture_id create_texture(uint32_t width, uint32_t height, const void* pixels, uint32_t stride);
	static texture_id create_texture_dynamic(uint32_t width, uint32_t height);
	static bool update_texture(texture_id texture, const void* pixels, uint32_t stride);
	static void destroy_texture(texture_id texture);
	static vec2 texture_size(texture_id texture);
	static void draw_image(texture_id texture, rect bounds, color tint);
	static void set_texture(texture_id texture);

	static uint32_t push_vertex(vec2 point, vec2 uv, color tint, vec4 edge);
	static void push_index(uint32_t index);
	static void push_quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

	static void draw_line(vec2 from, vec2 to, float thickness, color value);
	static void draw_rounded_line(vec2 from, vec2 to, float thickness, color value);
	static void draw_polyline(const std::vector<vec2>& points, float thickness, color value);
	static void draw_bezier(vec2 p0, vec2 p1, vec2 p2, float thickness, color value);
	static void draw_rounded_bezier(vec2 p0, vec2 p1, vec2 p2, float thickness, color value);
	static void draw_circle(vec2 point, float radius, float thickness, color value);
	static void draw_filled_circle(vec2 point, float radius, color value);
	static void draw_filled_triangle(vec2 p0, vec2 p1, vec2 p2, color value);
	static void draw_rect(rect bounds, float thickness, color value);
	static void draw_filled_rect(rect bounds, color value);
	static void draw_rounded_rect(rect bounds, float radius, float thickness, color value);
	static void draw_filled_rounded_rect(rect bounds, float radius, color value);
	static void draw_rounded_gradient(rect bounds, float radius, color top, color bottom);
	static void draw_rounded_glow(rect bounds, float radius, color tint, float spread);
	static void draw_rounded_border(rect bounds, float radius, float thickness, color top, color bottom, float glow);
	static void draw_flow_ribbon(const vec2* points, const float* widths, const color* tints, int count);
	static void draw_shadow(rect bounds, float blur_radius, color value);

	static void push_scale(float scale_x, float scale_y, vec2 center);
	static void push_scale(float scale_x, float scale_y, vec2 center, float rotation_degrees);
	static void pop_scale();
	static void push_clip(rect bounds);
	static void pop_clip();

	static void draw_text(vec2 point, float size, text_align align, const wchar_t* value, color tint);
	static void draw_text_ex(vec2 point, float size, text_align align, const wchar_t* value, color tint, color outline, float outline_width);
	static void draw_text_f(vec2 point, float size, text_align align, color tint, const wchar_t* format, ...);
	static void draw_rich_text(vec2 point, float size, text_align align, color tint, const wchar_t* format, ...);
	static rect measure_text(const wchar_t* value, float size);
	static rect measure_text_f(float size, const wchar_t* format, ...);
	static rect measure_rich_text(float size, const wchar_t* format, ...);
	static rect text_bounds(vec2 point, float size, text_align align, const wchar_t* value);
	static rect text_bounds_f(vec2 point, float size, text_align align, const wchar_t* format, ...);
	static rect rich_text_bounds(vec2 point, float size, text_align align, const wchar_t* format, ...);
	static float line_height(float size);
	static void dbg(vec2 point = vec2(24.f, 24.f));

	static vec2 display_size();
	static float delta_time();
	static uint64_t frame();
};

using frame_hook = bool (*)();

struct resources
{
	static texture_id import_texture(const wchar_t* name, const wchar_t* filename);
	static texture_id import_texture(const wchar_t* name, uint32_t width, uint32_t height, const void* pixels, uint32_t stride);
	static texture_id texture(const wchar_t* name);
};

struct app
{
	static int run(HWND window, frame_hook frame);
};

inline vec2::vec2(const vec3& other) : x(other.x), y(other.y) {}
inline vec2::vec2(const vec4& other) : x(other.x), y(other.y) {}
inline vec3::vec3(const vec4& other) : x(other.x), y(other.y), z(other.z) {}
