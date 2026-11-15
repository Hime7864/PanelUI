#pragma once

#include "graphics.hpp"

// Built-in icon set. Icons are drawn from vector primitives rather than glyphs or textures: they stay
// sharp at any size and DPI, need no atlas, and take the theme tint directly.
enum class UiIcon : int
{
	None,
	Close,
	Plus,
	Minus,
	Check,
	ChevronDown,
	ChevronUp,
	ChevronLeft,
	ChevronRight,
	Search,
	Settings,
	Folder,
	File,
	Image,
	Play,
	Pause,
	Stop,
	Refresh,
	Trash,
	Info,
	Warning,
	Error,
	Star,
	Lock,
	Menu,
	Grid,
	List,
	WindowMaximize,
	WindowRestore,
	Count
};

// Draws `Icon` centred inside `Bounds`, inked with `Tint`, keeping a margin so the stroke stays inside.
// `StrokeWidth` of 0 derives a width from the icon size.
void DrawUiIcon(UiIcon Icon, rect Bounds, color Tint, float StrokeWidth = 0.f);

// Stable name for inspectors and the demo (`L"none"` for None/out of range).
const wchar_t* UiIconName(UiIcon Icon);
