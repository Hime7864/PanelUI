#include "framework.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
	constexpr float CollapsibleHeader = 30.f;
	constexpr float CollapsibleIndent = 10.f;
	constexpr float ListRow = 26.f;
	constexpr float ListPad = 4.f;
	constexpr float TableHeader = 26.f;
	constexpr float DropdownRow = 28.f;
	constexpr float DropdownPad = 4.f;
	constexpr int DropdownMaxRows = 8;
	constexpr float CheckSize = 16.f;
	constexpr float ToggleWidth = 32.f;
	constexpr float ToggleHeight = 18.f;
	constexpr float IndicatorGap = 8.f;
	// Stroke width for outlines that are entirely curved (radio ring, toggle knob). A 1 px stroke on a
	// circle lands on half-covered pixels; multisampling splits those into two faint columns per side
	// ("ghosting"). 1.5 px keeps one column fully covered, so the ring reads as one line. Straight-edged
	// outlines (checkbox box, toggle track) stay at 1 px: their edges sit on pixel boundaries and are
	// already crisp, and a thicker stroke would just look heavy.
	constexpr float RingStroke = 1.5f;
	constexpr int InputMaxLength = 256;

	// A circular stroke reads as one line only when its band falls between pixel boundaries, which needs
	// the centre on a pixel centre. With the centre on a pixel edge multisampling splits the ring into two
	// half-covered columns per side ("ghosting").
	vec2 PixelCentre(vec2 Point)
	{
		return vec2(std::floor(Point.x) + 0.5f, std::floor(Point.y) + 0.5f);
	}

	bool PointInRect(rect Bounds, vec2 Point)
	{
		return Point.x >= Bounds.left && Point.x < Bounds.right && Point.y >= Bounds.top && Point.y < Bounds.bottom;
	}

	bool HasArea(rect Bounds)
	{
		return Bounds.width() > 0.f && Bounds.height() > 0.f;
	}

	bool IntersectsRect(rect A, rect B)
	{
		return A.left < B.right && B.left < A.right && A.top < B.bottom && B.top < A.bottom;
	}

	rect IntersectionRect(rect A, rect B)
	{
		const float Left = std::max(A.left, B.left);
		const float Top = std::max(A.top, B.top);
		const float Right = std::min(A.right, B.right);
		const float Bottom = std::min(A.bottom, B.bottom);

		if (Right <= Left || Bottom <= Top)
			return rect();

		return rect(Left, Top, Right, Bottom);
	}

	// Text is culled explicitly instead of relying on the scissor alone: the renderer emits glyphs in a
	// separate pass, so a clip is not enough to keep scrolled rows from painting outside their viewport.
	bool TextVisible(rect Area, rect Visible)
	{
		return IntersectsRect(Area, Visible) && HasArea(Area);
	}

	color WithAlpha(color Value, float Alpha)
	{
		Value.a = std::clamp(Value.a * Alpha, 0.f, 1.f);
		return Value;
	}

	color Opaque(color Value)
	{
		Value.a = 1.f;
		return Value;
	}

	// Outline tone for unchecked indicators (checkbox, radio, toggle). Theme.Border is tuned for panel
	// separators and almost vanishes against the workspace fill an indicator sits on.
	color ControlEdge(const UiTheme& Theme)
	{
		return UiMix(Theme.Border, Theme.Muted, 0.35f);
	}

	bool IsInteractive(UiNodeKind Kind)
	{
		switch (Kind)
		{
		case UiNodeKind::Button:
		case UiNodeKind::Slider:
		case UiNodeKind::Input:
		case UiNodeKind::Dropdown:
		case UiNodeKind::Checkbox:
		case UiNodeKind::Toggle:
		case UiNodeKind::Radio:
		case UiNodeKind::ListBox:
		case UiNodeKind::Collapsible:
		case UiNodeKind::DragHandle:
			return true;
		default:
			return false;
		}
	}

	bool IsPressable(UiNodeKind Kind)
	{
		return Kind == UiNodeKind::Button || Kind == UiNodeKind::Checkbox || Kind == UiNodeKind::Toggle ||
			Kind == UiNodeKind::Radio || Kind == UiNodeKind::Collapsible;
	}

	bool IsScrollable(const UiNode& Item)
	{
		return Item.Kind == UiNodeKind::ListBox || Item.Style.Scroll || Item.Style.ScrollX;
	}

	void CopyTextToClipboard(const std::wstring& Text)
	{
		if (!OpenClipboard(nullptr))
			return;

		EmptyClipboard();

		const size_t Bytes = (Text.size() + 1) * sizeof(wchar_t);
		HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes);

		if (Memory)
		{
			void* Destination = GlobalLock(Memory);

			if (Destination)
			{
				memcpy(Destination, Text.c_str(), Bytes);
				GlobalUnlock(Memory);
				SetClipboardData(CF_UNICODETEXT, Memory);
				Memory = nullptr;
			}
		}

		if (Memory)
			GlobalFree(Memory);

		CloseClipboard();
	}

	bool MenuHasSeparator(const UiNode& Item, int Index)
	{
		return std::find(Item.MenuSeparators.begin(), Item.MenuSeparators.end(), Index) != Item.MenuSeparators.end();
	}

	bool MenuItemDisabled(const UiNode& Item, int Index)
	{
		return std::find(Item.MenuDisabled.begin(), Item.MenuDisabled.end(), Index) != Item.MenuDisabled.end();
	}

	// The title row is optional; items start below it (or at the top padding when there is no title).
	float MenuItemsOffset(const UiNode& Item, float ScaleValue)
	{
		return (Item.MenuTitle.empty() ? 0.f : 24.f * ScaleValue) + 4.f * ScaleValue;
	}

	float MenuItemTop(const UiNode& Item, int Index, float ScaleValue, float ItemsTop)
	{
		float Top = ItemsTop;

		for (int Current = 0; Current < Index; Current++)
		{
			Top += 20.f * ScaleValue;

			if (MenuHasSeparator(Item, Current))
				Top += 7.f * ScaleValue;
		}

		return Top;
	}

	// A check mark or icon reserves the left column; without either the label starts near the edge.
	bool MenuHasDecoration(const UiNode& Item)
	{
		for (int Check : Item.MenuChecks)
		{
			if (Check)
				return true;
		}

		for (UiIcon Icon : Item.MenuIcons)
		{
			if (Icon != UiIcon::None)
				return true;
		}

		return false;
	}

	float MenuDecorationWidth(const UiNode& Item, float ScaleValue)
	{
		return MenuHasDecoration(Item) ? 18.f * ScaleValue : 0.f;
	}

	// One geometry for drawing, selection highlight and hit testing: every column is a whole number of
	// monospace cells, so highlights land exactly on the glyphs they cover.
	struct HexGeometry
	{
		float CharWidth = 0.f;
		float AddressX = 0.f;
		float ValuesX = 0.f;
		float CellWidth = 0.f;
		float DigitsWidth = 0.f;
		float TextX = 0.f;
		float Right = 0.f;
		int Unit = 1;
		int Columns = 16;
		int Digits = 2;
		int TextChars = 16;
	};

	int HexUnitBytes(const UiNode& Item)
	{
		return Item.HexUnit >= 8 ? 8 : Item.HexUnit >= 4 ? 4 : Item.HexUnit >= 2 ? 2 : 1;
	}

	int HexCellDigits(const UiNode& Item)
	{
		const int Unit = HexUnitBytes(Item);

		if (Item.HexKind != 1)
			return Unit * 2;

		// Widest decimal rendering per size: "255"/"-128", "65535"/"-32768", 10/11 and 20/20 digits.
		switch (Unit)
		{
		case 1:
			return Item.HexSigned ? 4 : 3;
		case 2:
			return Item.HexSigned ? 6 : 5;
		case 4:
			return Item.HexSigned ? 11 : 10;
		default:
			return 20;
		}
	}

	float HexMeasureCharWidth(const UiNode& Item)
	{
		const font_id Previous = renderer::font();
		const bool Overridden = !Item.FontName.empty() && renderer::select_font(Item.FontName.c_str());
		// Ink extents stop short of the last advance; the trailing sentinel makes this the true 10-glyph pen advance.
		const float Width = (renderer::measure_text(L"0000000000|", Item.Style.FontSize).width() -
			renderer::measure_text(L"|", Item.Style.FontSize).width()) / 10.f;

		if (Overridden)
			renderer::set_font(Previous);

		return Width > 0.f ? Width : Item.Style.FontSize * 0.55f;
	}

	HexGeometry HexLayout(const UiNode& Item, float Left, float ScaleValue)
	{
		HexGeometry Result;
		Result.CharWidth = (Item.HexCharWidth > 0.f ? Item.HexCharWidth : Item.Style.FontSize * 0.55f) * ScaleValue;
		Result.Unit = HexUnitBytes(Item);
		Result.Columns = 16 / Result.Unit;
		Result.Digits = HexCellDigits(Item);
		Result.TextChars = Item.HexUtf16 ? 8 : 16;

		const int AddressChars = Item.HexAddressChars > 0 ? Item.HexAddressChars : 8;
		Result.AddressX = Left + 6.f * ScaleValue;
		Result.ValuesX = Result.AddressX + static_cast<float>(AddressChars + 2) * Result.CharWidth;
		Result.DigitsWidth = static_cast<float>(Result.Digits) * Result.CharWidth;
		Result.CellWidth = static_cast<float>(Result.Digits + 1) * Result.CharWidth;
		Result.TextX = Result.ValuesX + static_cast<float>(Result.Columns) * Result.CellWidth + Result.CharWidth;
		Result.Right = Result.TextX + static_cast<float>(Result.TextChars) * Result.CharWidth;
		return Result;
	}

	uint64_t HexRowCount(const UiNode& Item)
	{
		return (Item.HexSpan + 15) / 16;
	}

	// The dump scrolls in whole rows: glyphs are not clipped by the node, so a half-scrolled top row would
	// spill over the content above it. Rounding up keeps the last row reachable at ScrollMax.
	uint64_t HexFirstRow(const UiNode& Item, float RowPx)
	{
		if (RowPx <= 0.f || Item.Scroll <= 0.f)
			return 0;

		return static_cast<uint64_t>(std::ceil(Item.Scroll / RowPx - 0.01f));
	}

	// One value cell as text: little-endian hex digits, or decimal right-aligned to Digits when Pad.
	std::wstring HexFormatCell(const UiNode& Item, uint64_t Raw, int Unit, int Digits, bool Pad)
	{
		wchar_t Cell[32];
		const int Width = Pad ? Digits : 0;

		if (Item.HexKind != 1)
		{
			swprintf_s(Cell, L"%0*llX", Unit * 2, static_cast<unsigned long long>(Raw));
		}
		else if (Item.HexSigned)
		{
			const int Bits = Unit * 8;
			const int64_t Number = Bits < 64 && (Raw & (1ull << (Bits - 1))) != 0
				? static_cast<int64_t>(Raw | (~0ull << Bits)) : static_cast<int64_t>(Raw);
			swprintf_s(Cell, L"%*lld", Width, static_cast<long long>(Number));
		}
		else
		{
			swprintf_s(Cell, L"%*llu", Width, static_cast<unsigned long long>(Raw));
		}

		return Cell;
	}

	// Text-column glyph for one decoded unit. Only printable ASCII (and Latin-1 for UTF-16) is shown so
	// every character keeps the monospace grid.
	wchar_t HexTextChar(unsigned Value, bool Utf16)
	{
		if (Value >= 0x20 && Value < 0x7F)
			return static_cast<wchar_t>(Value);

		if (Utf16 && Value >= 0xA0 && Value <= 0xFF)
			return static_cast<wchar_t>(Value);

		return L'.';
	}

	// Moves the inline edit by whole cells (values) or characters (text); pending digits are dropped.
	bool HexMoveEdit(UiNode& Item, int64_t Steps)
	{
		const int64_t Width = Item.HexEditColumn == 1 ? HexUnitBytes(Item) : Item.HexUtf16 ? 2 : 1;
		const int64_t Target = static_cast<int64_t>(Item.HexEditAddress) + Steps * Width;

		if (Target < 0 || static_cast<uint64_t>(Target) >= Item.HexSpan)
			return false;

		Item.HexEditAddress = static_cast<uint64_t>(Target);
		Item.HexEditText.clear();
		Item.HexSelAnchor = Item.HexEditAddress;
		Item.HexSelStart = Item.HexEditAddress;
		Item.HexSelEnd = std::min(Item.HexEditAddress + static_cast<uint64_t>(Width), Item.HexSpan);
		Item.HexHasSelection = true;
		return true;
	}

	// Scrolls whole rows so the cell being edited stays inside the visible part of the dump.
	void HexReveal(UiNode& Item, float ScaleValue)
	{
		const float RowPx = Item.RowHeight * ScaleValue;

		if (RowPx <= 0.f)
			return;

		const uint64_t Row = Item.HexEditAddress / 16;
		const uint64_t First = HexFirstRow(Item, RowPx);
		const uint64_t Visible = std::max<uint64_t>(1, static_cast<uint64_t>(Item.Bounds.height() / RowPx));

		if (Row < First)
			Item.Scroll = static_cast<float>(Row) * RowPx;
		else if (Row >= First + Visible)
			Item.Scroll = static_cast<float>(Row - Visible + 1) * RowPx;
	}

	void DrawDashedRect(rect Bounds, float Scale, color Tint)
	{
		// A hairline of short, translucent dashes: the outline marks an editable cell without competing
		// with the text inside it.
		const float Hair = std::max(1.f, std::round(Scale * 0.5f));
		const float Dash = 3.f * Scale;
		const float Gap = 2.f * Scale;
		const color Stroke = WithAlpha(Tint, 0.5f);

		for (float X = Bounds.left; X < Bounds.right; X += Dash + Gap)
		{
			const float Right = std::min(Bounds.right, X + Dash);
			renderer::draw_filled_rect(rect(X, Bounds.top, Right, Bounds.top + Hair), Stroke);
			renderer::draw_filled_rect(rect(X, Bounds.bottom - Hair, Right, Bounds.bottom), Stroke);
		}

		for (float Y = Bounds.top; Y < Bounds.bottom; Y += Dash + Gap)
		{
			const float Bottom = std::min(Bounds.bottom, Y + Dash);
			renderer::draw_filled_rect(rect(Bounds.left, Y, Bounds.left + Hair, Bottom), Stroke);
			renderer::draw_filled_rect(rect(Bounds.right - Hair, Y, Bounds.right, Bottom), Stroke);
		}
	}

	float IndicatorWidth(UiNodeKind Kind)
	{
		return Kind == UiNodeKind::Toggle ? ToggleWidth : CheckSize;
	}

	// Copies the callback before invoking it: a handler may add nodes or call Init(), which reallocates
	// the node vector that owns the original std::function.
	template <typename Callback, typename... Args>
	void Invoke(const Callback& Function, Args... Arguments)
	{
		if (!Function)
			return;

		Callback Copy = Function;
		Copy(Arguments...);
	}

	double SnapValue(const UiNode& Item, double Value)
	{
		const double Low = std::min(Item.Min, Item.Max);
		const double High = std::max(Item.Min, Item.Max);
		Value = std::clamp(Value, Low, High);

		if (Item.Step > 0.0)
			Value = std::clamp(Item.Min + std::round((Value - Item.Min) / Item.Step) * Item.Step, Low, High);

		return Value;
	}

	int ValueDecimals(const UiNode& Item)
	{
		if (Item.Decimals >= 0)
			return Item.Decimals;

		if (Item.Step > 0.0)
			return Item.Step >= 1.0 ? 0 : (Item.Step >= 0.1 ? 1 : 2);

		const double Range = std::fabs(Item.Max - Item.Min);
		return Range >= 10.0 ? 0 : (Range >= 1.0 ? 1 : 2);
	}

	float ValueRatio(const UiNode& Item)
	{
		if (Item.Max == Item.Min)
			return 0.f;

		return std::clamp(static_cast<float>((Item.Value - Item.Min) / (Item.Max - Item.Min)), 0.f, 1.f);
	}

	rect FitRect(rect Bounds, vec2 Size)
	{
		if (Size.x <= 0.f || Size.y <= 0.f)
			return Bounds;

		const float Factor = std::min(Bounds.width() / Size.x, Bounds.height() / Size.y);
		const vec2 Fitted(Size.x * Factor, Size.y * Factor);
		const vec2 Center = Bounds.center();
		return rect(Center.x - Fitted.x * 0.5f, Center.y - Fitted.y * 0.5f, Center.x + Fitted.x * 0.5f, Center.y + Fitted.y * 0.5f);
	}

	// Fills the bounds with the image, growing past them where the aspect does not match (the caller clips).
	rect CoverRect(rect Bounds, vec2 Size)
	{
		if (Size.x <= 0.f || Size.y <= 0.f)
			return Bounds;

		const float Factor = std::max(Bounds.width() / Size.x, Bounds.height() / Size.y);
		const vec2 Fitted(Size.x * Factor, Size.y * Factor);
		const vec2 Center = Bounds.center();
		return rect(Center.x - Fitted.x * 0.5f, Center.y - Fitted.y * 0.5f, Center.x + Fitted.x * 0.5f, Center.y + Fitted.y * 0.5f);
	}

	rect FitImage(rect Bounds, vec2 Size, UiImageFit Fit)
	{
		if (Fit == UiImageFit::Stretch || Size.x <= 0.f || Size.y <= 0.f)
			return Bounds;
		return Fit == UiImageFit::Cover ? CoverRect(Bounds, Size) : FitRect(Bounds, Size);
	}

	// Icon size inside a button, and the gap between icon and label.
	constexpr float ButtonIconGap = 7.f;

	float ButtonIconSize(const UiNode& Item, float ScaleValue)
	{
		return std::clamp(Item.Bounds.height() - 14.f * ScaleValue, 12.f * ScaleValue, 18.f * ScaleValue);
	}

	// Left edge of a button's content block (icon + gap + label), centred in the button.
	float ButtonContentLeft(const UiNode& Item, float ScaleValue)
	{
		const float IconWidth = Item.IconGlyph != UiIcon::None ? ButtonIconSize(Item, ScaleValue) + ButtonIconGap * ScaleValue : 0.f;
		const float TextWidth = Item.Text.empty() ? 0.f : renderer::measure_text(Item.Text.c_str(), Item.Style.FontSize * ScaleValue).width();
		return Item.Bounds.center().x - (IconWidth + TextWidth) * 0.5f;
	}
}

UiPaint UiPaint::RolePaint(UiColorRole Role)
{
	UiPaint Paint;
	Paint.Role = Role;
	Paint.Set = true;
	return Paint;
}

UiPaint UiPaint::Solid(color Value)
{
	UiPaint Paint;
	Paint.Role = UiColorRole::Custom;
	Paint.Value = Value;
	Paint.Set = true;
	return Paint;
}

color UiPaint::Resolve(const UiTheme& Theme) const
{
	if (!Set)
		return color(0.f, 0.f, 0.f, 0.f);

	switch (Role)
	{
	case UiColorRole::Window:
		return Theme.Window;
	case UiColorRole::Workspace:
		return Theme.Workspace;
	case UiColorRole::Panel:
		return Theme.Panel;
	case UiColorRole::Header:
		return Theme.Header;
	case UiColorRole::TabHover:
		return Theme.TabHover;
	case UiColorRole::TabActive:
		return Theme.TabActive;
	case UiColorRole::Border:
		return Theme.Border;
	case UiColorRole::Text:
		return Theme.Text;
	case UiColorRole::Muted:
		return Theme.Muted;
	case UiColorRole::Accent:
		return Theme.Accent;
	case UiColorRole::AccentSoft:
		return Theme.AccentSoft;
	case UiColorRole::Danger:
		return Theme.Danger;
	case UiColorRole::Menu:
		return Theme.Menu;
	case UiColorRole::MenuHover:
		return Theme.MenuHover;
	case UiColorRole::MenuBorder:
		return Theme.MenuBorder;
	default:
		return Value;
	}
}

int PanelContent::Init(UiFlow Flow, float Padding, float Gap)
{
	Nodes.clear();
	RootNode = -1;
	Focused = -1;
	Pressed = -1;
	RadioGroups = 0;

	UiStyle Style;
	Style.Flow = Flow;
	Style.Padding = Padding;
	Style.Gap = Gap;
	Style.Width = UiSize::Fill;
	Style.Height = UiSize::Fill;
	RootNode = Add(UiNodeKind::Container, -1, Style, L"");
	return RootNode;
}

int PanelContent::Add(UiNodeKind Kind, int Parent, const UiStyle& Style, const std::wstring& Text)
{
	UiNode Item;
	Item.Kind = Kind;
	Item.Style = Style;
	Item.Text = Text;
	Item.Parent = Parent;

	const int Id = static_cast<int>(Nodes.size());
	Nodes.push_back(std::move(Item));

	if (Valid(Parent))
		Nodes[Parent].Children.push_back(Id);

	return Id;
}

int PanelContent::Find(const std::wstring& Id) const
{
	if (Id.empty())
		return -1;

	for (int Node = 0; Node < static_cast<int>(Nodes.size()); Node++)
	{
		if (Nodes[Node].Id == Id)
			return Node;
	}

	return -1;
}

int PanelContent::Container(int Parent, UiFlow Flow, float Padding, float Gap)
{
	UiStyle Style;
	Style.Flow = Flow;
	Style.Padding = Padding;
	Style.Gap = Gap;
	return Add(UiNodeKind::Container, Parent, Style, L"");
}

int PanelContent::Text(int Parent, const std::wstring& Value, float Size)
{
	UiStyle Style;
	Style.FontSize = Size;
	return Add(UiNodeKind::Text, Parent, Style, Value);
}

int PanelContent::Paragraph(int Parent, const std::wstring& Value, float Size)
{
	UiStyle Style;
	Style.FontSize = Size;
	Style.Wrap = true;
	return Add(UiNodeKind::Text, Parent, Style, Value);
}

int PanelContent::Button(int Parent, const std::wstring& Value, std::function<void()> Click)
{
	UiStyle Style;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = 32.f;
	Style.Align = UiAlign::Start;
	Style.Padding = 6.f;
	Style.Radius = 5.f;
	const int Id = Add(UiNodeKind::Button, Parent, Style, Value);
	Nodes[Id].OnClick = std::move(Click);
	return Id;
}

int PanelContent::Button(int Parent, UiIcon Glyph, const std::wstring& Value, std::function<void()> Click)
{
	const int Id = Button(Parent, Value, std::move(Click));
	Nodes[Id].IconGlyph = Glyph;
	return Id;
}

int PanelContent::Slider(int Parent, const std::wstring& Label, double Min, double Max, double Value, std::function<void(double)> Change, double Step)
{
	UiStyle Style;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = 42.f;
	Style.Width = UiSize::Fill;
	Style.Padding = 4.f;
	const int Id = Add(UiNodeKind::Slider, Parent, Style, Label);
	UiNode& Item = Nodes[Id];
	Item.Min = Min;
	Item.Max = Max;
	Item.Step = std::max(0.0, Step);
	Item.Value = SnapValue(Item, Value);
	Item.OnChange = std::move(Change);
	return Id;
}

int PanelContent::Input(int Parent, const std::wstring& Value, std::function<void(const std::wstring&)> Submit, const std::wstring& Placeholder)
{
	UiStyle Style;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = 32.f;
	Style.Width = UiSize::Fill;
	Style.Padding = 8.f;
	Style.Radius = 4.f;
	const int Id = Add(UiNodeKind::Input, Parent, Style, Value.substr(0, InputMaxLength));
	UiNode& Item = Nodes[Id];
	Item.Caret = static_cast<int>(Item.Text.size());
	Item.Placeholder = Placeholder;
	Item.OnSubmit = std::move(Submit);
	return Id;
}

int PanelContent::Dropdown(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select)
{
	UiStyle Style;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = 32.f;
	Style.Width = UiSize::Fill;
	Style.Padding = 8.f;
	Style.Radius = 4.f;
	const int Id = Add(UiNodeKind::Dropdown, Parent, Style, L"");
	UiNode& Item = Nodes[Id];
	Item.Options = Options;
	Item.Selected = Options.empty() ? 0 : std::clamp(Selected, 0, static_cast<int>(Options.size()) - 1);
	Item.OnSelect = std::move(Select);
	return Id;
}

int PanelContent::Checkbox(int Parent, const std::wstring& Label, bool Checked, std::function<void(bool)> Changed)
{
	UiStyle Style;
	Style.Align = UiAlign::Start;
	Style.Padding = 3.f;
	const int Id = Add(UiNodeKind::Checkbox, Parent, Style, Label);
	Nodes[Id].Checked = Checked;
	Nodes[Id].OnToggle = std::move(Changed);
	return Id;
}

int PanelContent::Toggle(int Parent, const std::wstring& Label, bool On, std::function<void(bool)> Changed)
{
	UiStyle Style;
	Style.Align = UiAlign::Start;
	Style.Padding = 3.f;
	const int Id = Add(UiNodeKind::Toggle, Parent, Style, Label);
	Nodes[Id].Checked = On;
	Nodes[Id].OnToggle = std::move(Changed);
	return Id;
}

int PanelContent::Radio(int Parent, const std::wstring& Label, int Group, bool Checked, std::function<void(bool)> Changed)
{
	UiStyle Style;
	Style.Align = UiAlign::Start;
	Style.Padding = 3.f;
	const int Id = Add(UiNodeKind::Radio, Parent, Style, Label);
	Nodes[Id].RadioGroup = Group;
	Nodes[Id].OnToggle = std::move(Changed);
	SetChecked(Id, Checked);
	return Id;
}

int PanelContent::RadioGroup(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select)
{
	const int Box = Container(Parent, UiFlow::Vertical, 0.f, 2.f);

	// Generated groups are negative so they never collide with caller-chosen group ids.
	const int Group = -(++RadioGroups);
	auto Shared = std::make_shared<std::function<void(int)>>(std::move(Select));

	for (int Index = 0; Index < static_cast<int>(Options.size()); Index++)
	{
		Radio(Box, Options[Index], Group, Index == Selected, [Shared, Index](bool On)
		{
			if (On && *Shared)
				(*Shared)(Index);
		});
	}

	return Box;
}

int PanelContent::Progress(int Parent, const std::wstring& Label, double Value, double Min, double Max)
{
	UiStyle Style;
	Style.Width = UiSize::Fill;
	Style.Padding = 4.f;
	const int Id = Add(UiNodeKind::Progress, Parent, Style, Label);
	UiNode& Item = Nodes[Id];
	Item.Min = Min;
	Item.Max = Max;
	Item.Value = SnapValue(Item, Value);
	return Id;
}

int PanelContent::Separator(int Parent)
{
	return Add(UiNodeKind::Separator, Parent, UiStyle(), L"");
}

int PanelContent::Image(int Parent, texture_id Texture, float Width, float Height, UiImageFit Fit)
{
	UiStyle Style;
	Style.Align = UiAlign::Start;
	Style.Radius = 6.f;

	if (Width > 0.f)
	{
		Style.Width = UiSize::Fixed;
		Style.WidthValue = Width;
	}

	if (Height > 0.f)
	{
		Style.Height = UiSize::Fixed;
		Style.HeightValue = Height;
	}

	const int Id = Add(UiNodeKind::Image, Parent, Style, L"");
	Nodes[Id].Texture = Texture;
	Nodes[Id].Fit = Fit;
	return Id;
}

int PanelContent::ImageFile(int Parent, const wchar_t* Path, float Width, float Height, UiImageFit Fit)
{
	// The named-texture registry doubles as the path cache: one texture per path, released with the
	// renderer (and restored after device loss), so repeated frames do not reload the file.
	texture_id Texture = Path ? resources::texture(Path) : 0;
	if (!Texture && Path && *Path)
		Texture = resources::import_texture(Path, Path);

	return Image(Parent, Texture, Width, Height, Fit);
}

int PanelContent::Icon(int Parent, UiIcon Glyph, float Size)
{
	UiStyle Style;
	Style.Align = UiAlign::Start;
	Style.Padding = 2.f;
	Style.Width = UiSize::Fixed;
	Style.WidthValue = Size;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = Size;

	const int Id = Add(UiNodeKind::Icon, Parent, Style, L"");
	Nodes[Id].IconGlyph = Glyph;
	return Id;
}

int PanelContent::ListBox(int Parent, const std::vector<std::wstring>& Items, int Selected, std::function<void(int)> Select, float Height)
{
	UiStyle Style;
	Style.Width = UiSize::Fill;
	Style.Height = UiSize::Fixed;
	Style.HeightValue = Height;
	Style.Radius = 5.f;
	const int Id = Add(UiNodeKind::ListBox, Parent, Style, L"");
	UiNode& Item = Nodes[Id];
	Item.Options = Items;
	Item.Selected = std::clamp(Selected, -1, static_cast<int>(Items.size()) - 1);
	Item.OnSelect = std::move(Select);
	return Id;
}

int PanelContent::TableList(int Parent, std::vector<std::wstring> Headers, std::vector<std::vector<std::wstring>> Rows,
	std::vector<float> Widths, float ViewHeight, float RowHeightIn)
{
	UiStyle Style;
	Style.Width = UiSize::Fixed;
	// A non-positive height fills the remaining space, so the table can be the panel's whole content.
	Style.Height = ViewHeight > 0.f ? UiSize::Fixed : UiSize::Fill;
	Style.HeightValue = ViewHeight;
	Style.FontSize = 12.f;
	Style.Scroll = true;
	Style.Align = UiAlign::Start;

	float Total = 0.f;
	for (float Width : Widths)
		Total += Width;

	Style.WidthValue = Total;

	const int Id = Add(UiNodeKind::TableList, Parent, Style, L"");
	UiNode& Item = Nodes[Id];
	Item.Columns = std::move(Headers);
	Item.Rows = std::move(Rows);
	Item.ColumnWidths = std::move(Widths);
	Item.RowHeight = RowHeightIn;
	return Id;
}

int PanelContent::HexView(int Parent, const uint8_t* Data, size_t Size, bool ByRva, uint64_t SnapOffset, float ViewHeight, int UnitBytes, int Kind)
{
	UiStyle Style;
	// Width follows the column geometry; a non-positive height fills the remaining panel space.
	Style.Width = UiSize::Auto;
	Style.Height = ViewHeight > 0.f ? UiSize::Fixed : UiSize::Fill;
	Style.HeightValue = ViewHeight;
	Style.FontSize = 10.f;
	Style.Scroll = true;
	Style.Align = UiAlign::Start;

	const int Id = Add(UiNodeKind::HexView, Parent, Style, L"");
	UiNode& Item = Nodes[Id];
	Item.HexData = Data;
	Item.HexSize = Size;
	Item.HexSpan = Size;
	Item.HexByRva = ByRva;
	Item.HexUnit = UnitBytes;
	Item.HexUnit = HexUnitBytes(Item);
	Item.HexKind = Kind == 1 ? 1 : 0;
	Item.RowHeight = 14.f;
	Item.HexSnapRow = static_cast<int>(SnapOffset / 16);
	return Id;
}

int PanelContent::TableListRowAt(const UiNode& Item, vec2 Point) const
{
	if (Item.Kind != UiNodeKind::TableList || Item.RowHeight <= 0.f)
		return -1;

	const float Header = TableHeader * Scale;
	const float RowPx = Item.RowHeight * Scale;

	if (Point.x < Item.Bounds.left || Point.x >= Item.Bounds.right ||
		Point.y < Item.Bounds.top + Header || Point.y >= Item.Bounds.bottom)
		return -1;

	const float Local = Point.y - (Item.Bounds.top + Header) + Item.Scroll;
	const int Row = static_cast<int>(std::floor(Local / RowPx));

	return Row >= 0 && Row < static_cast<int>(Item.Rows.size()) ? Row : -1;
}

int PanelContent::TableListColumnAt(const UiNode& Item, vec2 Point) const
{
	if (Item.Kind != UiNodeKind::TableList || Point.x < Item.Bounds.left || Point.x >= Item.Bounds.right)
		return -1;

	const std::vector<float> Widths = TableWidths(Item);
	float X = Item.Bounds.left;

	for (size_t Column = 0; Column < Widths.size(); Column++)
	{
		X += Widths[Column] * Scale;

		if (Point.x < X)
			return static_cast<int>(Column);
	}

	return -1;
}

std::vector<float> PanelContent::TableWidths(const UiNode& Item) const
{
	std::vector<float> Widths = Item.ColumnWidths;

	if (Widths.empty() || Scale <= 0.f)
		return Widths;

	float Total = 0.f;

	for (float Width : Widths)
		Total += Width * Scale;

	const float Extra = Item.Bounds.width() - Total;

	if (Extra > 0.5f)
	{
		Widths.back() += Extra / Scale;
	}
	else if (Extra < -0.5f && Total > 0.f)
	{
		// Narrower than its columns: share the width so every column stays visible and text is clipped by
		// its cell instead of the last columns falling off the panel.
		const float Factor = Item.Bounds.width() / Total;

		for (float& Width : Widths)
			Width = std::max(24.f, Width * Factor);
	}

	return Widths;
}

bool PanelContent::HexHit(const UiNode& Item, vec2 Point, int& Column, uint64_t& Address, uint64_t& Width, int ForceColumn) const
{
	if (Item.Kind != UiNodeKind::HexView || Item.RowHeight <= 0.f)
		return false;

	const float RowPx = Item.RowHeight * Scale;
	const float RowLimit = Item.Bounds.top + std::floor(Item.Bounds.height() / RowPx) * RowPx;
	const HexGeometry Layout = HexLayout(Item, Item.Bounds.left, Scale);
	const float Half = Layout.CharWidth * 0.5f;
	const float ValuesLeft = Layout.ValuesX - Half;
	const float TextLeft = Layout.TextX - Half;

	if (RowLimit <= Item.Bounds.top)
		return false;

	if (ForceColumn >= 0)
	{
		// Drags keep extending the selection in the column where they started, even past its edges.
		Point.y = std::clamp(Point.y, Item.Bounds.top, RowLimit - 1.f);

		if (ForceColumn == 0)
			Point.x = std::clamp(Point.x, Item.Bounds.left, ValuesLeft - 1.f);
		else if (ForceColumn == 1)
			Point.x = std::clamp(Point.x, ValuesLeft, TextLeft - 1.f);
		else
			Point.x = std::clamp(Point.x, TextLeft, Layout.Right - 1.f);
	}

	if (Point.x < Item.Bounds.left || Point.x >= Item.Bounds.right || Point.y < Item.Bounds.top || Point.y >= RowLimit)
		return false;

	const uint64_t Row = HexFirstRow(Item, RowPx) + static_cast<uint64_t>((Point.y - Item.Bounds.top) / RowPx);

	if (Row >= HexRowCount(Item))
		return false;

	uint64_t Offset = 0;

	if (Point.x < ValuesLeft)
	{
		// The address column selects whole rows.
		Column = 0;
		Width = 16;
	}
	else if (Point.x < TextLeft)
	{
		// Value cells select whole units, matching how the value is rendered.
		const int Cell = std::clamp(static_cast<int>((Point.x - Layout.ValuesX + Half) / Layout.CellWidth), 0, Layout.Columns - 1);
		Column = 1;
		Offset = static_cast<uint64_t>(Cell) * static_cast<uint64_t>(Layout.Unit);
		Width = static_cast<uint64_t>(Layout.Unit);
	}
	else if (Point.x < Layout.Right)
	{
		const int Stride = Item.HexUtf16 ? 2 : 1;
		const int Char = std::clamp(static_cast<int>((Point.x - Layout.TextX) / Layout.CharWidth), 0, Layout.TextChars - 1);
		Column = 2;
		Offset = static_cast<uint64_t>(Char) * static_cast<uint64_t>(Stride);
		Width = static_cast<uint64_t>(Stride);
	}
	else
	{
		return false;
	}

	const uint64_t Result = Row * 16 + Offset;

	if (Result >= Item.HexSpan)
		return false;

	Address = Result;
	return true;
}

bool PanelContent::HexReadByte(const UiNode& Item, uint64_t Address, uint8_t& Value) const
{
	uint64_t FileOffset = Address;

	if (!Item.HexData || Address >= Item.HexSpan)
		return false;

	if (Item.HexResolve && !Item.HexResolve(Address, FileOffset))
		return false;

	if (FileOffset >= Item.HexSize)
		return false;

	Value = Item.HexData[FileOffset];
	return true;
}

void PanelContent::HexBeginEdit(UiNode& Item, int Column, uint64_t Address)
{
	const uint64_t Width = Column == 1 ? static_cast<uint64_t>(HexUnitBytes(Item)) : Item.HexUtf16 ? 2 : 1;
	const uint64_t Start = Address - Address % Width;

	if (!Item.HexWrite || Column == 0 || Start >= Item.HexSpan)
		return;

	Item.HexEditing = true;
	Item.HexEditColumn = Column;
	Item.HexEditAddress = Start;
	Item.HexEditText.clear();
	Item.HexSelColumn = Column;
	Item.HexSelAnchor = Start;
	Item.HexSelStart = Start;
	Item.HexSelEnd = std::min(Start + Width, Item.HexSpan);
	Item.HexHasSelection = true;
}

bool PanelContent::HexCommitEdit(UiNode& Item)
{
	if (!Item.HexEditing || Item.HexEditColumn != 1 || Item.HexEditText.empty() || !Item.HexWrite)
		return false;

	const int Unit = HexUnitBytes(Item);
	const int Bits = Unit * 8;
	const uint64_t Mask = Bits >= 64 ? ~0ull : (1ull << Bits) - 1;
	const wchar_t* Begin = Item.HexEditText.c_str();
	wchar_t* End = nullptr;
	uint64_t Raw = 0;
	errno = 0;

	if (Item.HexKind != 1)
	{
		Raw = wcstoull(Begin, &End, 16);
	}
	else if (Item.HexSigned)
	{
		const long long Number = wcstoll(Begin, &End, 10);
		const long long Low = Bits >= 64 ? LLONG_MIN : -(1ll << (Bits - 1));
		const long long High = Bits >= 64 ? LLONG_MAX : (1ll << (Bits - 1)) - 1;

		if (Number < Low || Number > High)
			return false;

		Raw = static_cast<uint64_t>(Number) & Mask;
	}
	else
	{
		Raw = wcstoull(Begin, &End, 10);

		if (Raw > Mask)
			return false;
	}

	if (errno == ERANGE || End == Begin || (End && *End))
		return false;

	std::vector<uint8_t> Bytes(static_cast<size_t>(Unit));

	for (int Part = 0; Part < Unit; Part++)
		Bytes[static_cast<size_t>(Part)] = static_cast<uint8_t>(Raw >> (8 * Part));

	Item.HexEditText.clear();
	std::function<void(uint64_t, const std::vector<uint8_t>&)> Write = Item.HexWrite;
	Write(Item.HexEditAddress, Bytes);
	return true;
}

void PanelContent::HexEndEdit(UiNode& Item, bool Commit)
{
	if (Commit)
		HexCommitEdit(Item);

	Item.HexEditing = false;
	Item.HexEditText.clear();
}

std::wstring PanelContent::HexRowValues(const UiNode& Item, uint64_t RowAddress) const
{
	const int Unit = HexUnitBytes(Item);
	const int Digits = HexCellDigits(Item);
	std::wstring Values;

	for (int Column = 0; Column < 16 / Unit; Column++)
	{
		uint64_t Raw = 0;
		bool Complete = true;

		for (int Part = 0; Part < Unit; Part++)
		{
			uint8_t Value = 0;
			Complete = HexReadByte(Item, RowAddress + static_cast<uint64_t>(Column * Unit + Part), Value) && Complete;
			Raw |= static_cast<uint64_t>(Value) << (8 * Part);
		}

		if (!Values.empty())
			Values += L' ';

		if (Complete)
			Values += HexFormatCell(Item, Raw, Unit, Digits, true);
		else
			Values.append(Item.HexKind == 1 ? 1 : static_cast<size_t>(Digits), L'?');
	}

	return Values;
}

std::wstring PanelContent::HexRowText(const UiNode& Item, uint64_t RowAddress) const
{
	const uint64_t Stride = Item.HexUtf16 ? 2 : 1;
	const int Chars = Item.HexUtf16 ? 8 : 16;
	std::wstring Text;

	for (int Char = 0; Char < Chars; Char++)
	{
		uint8_t Low = 0;
		uint8_t High = 0;
		const bool Present = HexReadByte(Item, RowAddress + static_cast<uint64_t>(Char) * Stride, Low) &&
			(Stride == 1 || HexReadByte(Item, RowAddress + static_cast<uint64_t>(Char) * Stride + 1, High));
		Text += Present ? HexTextChar(static_cast<unsigned>(Low) | (static_cast<unsigned>(High) << 8), Item.HexUtf16) : L'?';
	}

	return Text;
}

std::wstring PanelContent::HexAddressLabel(const UiNode& Item, uint64_t Address) const
{
	std::wstring Label;

	if (Item.HexAddressText)
	{
		Label = Item.HexAddressText(Address);
	}
	else
	{
		wchar_t Buffer[24];
		swprintf_s(Buffer, L"%08llX", static_cast<unsigned long long>(Address));
		Label = Buffer;
	}

	// The column pads the section name so every digit lines up; a copied label keeps a single separator.
	std::wstring Clean;
	Clean.reserve(Label.size());
	bool Space = false;

	for (wchar_t Char : Label)
	{
		if (Char == L' ')
		{
			Space = true;
			continue;
		}

		if (Space && !Clean.empty())
			Clean += L' ';

		Space = false;
		Clean += Char;
	}

	return Clean;
}

void PanelContent::HexCopySelection(int Node, HexCopy Mode) const
{
	if (!Valid(Node))
		return;

	const UiNode& Dump = Nodes[Node];

	if (Dump.Kind != UiNodeKind::HexView || !Dump.HexHasSelection || Dump.HexSelEnd <= Dump.HexSelStart)
		return;

	if (Mode == HexCopy::Column)
		Mode = Dump.HexSelColumn == 0 ? HexCopy::Address : Dump.HexSelColumn == 1 ? HexCopy::Values : HexCopy::Text;

	std::wstring Text;

	if (Mode == HexCopy::Address)
	{
		for (uint64_t Row = Dump.HexSelStart / 16; Row <= (Dump.HexSelEnd - 1) / 16; Row++)
		{
			if (!Text.empty())
				Text += L"\r\n";

			Text += HexAddressLabel(Dump, Row * 16);
		}
	}
	else if (Mode == HexCopy::Values)
	{
		// Values as displayed, one space between cells; the selection is widened to whole units.
		const int Unit = HexUnitBytes(Dump);

		for (uint64_t Address = Dump.HexSelStart - Dump.HexSelStart % Unit; Address < Dump.HexSelEnd; Address += Unit)
		{
			uint64_t Raw = 0;
			bool Complete = true;

			for (int Part = 0; Part < Unit; Part++)
			{
				uint8_t Value = 0;
				Complete = HexReadByte(Dump, Address + static_cast<uint64_t>(Part), Value) && Complete;
				Raw |= static_cast<uint64_t>(Value) << (8 * Part);
			}

			if (!Text.empty())
				Text += L' ';

			if (Complete)
				Text += HexFormatCell(Dump, Raw, Unit, 0, false);
			else
				Text.append(Dump.HexKind == 1 ? 1 : static_cast<size_t>(Unit * 2), L'?');
		}
	}
	else if (Mode == HexCopy::Text)
	{
		const uint64_t Stride = Dump.HexUtf16 ? 2 : 1;

		for (uint64_t Address = Dump.HexSelStart - Dump.HexSelStart % Stride; Address < Dump.HexSelEnd; Address += Stride)
		{
			uint8_t Low = 0;
			uint8_t High = 0;
			const bool Present = HexReadByte(Dump, Address, Low) && (Stride == 1 || HexReadByte(Dump, Address + 1, High));
			Text += Present ? HexTextChar(static_cast<unsigned>(Low) | (static_cast<unsigned>(High) << 8), Dump.HexUtf16) : L'?';
		}
	}
	else
	{
		// One full line per selected row: address, spacing, values, spacing, text.
		for (uint64_t Row = Dump.HexSelStart / 16; Row <= (Dump.HexSelEnd - 1) / 16; Row++)
		{
			if (!Text.empty())
				Text += L"\r\n";

			Text += HexAddressLabel(Dump, Row * 16) + L"  " + HexRowValues(Dump, Row * 16) + L"  " + HexRowText(Dump, Row * 16);
		}
	}

	if (!Text.empty())
		CopyTextToClipboard(Text);
}

bool PanelContent::BeginCellEdit(int Node, int Row, int Column)
{
	if (!Valid(Node))
		return false;

	UiNode& Item = Nodes[Node];

	if (Item.Kind != UiNodeKind::TableList || Row < 0 || Row >= static_cast<int>(Item.Rows.size()) || Column < 0 ||
		Column >= static_cast<int>(Item.Rows[static_cast<size_t>(Row)].size()) || !Item.CanEdit || !Item.CanEdit(Row, Column))
		return false;

	CloseMenus();
	ClearFocus();
	Focused = Node;
	Item.SelectedRow = Row;
	Item.SelectedColumn = Column;
	Item.EditRow = Row;
	Item.EditColumn = Column;
	Item.EditText = Item.Rows[static_cast<size_t>(Row)][static_cast<size_t>(Column)];
	Item.EditCaret = static_cast<int>(Item.EditText.size());
	return true;
}

void PanelContent::CommitTableEdit(int Node)
{
	if (!Valid(Node))
		return;

	UiNode& Item = Nodes[Node];

	if (Item.Kind != UiNodeKind::TableList || Item.EditRow < 0 || Item.EditRow >= static_cast<int>(Item.Rows.size()) ||
		Item.EditColumn < 0 || Item.EditColumn >= static_cast<int>(Item.Rows[static_cast<size_t>(Item.EditRow)].size()))
	{
		Item.EditRow = -1;
		return;
	}

	const int Row = Item.EditRow;
	const int Column = Item.EditColumn;
	const std::wstring Value = Item.EditText;
	Item.Rows[static_cast<size_t>(Row)][static_cast<size_t>(Column)] = Value;
	Item.EditRow = -1;
	Item.EditColumn = -1;
	Item.EditCaret = 0;

	std::function<void(int, int, const std::wstring&)> Callback = Item.OnCellEdit;

	if (Focused == Node)
		ClearFocus();

	if (Callback)
		Callback(Row, Column, Value);
}

void PanelContent::CancelTableEdit(int Node)
{
	if (!Valid(Node))
		return;

	if (Nodes[Node].Kind == UiNodeKind::TableList)
	{
		Nodes[Node].EditRow = -1;
		Nodes[Node].EditColumn = -1;
	}

	if (Focused == Node)
		ClearFocus();
}

int PanelContent::OpenMenuNode() const
{
	for (int Node = 0; Node < static_cast<int>(Nodes.size()); Node++)
	{
		if (Nodes[Node].MenuOpen && !Nodes[Node].MenuItems.empty())
			return Node;
	}

	return -1;
}

void PanelContent::CloseMenus()
{
	for (UiNode& Item : Nodes)
	{
		Item.MenuOpen = false;
		Item.MenuHover = -1;
	}
}

void PanelContent::AdoptViewState(const PanelContent& Previous)
{
	const size_t Count = std::min(Nodes.size(), Previous.Nodes.size());

	for (size_t Index = 0; Index < Count; Index++)
	{
		UiNode& Item = Nodes[Index];
		const UiNode& Old = Previous.Nodes[Index];

		// A hex dump rebuilt by a refresh is a navigation (new snap target), so it keeps its own view.
		if (Item.Kind != Old.Kind || Item.Kind == UiNodeKind::HexView)
			continue;

		Item.Scroll = Old.Scroll;
		Item.ScrollX = Old.ScrollX;

		if (Item.Kind == UiNodeKind::Collapsible && Item.Text == Old.Text)
			Item.Checked = Old.Checked;

		if (Item.Kind == UiNodeKind::TableList && Old.SelectedRow < static_cast<int>(Item.Rows.size()))
		{
			Item.SelectedRow = Old.SelectedRow;
			Item.SelectedColumn = Old.SelectedColumn;
		}
	}
}

void PanelContent::OpenMenu(int Node, vec2 Point)
{
	CloseMenus();

	UiNode& Item = Nodes[Node];
	Item.MenuOpen = true;
	Item.MenuPoint = Point;
	Item.MenuHover = -1;

	// The owner fills the items for the clicked target; an empty list means no menu.
	std::function<void()> Fill = Item.OnMenuOpen;

	if (Fill)
		Fill();

	if (Nodes[Node].MenuItems.empty())
		Nodes[Node].MenuOpen = false;
}

bool PanelContent::OnRightMouseDown(vec2 Point)
{
	// Right-click in the dump opens the context menu at the pointer. A press outside the current selection
	// first selects the byte under it, so menu actions always apply to what is highlighted.
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (Item.Kind != UiNodeKind::HexView || !Item.Visible || !PointInRect(Item.Bounds, Point) || !PointVisible(Index, Point))
			continue;

		if (Item.HexEditing)
			HexEndEdit(Item, true);

		int Column = 1;
		uint64_t Address = 0;
		uint64_t Width = 1;
		Item.HexMenuOnByte = HexHit(Item, Point, Column, Address, Width);
		Item.HexMenuAddress = Address;

		if (Item.HexMenuOnByte)
		{
			const bool Inside = Item.HexHasSelection && Column == Item.HexSelColumn &&
				Address >= Item.HexSelStart && Address < Item.HexSelEnd;

			if (!Inside)
			{
				Item.HexSelColumn = Column;
				Item.HexSelAnchor = Address;
				Item.HexSelStart = Address;
				Item.HexSelEnd = std::min(Address + Width, Item.HexSpan);
				Item.HexHasSelection = true;
			}
		}

		ClearFocus();
		Focused = Index;
		OpenMenu(Index, Point);
		return true;
	}

	// Tables: right-click selects the cell under the pointer and opens the owner's menu for it.
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (Item.Kind != UiNodeKind::TableList || !Item.Visible)
			continue;

		const int Row = TableListRowAt(Item, Point);
		const int Column = TableListColumnAt(Item, Point);

		if (Row < 0 || Column < 0 || !PointVisible(Index, Point))
			continue;

		if (Item.EditRow >= 0)
			CommitTableEdit(Index);

		ClearFocus();
		Focused = Index;
		Nodes[Index].SelectedRow = Row;
		Nodes[Index].SelectedColumn = Column;

		if (!Nodes[Index].OnMenuOpen)
			return true;

		OpenMenu(Index, Point);
		return true;
	}

	return false;
}

int PanelContent::Collapsible(int Parent, const std::wstring& Title, bool Expanded, float Padding, float Gap)
{
	// No card: a collapsible is a section of the panel, so it inherits the panel surface and only its
	// header row reacts to the pointer. Content keeps the panel's own indent.
	UiStyle Style;
	Style.Flow = UiFlow::Vertical;
	Style.Padding = Padding;
	Style.Gap = Gap;
	const int Id = Add(UiNodeKind::Collapsible, Parent, Style, Title);
	Nodes[Id].Checked = Expanded;
	return Id;
}

int PanelContent::Rect(int Parent, UiPaint Fill)
{
	UiStyle Style;
	Style.Background = Fill;
	return Add(UiNodeKind::Rect, Parent, Style, L"");
}

int PanelContent::Spacer(int Parent, float Size, float Grow)
{
	UiStyle Style;
	Style.Height = Size > 0.f ? UiSize::Fixed : UiSize::Fill;
	Style.HeightValue = Size;
	Style.Grow = Grow;
	return Add(UiNodeKind::Spacer, Parent, Style, L"");
}

int PanelContent::DragHandle(int Parent, bool Vertical, float Thickness)
{
	UiStyle Style;

	if (Vertical)
	{
		Style.Width = UiSize::Fixed;
		Style.WidthValue = Thickness;
		Style.Height = UiSize::Fill;
	}
	else
	{
		Style.Width = UiSize::Fill;
		Style.Height = UiSize::Fixed;
		Style.HeightValue = Thickness;
	}

	const int Id = Add(UiNodeKind::DragHandle, Parent, Style, L"");
	Nodes[Id].ResizeVertical = Vertical;
	return Id;
}

bool PanelContent::InSubtree(int Node, int Ancestor) const
{
	for (int Current = Node; Valid(Current); Current = Nodes[Current].Parent)
	{
		if (Current == Ancestor)
			return true;
	}

	return false;
}

void PanelContent::SetVisible(int Node, bool Visible)
{
	if (!Valid(Node))
		return;

	Nodes[Node].Visible = Visible;

	if (!Visible)
		HideSubtree(Node);
}

void PanelContent::SetEnabled(int Node, bool Enabled)
{
	if (!Valid(Node))
		return;

	Nodes[Node].Enabled = Enabled;

	if (Enabled)
		return;

	if (Valid(Focused) && InSubtree(Focused, Node))
		ClearFocus();

	if (Valid(Pressed) && InSubtree(Pressed, Node))
		CancelInteraction();

	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		if (Nodes[Index].Open && InSubtree(Index, Node))
			Nodes[Index].Open = false;
	}
}

void PanelContent::SetChecked(int Node, bool Checked)
{
	if (!Valid(Node))
		return;

	UiNode& Item = Nodes[Node];

	if (Item.Kind == UiNodeKind::Radio && Checked)
	{
		for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
		{
			if (Index != Node && Nodes[Index].Kind == UiNodeKind::Radio && Nodes[Index].RadioGroup == Item.RadioGroup)
				Nodes[Index].Checked = false;
		}
	}

	Item.Checked = Checked;
}

void PanelContent::SetValue(int Node, double Value)
{
	if (Valid(Node))
		Nodes[Node].Value = SnapValue(Nodes[Node], Value);
}

void PanelContent::SetTextAlpha(float Alpha)
{
	TextAlpha = std::clamp(Alpha, 0.f, 1.f);
}

void PanelContent::Layout(rect Area, float ScaleValue)
{
	if (!Valid(RootNode))
		return;

	Scale = ScaleValue;
	ContentArea = Area;
	DeferredAbsolute.clear();

	LayoutNode(RootNode, Area);

	// Absolute nodes are placed after the flow pass so AnchorTo resolves against this frame's bounds,
	// not the previous frame's (which lagged one frame behind during resize/dock animations).
	for (size_t Index = 0; Index < DeferredAbsolute.size(); Index++)
		LayoutAbsolute(DeferredAbsolute[Index]);
}

rect PanelContent::ResolveReference(const UiNode& Item) const
{
	const int Anchor = Find(Item.Style.AnchorTo);

	if (Valid(Anchor))
		return Nodes[Anchor].Bounds;

	if (Valid(Item.Parent))
		return Nodes[Item.Parent].Inner;

	return ContentArea;
}

void PanelContent::HideSubtree(int Node)
{
	UiNode& Item = Nodes[Node];
	Item.Bounds = rect();
	Item.Inner = rect();
	Item.Open = false;

	if (Focused == Node)
		ClearFocus();

	if (Pressed == Node)
		CancelInteraction();

	for (int Child : Item.Children)
		HideSubtree(Child);
}

void PanelContent::UpdateWrap(UiNode& Item, float Width)
{
	const float Font = Item.Style.FontSize * Scale;
	Width = std::max(Width, 1.f);
	UiWrapCache& Cache = Item.Wrap;

	if (Cache.Width == Width && Cache.Font == Font && Cache.Source == Item.Text)
		return;

	Cache.Source = Item.Text;
	Cache.Width = Width;
	Cache.Font = Font;
	Cache.Lines.clear();

	const std::wstring& Value = Item.Text;
	const int Length = static_cast<int>(Value.size());
	std::wstring Probe;
	int Start = 0;

	while (true)
	{
		const size_t Break = Value.find(L'\n', static_cast<size_t>(Start));
		const int HardEnd = Break == std::wstring::npos ? Length : static_cast<int>(Break);
		int LineStart = Start;

		if (LineStart >= HardEnd)
			Cache.Lines.emplace_back();

		while (LineStart < HardEnd)
		{
			int Fit = -1;
			int Cursor = LineStart;

			while (Cursor < HardEnd)
			{
				int Next = Cursor;

				while (Next < HardEnd && Value[Next] == L' ')
					Next++;

				while (Next < HardEnd && Value[Next] != L' ')
					Next++;

				Probe.assign(Value, static_cast<size_t>(LineStart), static_cast<size_t>(Next - LineStart));

				if (renderer::measure_text(Probe.c_str(), Font).width() > Width)
					break;

				Fit = Next;
				Cursor = Next;
			}

			if (Fit < 0)
			{
				// A single word wider than the line: break it by characters, at least one per line.
				Fit = LineStart + 1;

				while (Fit < HardEnd)
				{
					Probe.assign(Value, static_cast<size_t>(LineStart), static_cast<size_t>(Fit + 1 - LineStart));

					if (renderer::measure_text(Probe.c_str(), Font).width() > Width)
						break;

					Fit++;
				}
			}

			Cache.Lines.emplace_back(Value, static_cast<size_t>(LineStart), static_cast<size_t>(Fit - LineStart));
			LineStart = Fit;

			while (LineStart < HardEnd && Value[LineStart] == L' ')
				LineStart++;
		}

		if (HardEnd >= Length)
			break;

		Start = HardEnd + 1;
	}
}

void PanelContent::LayoutNode(int Node, rect Bounds)
{
	if (!Valid(Node))
		return;

	UiNode& Item = Nodes[Node];
	Bounds = UiSnap(Bounds);
	Item.Bounds = Bounds;

	const float Pad = Item.Style.Padding * Scale;
	const float Header = Item.Kind == UiNodeKind::Collapsible ? CollapsibleHeader * Scale : 0.f;
	const float Indent = Item.Kind == UiNodeKind::Collapsible ? CollapsibleIndent * Scale : 0.f;
	const float InnerTop = Bounds.top + Header + Pad;
	Item.Inner = rect(Bounds.left + Pad + Indent, InnerTop,
		std::max(Bounds.left + Pad + Indent, Bounds.right - Pad), std::max(InnerTop, Bounds.bottom - Pad));

	switch (Item.Kind)
	{
	case UiNodeKind::Text:
		if (Item.Style.Wrap)
			UpdateWrap(Item, Bounds.width() - Pad * 2.f);
		return;
	case UiNodeKind::ListBox:
	{
		const float Total = static_cast<float>(Item.Options.size()) * ListRowHeight() + ListPad * Scale * 2.f;
		Item.ScrollMax = std::max(0.f, Total - Bounds.height());
		Item.Scroll = std::clamp(Item.Scroll, 0.f, Item.ScrollMax);
		return;
	}
	case UiNodeKind::TableList:
	{
		const float HeaderPx = TableHeader * Scale;
		const float Body = std::max(0.f, Bounds.height() - HeaderPx);
		const float Content = static_cast<float>(Item.Rows.size()) * Item.RowHeight * Scale;
		Item.ScrollMax = std::max(0.f, Content - Body);
		Item.Scroll = std::clamp(Item.Scroll, 0.f, Item.ScrollMax);
		return;
	}
	case UiNodeKind::HexView:
	{
		const float RowPx = Item.RowHeight * Scale;
		Item.HexCharWidth = HexMeasureCharWidth(Item);
		const float Content = static_cast<float>(HexRowCount(Item)) * RowPx;
		Item.ScrollMax = std::max(0.f, Content - Bounds.height());

		if (Item.HexSnapRow >= 0)
		{
			Item.Scroll = std::clamp(static_cast<float>(Item.HexSnapRow) * RowPx, 0.f, Item.ScrollMax);
			Item.HexSnapRow = -1;
		}
		else
		{
			Item.Scroll = std::clamp(Item.Scroll, 0.f, Item.ScrollMax);
		}

		return;
	}
	case UiNodeKind::Collapsible:
		if (!Item.Checked)
		{
			Item.ScrollMax = 0.f;
			Item.ScrollMaxX = 0.f;

			for (int Child : Item.Children)
				HideSubtree(Child);

			return;
		}
		break;
	case UiNodeKind::Container:
		break;
	default:
		return;
	}

	for (int Child : Item.Children)
	{
		if (!Nodes[Child].Visible)
			HideSubtree(Child);
		else if (Nodes[Child].Style.Absolute)
			DeferredAbsolute.push_back(Child);
	}

	LayoutFlow(Node, Item.Style.Flow == UiFlow::Vertical);
}

void PanelContent::LayoutAbsolute(int Node)
{
	UiNode& Item = Nodes[Node];
	const UiStyle& Style = Item.Style;
	const rect Ref = ResolveReference(Item);

	float Width = 0.f;

	if (Style.AnchorLeft && Style.AnchorRight)
		Width = std::max(0.f, Ref.width() - (Style.Left + Style.Right) * Scale);
	else if (Style.Width == UiSize::Fixed)
		Width = Style.WidthValue * Scale;
	else if (Style.Width == UiSize::Percent)
		Width = Ref.width() * Style.WidthValue / 100.f;
	else if (Style.Width == UiSize::Fill)
		Width = std::max(0.f, Ref.width() - (Style.Left + Style.Right) * Scale);
	else
		Width = MeasureWidth(Node, Ref.height());

	float Height = 0.f;

	if (Style.AnchorTop && Style.AnchorBottom)
		Height = std::max(0.f, Ref.height() - (Style.Top + Style.Bottom) * Scale);
	else if (Style.Height == UiSize::Fixed)
		Height = Style.HeightValue * Scale;
	else if (Style.Height == UiSize::Percent)
		Height = Ref.height() * Style.HeightValue / 100.f;
	else if (Style.Height == UiSize::Fill)
		Height = std::max(0.f, Ref.height() - (Style.Top + Style.Bottom) * Scale);
	else
		Height = MeasureHeight(Node, Width);

	float X = Ref.left + Style.Left * Scale;

	if (Style.AnchorRight && !Style.AnchorLeft)
		X = Ref.right - Style.Right * Scale - Width;

	float Y = Ref.top + Style.Top * Scale;

	if (Style.AnchorBottom && !Style.AnchorTop)
		Y = Ref.bottom - Style.Bottom * Scale - Height;

	LayoutNode(Node, rect(X, Y, X + Width, Y + Height));
}

void PanelContent::LayoutFlow(int Node, bool Vertical)
{
	UiNode& Parent = Nodes[Node];
	const float Gap = Parent.Style.Gap * Scale;
	const float Pad = Parent.Style.Padding * Scale;
	const float Available = Vertical ? Parent.Inner.height() : Parent.Inner.width();

	struct FlowItem
	{
		int Node = -1;
		float Main = 0.f;
		float Cross = 0.f;
		float Margin = 0.f;
		float Weight = 0.f;
		bool Fill = false;
	};

	std::vector<FlowItem> Items;
	Items.reserve(Parent.Children.size());

	float FixedTotal = 0.f;
	float FillWeight = 0.f;
	float GapTotal = 0.f;

	for (int Child : Parent.Children)
	{
		UiNode& Item = Nodes[Child];
		if (Item.Style.Absolute || !Item.Visible)
			continue;

		FlowItem Flow;
		Flow.Node = Child;
		Flow.Margin = Item.Style.Margin * Scale;

		const UiSize MainMode = Vertical ? Item.Style.Height : Item.Style.Width;
		const float MainValue = Vertical ? Item.Style.HeightValue : Item.Style.WidthValue;

		if (MainMode == UiSize::Fill)
		{
			Flow.Fill = true;
			Flow.Weight = Item.Style.Grow > 0.f ? Item.Style.Grow : 1.f;
			FillWeight += Flow.Weight;
		}
		else if (MainMode == UiSize::Fixed)
		{
			Flow.Main = MainValue * Scale;
		}
		else if (MainMode == UiSize::Percent)
		{
			Flow.Main = Available * MainValue / 100.f;
		}
		else
		{
			Flow.Main = Vertical
				? MeasureHeight(Child, Parent.Inner.width() - Flow.Margin * 2.f)
				: MeasureWidth(Child, Parent.Inner.height() - Flow.Margin * 2.f);
		}

		if (!Items.empty())
			GapTotal += Gap;

		FixedTotal += Flow.Main + Flow.Margin * 2.f;
		Items.push_back(Flow);
	}

	const float Remaining = std::max(0.f, Available - FixedTotal - GapTotal);

	for (FlowItem& Flow : Items)
	{
		const UiNode& Item = Nodes[Flow.Node];
		const float Main = Flow.Fill && FillWeight > 0.f ? Remaining * Flow.Weight / FillWeight : Flow.Main;
		const float CrossSpace = std::max(0.f, (Vertical ? Parent.Inner.width() : Parent.Inner.height()) - Flow.Margin * 2.f);
		const UiSize CrossMode = Vertical ? Item.Style.Width : Item.Style.Height;
		const float CrossValue = Vertical ? Item.Style.WidthValue : Item.Style.HeightValue;

		switch (CrossMode)
		{
		case UiSize::Fixed:
			Flow.Cross = CrossValue * Scale;
			break;
		case UiSize::Percent:
			Flow.Cross = (Vertical ? Parent.Inner.width() : Parent.Inner.height()) * CrossValue / 100.f;
			break;
		case UiSize::Fill:
			Flow.Cross = CrossSpace;
			break;
		default:
			Flow.Cross = Vertical ? MeasureWidth(Flow.Node, Main) : MeasureHeight(Flow.Node, Main);
			if (Vertical && Flow.Cross <= 0.f)
				Flow.Cross = CrossSpace;
			break;
		}

		if (Item.Style.Align == UiAlign::Stretch)
			Flow.Cross = CrossSpace;
	}

	if (Parent.Style.Scroll && Vertical)
	{
		const float TotalContent = FixedTotal + GapTotal + Pad * 2.f;
		Parent.ScrollMax = std::max(0.f, TotalContent - Parent.Bounds.height());
		Parent.Scroll = std::clamp(Parent.Scroll, 0.f, Parent.ScrollMax);
	}
	else
	{
		Parent.ScrollMax = 0.f;
	}

	if (Parent.Style.ScrollX)
	{
		// Horizontal flow scrolls on the main axis; a vertical flow scrolls on the widest child.
		float ContentWidth = FixedTotal + GapTotal + Pad * 2.f;

		if (Vertical)
		{
			float MaxCross = 0.f;
			for (const FlowItem& Flow : Items)
				MaxCross = std::max(MaxCross, Flow.Cross + Flow.Margin * 2.f);

			ContentWidth = MaxCross + Pad * 2.f;
		}

		Parent.ScrollMaxX = std::max(0.f, ContentWidth - Parent.Bounds.width());
		Parent.ScrollX = std::clamp(Parent.ScrollX, 0.f, Parent.ScrollMaxX);
	}
	else
	{
		Parent.ScrollMaxX = 0.f;
	}

	float Cursor = Vertical ? Parent.Inner.top : Parent.Inner.left;

	if (Parent.Style.Scroll && Vertical)
		Cursor -= Parent.Scroll;

	if (Parent.Style.ScrollX && !Vertical)
		Cursor -= Parent.ScrollX;

	for (const FlowItem& Flow : Items)
	{
		const UiNode& Item = Nodes[Flow.Node];
		const float Main = Flow.Fill && FillWeight > 0.f ? Remaining * Flow.Weight / FillWeight : Flow.Main;
		const float Cross = Flow.Cross;
		const float CrossShift = Vertical && Parent.Style.ScrollX ? Parent.ScrollX : 0.f;

		const float CrossStart = (Vertical ? Parent.Inner.left : Parent.Inner.top) - CrossShift;
		const float CrossEnd = (Vertical ? Parent.Inner.right : Parent.Inner.bottom) - CrossShift;
		float Offset = CrossStart + Flow.Margin;

		if (Item.Style.Align == UiAlign::Center)
			Offset = CrossStart + ((CrossEnd - CrossStart) - Cross) * 0.5f;
		else if (Item.Style.Align == UiAlign::End)
			Offset = CrossEnd - Flow.Margin - Cross;

		const float MainStart = Cursor + Flow.Margin;

		if (Vertical)
			LayoutNode(Flow.Node, rect(Offset, MainStart, Offset + Cross, MainStart + Main));
		else
			LayoutNode(Flow.Node, rect(MainStart, Offset, MainStart + Main, Offset + Cross));

		Cursor += Main + Flow.Margin * 2.f + Gap;
	}
}

float PanelContent::MeasureChildren(int Node, float Cross, bool Height)
{
	const UiNode& Item = Nodes[Node];
	const bool Along = (Item.Style.Flow == UiFlow::Vertical) == Height;
	const float Gap = Item.Style.Gap * Scale;
	float Total = 0.f;
	bool First = true;

	for (int Child : Item.Children)
	{
		const UiNode& ChildNode = Nodes[Child];
		if (ChildNode.Style.Absolute || !ChildNode.Visible)
			continue;

		const float Margin = ChildNode.Style.Margin * Scale * 2.f;
		const float Size = Height ? MeasureHeight(Child, Cross - Margin) : MeasureWidth(Child, Cross - Margin);

		if (Along)
		{
			if (!First)
				Total += Gap;

			Total += Size + Margin;
		}
		else
		{
			Total = std::max(Total, Size + Margin);
		}

		First = false;
	}

	return Total;
}

float PanelContent::MeasureHeight(int Node, float Width)
{
	if (!Valid(Node))
		return 0.f;

	UiNode& Item = Nodes[Node];
	const UiStyle& Style = Item.Style;

	if (!Item.Visible)
		return 0.f;

	if (Style.Height == UiSize::Fixed)
		return Style.HeightValue * Scale;

	const float Pad = Style.Padding * Scale;
	const float Line = renderer::line_height(Style.FontSize * Scale);
	const bool Flexible = Style.Height == UiSize::Percent || Style.Height == UiSize::Fill;

	switch (Item.Kind)
	{
	case UiNodeKind::Text:
		if (Style.Wrap)
		{
			UpdateWrap(Item, Width - Pad * 2.f);
			return Line * static_cast<float>(std::max<size_t>(1, Item.Wrap.Lines.size())) + Pad * 2.f;
		}
		return Line + Pad * 2.f;
	case UiNodeKind::Button:
		// Same height as inputs and dropdowns so mixed rows line up; taller only for large fonts.
		return std::max(30.f * Scale, std::round(Line + 12.f * Scale));
	case UiNodeKind::Input:
	case UiNodeKind::Dropdown:
		return 30.f * Scale;
	case UiNodeKind::Slider:
		return 40.f * Scale;
	case UiNodeKind::Progress:
		return 30.f * Scale;
	case UiNodeKind::Checkbox:
	case UiNodeKind::Toggle:
	case UiNodeKind::Radio:
		return std::max(Line, ToggleHeight * Scale) + Pad * 2.f;
	case UiNodeKind::Separator:
		return 9.f * Scale;
	case UiNodeKind::Image:
		return Item.Texture ? renderer::texture_size(Item.Texture).y * Scale : 64.f * Scale;
	case UiNodeKind::Icon:
		return Item.IconGlyph != UiIcon::None ? 16.f * Scale : 0.f;
	case UiNodeKind::ListBox:
		return 140.f * Scale;
	case UiNodeKind::Rect:
	case UiNodeKind::Spacer:
	case UiNodeKind::DragHandle:
	case UiNodeKind::TableList:
	case UiNodeKind::HexView:
		return Flexible ? 0.f : Style.HeightValue * Scale;
	case UiNodeKind::Collapsible:
		if (Flexible)
			return 0.f;
		if (!Item.Checked)
			return CollapsibleHeader * Scale;
		return CollapsibleHeader * Scale + MeasureChildren(Node, Width - Pad * 2.f, true) + Pad * 2.f;
	case UiNodeKind::Container:
		if (Flexible)
			return 0.f;
		return MeasureChildren(Node, Width - Pad * 2.f, true) + Pad * 2.f;
	}

	return 0.f;
}

float PanelContent::MeasureWidth(int Node, float Height)
{
	if (!Valid(Node))
		return 0.f;

	const UiNode& Item = Nodes[Node];
	const UiStyle& Style = Item.Style;

	if (!Item.Visible)
		return 0.f;

	if (Style.Width == UiSize::Fixed)
		return Style.WidthValue * Scale;

	const float Pad = Style.Padding * Scale;
	const float Font = Style.FontSize * Scale;
	const bool Flexible = Style.Width == UiSize::Percent || Style.Width == UiSize::Fill;

	switch (Item.Kind)
	{
	case UiNodeKind::Text:
		return renderer::measure_text(Item.Text.c_str(), Font).width() + Pad * 2.f;
	case UiNodeKind::Button:
		return renderer::measure_text(Item.Text.c_str(), Font).width() + Pad * 2.f + 12.f * Scale +
			(Item.IconGlyph != UiIcon::None ? 16.f * Scale + ButtonIconGap * Scale : 0.f);
	case UiNodeKind::Input:
	case UiNodeKind::Dropdown:
	case UiNodeKind::Slider:
	case UiNodeKind::Progress:
	case UiNodeKind::ListBox:
		return Flexible ? 0.f : 120.f * Scale;
	case UiNodeKind::Checkbox:
	case UiNodeKind::Toggle:
	case UiNodeKind::Radio:
	{
		const float Label = Item.Text.empty() ? 0.f : IndicatorGap * Scale + renderer::measure_text(Item.Text.c_str(), Font).width();
		return IndicatorWidth(Item.Kind) * Scale + Label + Pad * 2.f;
	}
	case UiNodeKind::Separator:
		return 9.f * Scale;
	case UiNodeKind::Icon:
		return Item.IconGlyph != UiIcon::None ? 16.f * Scale : 0.f;
	case UiNodeKind::Image:
		return Item.Texture ? renderer::texture_size(Item.Texture).x * Scale : 64.f * Scale;
	case UiNodeKind::Rect:
	case UiNodeKind::Spacer:
	case UiNodeKind::DragHandle:
	case UiNodeKind::TableList:
		return Flexible ? 0.f : Style.WidthValue * Scale;
	case UiNodeKind::HexView:
	{
		if (Flexible)
			return 0.f;

		// Columns plus a lane for the vertical scrollbar so the bar never sits on the text column.
		Nodes[Node].HexCharWidth = HexMeasureCharWidth(Item);
		return HexLayout(Item, 0.f, Scale).Right + 18.f * Scale;
	}
	case UiNodeKind::Collapsible:
	{
		if (Flexible)
			return 0.f;

		const float Title = renderer::measure_text(Item.Text.c_str(), Font).width() + 40.f * Scale;
		return Item.Checked ? std::max(Title, MeasureChildren(Node, Height - Pad * 2.f, false) + Pad * 2.f) : Title;
	}
	case UiNodeKind::Container:
		if (Flexible)
			return 0.f;
		return MeasureChildren(Node, Height - Pad * 2.f, false) + Pad * 2.f;
	}

	return 0.f;
}

color PanelContent::ResolvePaint(UiPaint Paint, const UiTheme& Theme, UiColorRole Fallback) const
{
	if (!Paint.Set)
		return UiPaint::RolePaint(Fallback).Resolve(Theme);

	return Paint.Resolve(Theme);
}

rect PanelContent::InteractiveRect(const UiNode& Item) const
{
	if (Item.Kind == UiNodeKind::Collapsible)
		return rect(Item.Bounds.left, Item.Bounds.top, Item.Bounds.right, std::min(Item.Bounds.bottom, Item.Bounds.top + CollapsibleHeader * Scale));

	return Item.Bounds;
}

float PanelContent::ListRowHeight() const
{
	return ListRow * Scale;
}

rect PanelContent::ScrollTrack(const UiNode& Item) const
{
	// 6 px: a 4 px pill has a 2 px end radius, which rasterises as a one-pixel nub rather than a curve.
	const float TrackWidth = std::round(6.f * Scale);

	if (Item.Kind == UiNodeKind::TableList || Item.Kind == UiNodeKind::HexView)
	{
		// A wide table/dump is scrolled horizontally by its section; keep the vertical bar at the visible edge.
		const rect View = VisibleBounds(Item);
		const float Right = std::min(Item.Bounds.right, View.right) - std::round(3.f * Scale);
		const float Top = std::max(Item.Kind == UiNodeKind::TableList ? Item.Bounds.top + TableHeader * Scale : Item.Bounds.top,
			View.top);
		const float Bottom = std::min(Item.Bounds.bottom, View.bottom) - 4.f * Scale;

		if (Bottom <= Top || Right <= View.left)
			return rect();

		return rect(Right - TrackWidth, Top, Right, Bottom);
	}

	const float Right = Item.Bounds.right - std::round(3.f * Scale);
	return rect(Right - TrackWidth, Item.Bounds.top + 4.f * Scale, Right, Item.Bounds.bottom - 4.f * Scale);
}

rect PanelContent::ScrollThumb(const UiNode& Item) const
{
	const rect Track = ScrollTrack(Item);
	const float Visible = Item.Kind == UiNodeKind::TableList
		? std::max(0.f, Item.Bounds.height() - TableHeader * Scale)
		: Item.Bounds.height();
	const float ThumbHeight = std::min(Track.height(), std::max(24.f * Scale, Track.height() * Visible / (Visible + Item.ScrollMax)));
	const float Travel = std::max(0.f, Track.height() - ThumbHeight);
	const float Offset = Item.ScrollMax > 0.f ? (Item.Scroll / Item.ScrollMax) * Travel : 0.f;
	const float Top = std::round(Track.top + Offset);
	return rect(Track.left, Top, Track.right, Top + std::round(ThumbHeight));
}

rect PanelContent::VisibleBounds(const UiNode& Item) const
{
	rect Result = Item.Bounds;

	for (int Parent = Item.Parent; Valid(Parent); Parent = Nodes[Parent].Parent)
	{
		const UiNode& Ancestor = Nodes[Parent];

		if (!Ancestor.Style.Clip && !Ancestor.Style.Scroll && !Ancestor.Style.ScrollX)
			continue;

		Result = IntersectionRect(Result, Ancestor.Bounds);
	}

	return Result;
}

rect PanelContent::MenuRect(const UiNode& Item) const
{
	// The menu belongs to the panel, not to the node: it is kept inside the panel's content area so a
	// short table near the bottom still gets a fully visible menu.
	const rect View = ContentArea;
	const float ItemHeight = 20.f * Scale;
	float Height = MenuItemsOffset(Item, Scale) + 4.f * Scale;

	for (size_t Index = 0; Index < Item.MenuItems.size(); Index++)
	{
		Height += ItemHeight;

		if (MenuHasSeparator(Item, static_cast<int>(Index)))
			Height += 7.f * Scale;
	}

	// Width follows the content: the widest label (plus its decoration and hint) decides it, so a menu of
	// short words stays narrow instead of reserving space for the longest possible entry.
	const float ItemFont = 11.5f * Scale;
	float Label = 0.f;
	float Hint = 0.f;

	for (size_t Index = 0; Index < Item.MenuItems.size(); Index++)
	{
		Label = std::max(Label, renderer::measure_text(Item.MenuItems[Index].c_str(), ItemFont).width());

		if (Index < Item.MenuHints.size() && !Item.MenuHints[Index].empty())
			Hint = std::max(Hint, renderer::measure_text(Item.MenuHints[Index].c_str(), ItemFont).width());
	}

	const float Decoration = MenuDecorationWidth(Item, Scale);
	const float Width = std::clamp(14.f * Scale + Decoration + Label + (Hint > 0.f ? 10.f * Scale + Hint : 0.f) + 10.f * Scale,
		110.f * Scale, std::max(110.f * Scale, View.width()));

	const float Left = std::clamp(Item.MenuPoint.x, View.left, std::max(View.left, View.right - Width));
	const float Top = std::clamp(Item.MenuPoint.y, View.top, std::max(View.top, View.bottom - Height));
	return rect(Left, Top, Left + Width, Top + Height);
}

int PanelContent::MenuIndexAt(const UiNode& Item, vec2 Point) const
{
	if (!Item.MenuOpen || Item.MenuItems.empty())
		return -1;

	const rect Menu = MenuRect(Item);

	if (!PointInRect(Menu, Point))
		return -1;

	const float ItemHeight = 20.f * Scale;
	const float ItemsTop = Menu.top + MenuItemsOffset(Item, Scale);

	for (size_t Index = 0; Index < Item.MenuItems.size(); Index++)
	{
		const float Top = MenuItemTop(Item, static_cast<int>(Index), Scale, ItemsTop);

		if (Point.y >= Top && Point.y < Top + ItemHeight)
			return MenuItemDisabled(Item, static_cast<int>(Index)) ? -1 : static_cast<int>(Index);
	}

	return -1;
}

rect PanelContent::ScrollTrackX(const UiNode& Item) const
{
	// The bar is pinned to the bottom of the visible part of the node: a tall section that runs past the
	// panel viewport keeps its slider on screen instead of parking it below the fold.
	const rect View = VisibleBounds(Item);
	const float TrackHeight = std::round(6.f * Scale);

	if (View.width() <= 1.f || View.height() <= TrackHeight)
		return rect();

	const float Bottom = std::min(Item.Bounds.bottom, View.bottom) - std::round(3.f * Scale);
	const float Top = std::max(View.top, Bottom - TrackHeight);
	const float Inset = std::round(4.f * Scale);
	const float Left = std::max(Item.Bounds.left, View.left) + Inset;
	const float Right = std::min(Item.Bounds.right, View.right) - Inset;

	if (Right <= Left || Bottom <= Top)
		return rect();

	return rect(Left, Top, Right, Bottom);
}

rect PanelContent::ScrollThumbX(const UiNode& Item) const
{
	const rect Track = ScrollTrackX(Item);

	if (Track.width() <= 1.f)
		return rect();

	const float Visible = Item.Bounds.width();
	const float ThumbWidth = std::min(Track.width(), std::max(24.f * Scale, Track.width() * Visible / (Visible + Item.ScrollMaxX)));
	const float Travel = std::max(0.f, Track.width() - ThumbWidth);
	const float Offset = Item.ScrollMaxX > 0.f ? (Item.ScrollX / Item.ScrollMaxX) * Travel : 0.f;
	const float Left = std::round(Track.left + Offset);
	return rect(Left, Track.top, Left + std::round(ThumbWidth), Track.bottom);
}

float PanelContent::InputScroll(const UiNode& Item) const
{
	const float Font = Item.Style.FontSize * Scale;
	const float Visible = Item.Bounds.width() - 18.f * Scale;
	const std::wstring Prefix = Item.Text.substr(0, static_cast<size_t>(std::clamp(Item.Caret, 0, static_cast<int>(Item.Text.size()))));
	return std::max(0.f, renderer::measure_text(Prefix.c_str(), Font).width() - Visible);
}

float PanelContent::PopupScrollMax(const UiNode& Item, rect Popup) const
{
	const float Total = static_cast<float>(Item.Options.size()) * DropdownRow * Scale;
	return std::max(0.f, Total - (Popup.height() - DropdownPad * Scale * 2.f));
}

bool PanelContent::PointVisible(int Node, vec2 Point) const
{
	for (int Parent = Nodes[Node].Parent; Valid(Parent); Parent = Nodes[Parent].Parent)
	{
		const UiNode& Ancestor = Nodes[Parent];

		if ((Ancestor.Style.Clip || Ancestor.Style.Scroll || Ancestor.Style.ScrollX) && !PointInRect(Ancestor.Bounds, Point))
			return false;
	}

	return true;
}

int PanelContent::HitTestNode(int Node, vec2 Point) const
{
	if (!Valid(Node))
		return -1;

	const UiNode& Item = Nodes[Node];

	if (!Item.Visible || !Item.Enabled)
		return -1;

	if ((Item.Style.Clip || Item.Style.Scroll || Item.Style.ScrollX) && !PointInRect(Item.Bounds, Point))
		return -1;

	for (size_t Index = Item.Children.size(); Index > 0; Index--)
	{
		const int Hit = HitTestNode(Item.Children[Index - 1], Point);

		if (Hit >= 0)
			return Hit;
	}

	if (IsInteractive(Item.Kind) && PointInRect(InteractiveRect(Item), Point))
		return Node;

	return -1;
}

int PanelContent::InteractiveAt(vec2 Point) const
{
	return HitTestNode(RootNode, Point);
}

int PanelContent::TooltipNode(int Node, vec2 Point) const
{
	if (!Valid(Node))
		return -1;

	const UiNode& Item = Nodes[Node];

	if (!Item.Visible)
		return -1;

	const bool Inside = PointInRect(Item.Bounds, Point);

	if ((Item.Style.Clip || Item.Style.Scroll || Item.Style.ScrollX) && !Inside)
		return -1;

	for (size_t Index = Item.Children.size(); Index > 0; Index--)
	{
		const int Hit = TooltipNode(Item.Children[Index - 1], Point);

		if (Hit >= 0)
			return Hit;
	}

	return Inside && !Item.Tooltip.empty() ? Node : -1;
}

int PanelContent::TooltipAt(vec2 Point) const
{
	return TooltipNode(RootNode, Point);
}

int PanelContent::ScrollbarAt(vec2 Point) const
{
	int Result = -1;
	float BestArea = 1e30f;

	for (int Node = 0; Node < static_cast<int>(Nodes.size()); Node++)
	{
		const UiNode& Item = Nodes[Node];

		if (!IsScrollable(Item) || (Item.ScrollMax <= 0.f && Item.ScrollMaxX <= 0.f) || !Item.Enabled || !HasArea(Item.Bounds))
			continue;

		const rect Track = ScrollTrack(Item);
		const rect Hit(Track.left - 6.f * Scale, Track.top, Item.Bounds.right, Track.bottom);
		const rect TrackX = ScrollTrackX(Item);
		const rect HitX(TrackX.left, TrackX.top - 6.f * Scale, TrackX.right, TrackX.bottom);

		const bool VerticalHit = Item.ScrollMax > 0.f && PointInRect(Hit, Point);
		const bool HorizontalHit = Item.ScrollMaxX > 0.f && TrackX.width() > 1.f && PointInRect(HitX, Point);

		if ((!VerticalHit && !HorizontalHit) || !PointVisible(Node, Point))
			continue;

		const float Area = Item.Bounds.width() * Item.Bounds.height();

		if (Area < BestArea)
		{
			BestArea = Area;
			Result = Node;
		}
	}

	return Result;
}

int PanelContent::FindScrollNode(vec2 Point) const
{
	int Result = -1;
	float BestArea = 1e30f;

	for (int Node = 0; Node < static_cast<int>(Nodes.size()); Node++)
	{
		const UiNode& Item = Nodes[Node];

		// Containers that cannot scroll must not swallow the wheel from a scrollable ancestor.
		if (!IsScrollable(Item) || (Item.ScrollMax <= 0.f && Item.ScrollMaxX <= 0.f) ||
			!PointInRect(Item.Bounds, Point) || !PointVisible(Node, Point))
			continue;

		const float Area = Item.Bounds.width() * Item.Bounds.height();

		if (Area < BestArea)
		{
			BestArea = Area;
			Result = Node;
		}
	}

	return Result;
}

void PanelContent::SetSliderFromPoint(UiNode& Item, float X)
{
	const float Span = std::max(1.f, Item.Bounds.width() - 12.f * Scale);
	const float Ratio = std::clamp((X - (Item.Bounds.left + 6.f * Scale)) / Span, 0.f, 1.f);
	const double Value = SnapValue(Item, Item.Min + (Item.Max - Item.Min) * Ratio);

	if (Value == Item.Value)
		return;

	Item.Value = Value;
	Invoke(Item.OnChange, Value);
}

void PanelContent::NotifyEdit(int Node)
{
	if (!Valid(Node) || !Nodes[Node].OnEdit)
		return;

	const std::wstring Value = Nodes[Node].Text;
	Invoke(Nodes[Node].OnEdit, Value);
}

void PanelContent::InsertText(UiNode& Item, const std::wstring& Value)
{
	const int Room = InputMaxLength - static_cast<int>(Item.Text.size());

	if (Room <= 0 || Value.empty())
		return;

	const size_t Count = std::min(Value.size(), static_cast<size_t>(Room));
	Item.Text.insert(static_cast<size_t>(Item.Caret), Value, 0, Count);
	Item.Caret += static_cast<int>(Count);
}

void PanelContent::Activate(int Node)
{
	UiNode& Item = Nodes[Node];

	switch (Item.Kind)
	{
	case UiNodeKind::Button:
		Invoke(Item.OnClick);
		break;
	case UiNodeKind::Checkbox:
	case UiNodeKind::Toggle:
	case UiNodeKind::Collapsible:
	{
		const bool Checked = !Item.Checked;
		Item.Checked = Checked;
		Invoke(Item.OnToggle, Checked);
		break;
	}
	case UiNodeKind::Radio:
	{
		if (Item.Checked)
			break;

		int Previous = -1;

		for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
		{
			if (Index != Node && Nodes[Index].Kind == UiNodeKind::Radio && Nodes[Index].RadioGroup == Item.RadioGroup && Nodes[Index].Checked)
			{
				Nodes[Index].Checked = false;
				Previous = Index;
			}
		}

		Item.Checked = true;

		if (Previous >= 0)
			Invoke(Nodes[Previous].OnToggle, false);

		if (Valid(Node))
			Invoke(Nodes[Node].OnToggle, true);

		break;
	}
	default:
		break;
	}
}

bool PanelContent::OnMouseDown(vec2 Point, int& DragNode, int& DragKind)
{
	DragNode = -1;
	DragKind = 0;

	// An open context menu swallows the next click; outside clicks just dismiss it. Choosing an item closes
	// the menu first, so the owner's handler may reopen it (options that toggle in place).
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (!Item.MenuOpen)
			continue;

		if (PointInRect(MenuRect(Item), Point))
		{
			const int Choice = MenuIndexAt(Item, Point);

			if (Choice >= 0)
			{
				std::function<void(int)> Select = Item.OnMenuSelect;
				Item.MenuOpen = false;
				Item.MenuHover = -1;

				if (Select)
					Select(Choice);
			}

			return true;
		}

		Item.MenuOpen = false;
		Item.MenuHover = -1;
	}

	// A press inside the cell being edited keeps editing; anywhere else commits the text.
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (Item.Kind != UiNodeKind::TableList || Item.EditRow < 0)
			continue;

		if (TableListRowAt(Item, Point) == Item.EditRow && TableListColumnAt(Item, Point) == Item.EditColumn && PointVisible(Index, Point))
			return true;

		CommitTableEdit(Index);
	}

	// Any press away from the menu finishes an inline edit; a pending complete value is written.
	for (UiNode& Item : Nodes)
	{
		if (Item.Kind == UiNodeKind::HexView && Item.HexEditing)
			HexEndEdit(Item, true);
	}

	// Hex dump: left press starts a selection in the column under the pointer; dragging extends it across
	// rows. A double-click on a value or character starts editing it. A press on the dump's scrollbar
	// belongs to the scrollbar even where the bar overlays the text column.
	const bool OnScrollbar = ScrollbarAt(Point) >= 0;

	for (int Index = 0; Index < static_cast<int>(Nodes.size()) && !OnScrollbar; Index++)
	{
		UiNode& Item = Nodes[Index];
		int Column = 1;
		uint64_t Address = 0;
		uint64_t Width = 1;

		if (Item.Kind != UiNodeKind::HexView || !Item.Visible || !HexHit(Item, Point, Column, Address, Width) ||
			!PointVisible(Index, Point))
			continue;

		const uint64_t Now = GetTickCount64();
		const bool DoubleClick = Index == LastHexClickNode && Column == LastHexClickColumn && Address == LastHexClickAddress &&
			Now - LastHexClickTime <= static_cast<uint64_t>(GetDoubleClickTime());

		LastHexClickNode = Index;
		LastHexClickColumn = Column;
		LastHexClickAddress = Address;
		LastHexClickTime = DoubleClick ? 0 : Now;

		ClearFocus();
		Focused = Index;
		Item.MenuOpen = false;
		Item.HexSelColumn = Column;
		Item.HexSelAnchor = Address;
		Item.HexSelStart = Address;
		Item.HexSelEnd = std::min(Address + Width, Item.HexSpan);
		Item.HexHasSelection = true;

		if (DoubleClick && Column != 0 && Item.HexWrite)
		{
			HexBeginEdit(Item, Column, Address);
			return true;
		}

		DragNode = Index;
		DragKind = 6;
		return true;
	}

	const int Bar = ScrollbarAt(Point);

	if (Bar >= 0)
	{
		ClearFocus();
		UiNode& Item = Nodes[Bar];
		const rect Thumb = ScrollThumb(Item);
		const rect TrackX = ScrollTrackX(Item);
		const rect ThumbX = ScrollThumbX(Item);

		// The horizontal bar sits at the bottom; anything above it is the vertical bar's hit strip.
		if (Item.ScrollMaxX > 0.f && TrackX.width() > 1.f && Point.y >= TrackX.top - 4.f * Scale)
		{
			if (Point.x >= ThumbX.left && Point.x < ThumbX.right)
			{
				ScrollGrab = Point.x - ThumbX.left;
			}
			else
			{
				ScrollGrab = ThumbX.width() * 0.5f;
				OnMouseMove(Point, Bar, 3);
			}

			DragNode = Bar;
			DragKind = 3;
			return true;
		}

		if (Point.y >= Thumb.top && Point.y < Thumb.bottom)
		{
			ScrollGrab = Point.y - Thumb.top;
		}
		else
		{
			ScrollGrab = Thumb.height() * 0.5f;
			OnMouseMove(Point, Bar, 2);
		}

		DragNode = Bar;
		DragKind = 2;
		return true;
	}

	// Table column dividers: pressing within 4 px of a header boundary starts a width drag.
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (Item.Kind != UiNodeKind::TableList || !Item.Visible || Item.ColumnWidths.size() < 2 || !HasArea(Item.Bounds))
			continue;

		const rect Header(Item.Bounds.left, Item.Bounds.top, Item.Bounds.right, Item.Bounds.top + TableHeader * Scale);
		if (!PointInRect(Header, Point) || !PointVisible(Index, Point))
			continue;

		float X = Item.Bounds.left;

		for (size_t Column = 0; Column + 1 < Item.ColumnWidths.size(); Column++)
		{
			X += Item.ColumnWidths[Column] * Scale;

			if (std::fabs(Point.x - X) > 4.f * Scale)
				continue;

			ClearFocus();
			DragNode = Index;
			DragKind = 5;
			ResizeTableNode = Index;
			ResizeTableColumn = static_cast<int>(Column);
			ResizeStartWidth = Item.ColumnWidths[Column];
			ResizeOrigin = Point;
			return true;
		}
	}

	// TableList cells: a click selects exactly one cell. Editing and navigation live in the owner's
	// right-click menu.
	for (int Index = 0; Index < static_cast<int>(Nodes.size()); Index++)
	{
		UiNode& Item = Nodes[Index];

		if (Item.Kind != UiNodeKind::TableList || !Item.Visible)
			continue;

		const int Row = TableListRowAt(Item, Point);
		const int Column = TableListColumnAt(Item, Point);

		if (Row < 0 || Column < 0 || !PointVisible(Index, Point))
			continue;

		ClearFocus();
		Focused = Index;
		Item.SelectedRow = Row;
		Item.SelectedColumn = Column;

		std::function<void(int, int)> Callback = Item.OnCellClick;

		if (Callback)
			Callback(Row, Column);

		return true;
	}

	const int Hit = InteractiveAt(Point);

	if (Hit < 0)
	{
		ClearFocus();
		return false;
	}

	UiNode& Item = Nodes[Hit];

	if (IsPressable(Item.Kind))
	{
		ClearFocus();
		Pressed = Hit;
		Item.Pressed = true;
		return true;
	}

	switch (Item.Kind)
	{
	case UiNodeKind::DragHandle:
		ClearFocus();
		DragNode = Hit;
		DragKind = 4;
		DragLast = Point;
		return true;
	case UiNodeKind::Slider:
		ClearFocus();
		DragNode = Hit;
		DragKind = 1;
		SetSliderFromPoint(Item, Point.x);
		return true;
	case UiNodeKind::Input:
	{
		const float Offset = Item.Focus ? InputScroll(Item) : 0.f;
		ClearFocus();
		Focused = Hit;
		Item.Focus = true;

		const float Font = Item.Style.FontSize * Scale;
		const float LocalX = Point.x - (Item.Bounds.left + 8.f * Scale) + Offset;
		std::wstring Prefix;
		Prefix.reserve(Item.Text.size());
		int Best = 0;
		float BestDistance = 1e9f;

		for (int Index = 0; Index <= static_cast<int>(Item.Text.size()); Index++)
		{
			Prefix.assign(Item.Text, 0, static_cast<size_t>(Index));
			const float Distance = std::fabs(renderer::measure_text(Prefix.c_str(), Font).width() - LocalX);

			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}

		Item.Caret = Best;
		return true;
	}
	case UiNodeKind::Dropdown:
	{
		ClearFocus();
		const bool WasOpen = Item.Open;
		CloseDropdown();
		Item.Open = !WasOpen;

		if (Item.Open)
			Item.PopupScroll = std::max(0.f, static_cast<float>(Item.Selected - DropdownMaxRows / 2) * DropdownRow * Scale);

		return true;
	}
	case UiNodeKind::ListBox:
	{
		ClearFocus();
		const float Local = Point.y - (Item.Bounds.top + ListPad * Scale) + Item.Scroll;
		const int Row = static_cast<int>(std::floor(Local / ListRowHeight()));

		if (Local >= 0.f && Row < static_cast<int>(Item.Options.size()) && Row != Item.Selected)
		{
			Item.Selected = Row;
			Invoke(Item.OnSelect, Row);
		}

		return true;
	}
	default:
		return false;
	}
}

void PanelContent::OnMouseMove(vec2 Point, int DragNode, int DragKind)
{
	for (UiNode& Item : Nodes)
	{
		if (Item.MenuOpen)
			Item.MenuHover = MenuIndexAt(Item, Point);
	}

	if (!Valid(DragNode))
		return;

	UiNode& Item = Nodes[DragNode];

	if (DragKind == 1)
	{
		SetSliderFromPoint(Item, Point.x);
	}
	else if (DragKind == 2 && Item.ScrollMax > 0.f)
	{
		const rect Track = ScrollTrack(Item);
		const rect Thumb = ScrollThumb(Item);
		const float Travel = std::max(1.f, Track.height() - Thumb.height());
		const float Offset = std::clamp(Point.y - ScrollGrab - Track.top, 0.f, Travel);
		Item.Scroll = Offset / Travel * Item.ScrollMax;
	}
	else if (DragKind == 3 && Item.ScrollMaxX > 0.f)
	{
		const rect Track = ScrollTrackX(Item);
		const rect Thumb = ScrollThumbX(Item);
		const float Travel = std::max(1.f, Track.width() - Thumb.width());
		const float Offset = std::clamp(Point.x - ScrollGrab - Track.left, 0.f, Travel);
		Item.ScrollX = Offset / Travel * Item.ScrollMaxX;
	}
	else if (DragKind == 4)
	{
		const float Delta = Item.ResizeVertical ? Point.x - DragLast.x : Point.y - DragLast.y;
		DragLast = Point;

		if (Delta != 0.f && Item.OnResize)
			Item.OnResize(Delta / (Scale > 0.f ? Scale : 1.f));
	}
	else if (DragKind == 5 && Item.Kind == UiNodeKind::TableList)
	{
		if (ResizeTableColumn < 0 || ResizeTableColumn >= static_cast<int>(Item.ColumnWidths.size()))
			return;

		const float Delta = (Point.x - ResizeOrigin.x) / (Scale > 0.f ? Scale : 1.f);
		Item.ColumnWidths[static_cast<size_t>(ResizeTableColumn)] = std::clamp(ResizeStartWidth + Delta, 24.f, 600.f);

		float Total = 0.f;
		for (float Width : Item.ColumnWidths)
			Total += Width;

		Item.Style.Width = UiSize::Fixed;
		Item.Style.WidthValue = Total;
	}
	else if (DragKind == 6 && Item.Kind == UiNodeKind::HexView)
	{
		int Column = Item.HexSelColumn;
		uint64_t Address = 0;
		uint64_t Width = 1;

		if (!Item.HexEditing && HexHit(Item, Point, Column, Address, Width, Item.HexSelColumn))
		{
			Item.HexSelStart = std::min(Item.HexSelAnchor, Address);
			Item.HexSelEnd = std::min(std::max(Item.HexSelAnchor, Address) + Width, Item.HexSpan);
			Item.HexHasSelection = true;
		}
	}
}

void PanelContent::OnMouseUp(vec2 Point)
{
	ResizeTableNode = -1;
	ResizeTableColumn = -1;

	if (!Valid(Pressed))
	{
		Pressed = -1;
		return;
	}

	const int Node = Pressed;
	Pressed = -1;
	Nodes[Node].Pressed = false;

	if (Nodes[Node].Enabled && PointInRect(InteractiveRect(Nodes[Node]), Point))
		Activate(Node);
}

void PanelContent::CancelInteraction()
{
	if (Valid(Pressed))
		Nodes[Pressed].Pressed = false;

	Pressed = -1;
}

bool PanelContent::OnWheel(vec2 Point, float Delta, bool Horizontal)
{
	const int Node = FindScrollNode(Point);

	if (Node < 0)
		return false;

	if (Horizontal)
	{
		for (int Current = Node; Valid(Current); Current = Nodes[Current].Parent)
		{
			UiNode& Candidate = Nodes[Current];

			if (Candidate.ScrollMaxX <= 0.f)
				continue;

			const float Step = 48.f * Scale;
			Candidate.ScrollX = std::clamp(Candidate.ScrollX - Delta * Step, 0.f, Candidate.ScrollMaxX);
			return true;
		}

		return false;
	}

	// A section may only scroll horizontally; the plain wheel must then scroll the panel behind it.
	for (int Current = Node; Valid(Current); Current = Nodes[Current].Parent)
	{
		UiNode& Candidate = Nodes[Current];

		if (Candidate.ScrollMax <= 0.f)
			continue;

		const float Step = Candidate.Kind == UiNodeKind::ListBox ? ListRowHeight() * 2.f : 48.f * Scale;
		Candidate.Scroll = std::clamp(Candidate.Scroll - Delta * Step, 0.f, Candidate.ScrollMax);
		return true;
	}

	return false;
}

bool PanelContent::OnChar(wchar_t Char)
{
	if (Valid(Focused) && Nodes[Focused].Kind == UiNodeKind::HexView)
	{
		// Enter, Escape and Backspace arrive through OnKey; here only typed characters are consumed.
		UiNode& Dump = Nodes[Focused];

		if (!Dump.HexEditing || Char < 32 || Char == 127)
			return true;

		if (Dump.HexEditColumn == 2)
		{
			std::vector<uint8_t> Bytes;

			if (Dump.HexUtf16)
				Bytes = { static_cast<uint8_t>(Char & 0xFF), static_cast<uint8_t>((Char >> 8) & 0xFF) };
			else if (Char <= 0xFF)
				Bytes = { static_cast<uint8_t>(Char) };
			else
				return true;

			std::function<void(uint64_t, const std::vector<uint8_t>&)> Write = Dump.HexWrite;
			Write(Dump.HexEditAddress, Bytes);

			if (!HexMoveEdit(Dump, 1))
				HexEndEdit(Dump, false);

			HexReveal(Dump, Scale);
			return true;
		}

		const bool Integer = Dump.HexKind == 1;
		const int Digits = HexCellDigits(Dump);
		const bool HexDigit = (Char >= L'0' && Char <= L'9') || (Char >= L'a' && Char <= L'f') || (Char >= L'A' && Char <= L'F');

		if (Integer && Char == L' ')
		{
			// Space confirms a decimal value and moves on, like a complete hex cell does.
			if (HexCommitEdit(Dump) && !HexMoveEdit(Dump, 1))
				HexEndEdit(Dump, false);

			HexReveal(Dump, Scale);
			return true;
		}

		const bool Accept = Integer
			? (Char >= L'0' && Char <= L'9') || (Char == L'-' && Dump.HexSigned && Dump.HexEditText.empty())
			: HexDigit;

		if (!Accept || static_cast<int>(Dump.HexEditText.size()) >= Digits)
			return true;

		Dump.HexEditText += static_cast<wchar_t>(towupper(Char));

		// A full hex cell is written immediately and typing continues in the next cell.
		if (!Integer && static_cast<int>(Dump.HexEditText.size()) == Digits)
		{
			if (HexCommitEdit(Dump) && !HexMoveEdit(Dump, 1))
				HexEndEdit(Dump, false);

			HexReveal(Dump, Scale);
		}

		return true;
	}

	if (Valid(Focused) && Nodes[Focused].Kind == UiNodeKind::TableList)
	{
		UiNode& Table = Nodes[Focused];

		if (Table.EditRow < 0)
			return false;

		Table.EditCaret = std::clamp(Table.EditCaret, 0, static_cast<int>(Table.EditText.size()));

		if (Char == L'\b')
		{
			if (Table.EditCaret > 0)
			{
				Table.EditText.erase(static_cast<size_t>(Table.EditCaret - 1), 1);
				Table.EditCaret--;
			}
		}
		else if (Char == L'\r' || Char == L'\n')
		{
			CommitTableEdit(Focused);
		}
		else if (Char == 27)
		{
			CancelTableEdit(Focused);
		}
		else if (Char >= 32)
		{
			Table.EditText.insert(static_cast<size_t>(Table.EditCaret), 1, Char);
			Table.EditCaret++;
		}

		return true;
	}

	if (!Valid(Focused))
		return false;

	const int Node = Focused;
	UiNode& Item = Nodes[Node];
	Item.Caret = std::clamp(Item.Caret, 0, static_cast<int>(Item.Text.size()));

	if (Char == L'\b')
	{
		if (Item.Caret > 0)
		{
			Item.Text.erase(static_cast<size_t>(Item.Caret - 1), 1);
			Item.Caret--;
			NotifyEdit(Node);
		}

		return true;
	}

	if (Char == L'\r')
	{
		const std::function<void(const std::wstring&)> Submit = Item.OnSubmit;
		const std::wstring Value = Item.Text;
		ClearFocus();

		if (Submit)
			Submit(Value);

		return true;
	}

	if (Char == 27)
	{
		ClearFocus();
		return true;
	}

	if (Char < 32)
		return true;

	const size_t Before = Item.Text.size();
	InsertText(Item, std::wstring(1, Char));

	if (Item.Text.size() != Before)
		NotifyEdit(Node);

	return true;
}

bool PanelContent::OnKey(int Key)
{
	if (Key == VK_ESCAPE && OpenMenuNode() >= 0)
	{
		CloseMenus();
		return true;
	}

	if (Valid(Focused) && Nodes[Focused].Kind == UiNodeKind::HexView)
	{
		UiNode& Dump = Nodes[Focused];
		const bool Control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

		if (Control && Key == 'C')
		{
			HexCopySelection(Focused);
			return true;
		}

		if (!Dump.HexEditing)
			return false;

		switch (Key)
		{
		case VK_ESCAPE:
			HexEndEdit(Dump, false);
			return true;
		case VK_RETURN:
			HexEndEdit(Dump, true);
			return true;
		case VK_BACK:
			// Backspace edits the pending digits first, then steps back a cell without writing.
			if (!Dump.HexEditText.empty())
				Dump.HexEditText.pop_back();
			else
				HexMoveEdit(Dump, -1);

			HexReveal(Dump, Scale);
			return true;
		case VK_LEFT:
		case VK_RIGHT:
			HexMoveEdit(Dump, Key == VK_LEFT ? -1 : 1);
			HexReveal(Dump, Scale);
			return true;
		case VK_UP:
		case VK_DOWN:
		{
			const int64_t Width = Dump.HexEditColumn == 1 ? HexUnitBytes(Dump) : Dump.HexUtf16 ? 2 : 1;
			HexMoveEdit(Dump, (Key == VK_UP ? -16 : 16) / Width);
			HexReveal(Dump, Scale);
			return true;
		}
		default:
			return true;
		}
	}

	if (Valid(Focused) && Nodes[Focused].Kind == UiNodeKind::TableList)
	{
		UiNode& Table = Nodes[Focused];

		// F2 edits the selected cell when the owner allows it.
		if (Table.EditRow < 0)
			return Key == VK_F2 && BeginCellEdit(Focused, Table.SelectedRow, Table.SelectedColumn);

		const int Length = static_cast<int>(Table.EditText.size());
		Table.EditCaret = std::clamp(Table.EditCaret, 0, Length);

		switch (Key)
		{
		case VK_LEFT:
			Table.EditCaret = std::max(0, Table.EditCaret - 1);
			return true;
		case VK_RIGHT:
			Table.EditCaret = std::min(Length, Table.EditCaret + 1);
			return true;
		case VK_HOME:
			Table.EditCaret = 0;
			return true;
		case VK_END:
			Table.EditCaret = Length;
			return true;
		case VK_DELETE:
			if (Table.EditCaret < Length)
				Table.EditText.erase(static_cast<size_t>(Table.EditCaret), 1);
			return true;
		case VK_RETURN:
			CommitTableEdit(Focused);
			return true;
		case VK_ESCAPE:
			CancelTableEdit(Focused);
			return true;
		default:
			return false;
		}
	}

	if (!Valid(Focused))
		return false;

	const int Node = Focused;
	UiNode& Item = Nodes[Node];
	const int Length = static_cast<int>(Item.Text.size());
	const bool Control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	Item.Caret = std::clamp(Item.Caret, 0, Length);

	switch (Key)
	{
	case VK_LEFT:
		Item.Caret = std::max(0, Item.Caret - 1);
		return true;
	case VK_RIGHT:
		Item.Caret = std::min(Length, Item.Caret + 1);
		return true;
	case VK_HOME:
		Item.Caret = 0;
		return true;
	case VK_END:
		Item.Caret = Length;
		return true;
	case VK_DELETE:
		if (Item.Caret < Length)
		{
			Item.Text.erase(static_cast<size_t>(Item.Caret), 1);
			NotifyEdit(Node);
		}
		return true;
	case VK_RETURN:
		return OnChar(L'\r');
	case VK_ESCAPE:
		ClearFocus();
		return true;
	case 'V':
	{
		if (!Control)
			return false;

		std::wstring Pasted;

		if (OpenClipboard(GetActiveWindow()))
		{
			if (HANDLE Data = GetClipboardData(CF_UNICODETEXT))
			{
				if (const wchar_t* Source = static_cast<const wchar_t*>(GlobalLock(Data)))
				{
					for (const wchar_t* Cursor = Source; *Cursor; Cursor++)
						Pasted.push_back(*Cursor == L'\t' ? L' ' : *Cursor);

					GlobalUnlock(Data);
				}
			}

			CloseClipboard();
		}

		Pasted.erase(std::remove_if(Pasted.begin(), Pasted.end(), [](wchar_t Char) { return Char < 32; }), Pasted.end());
		const size_t Before = Item.Text.size();
		InsertText(Item, Pasted);

		if (Item.Text.size() != Before)
			NotifyEdit(Node);

		return true;
	}
	case 'C':
	{
		if (!Control)
			return false;

		if (OpenClipboard(GetActiveWindow()))
		{
			EmptyClipboard();
			const size_t Bytes = (Item.Text.size() + 1) * sizeof(wchar_t);

			if (HGLOBAL Memory = GlobalAlloc(GMEM_MOVEABLE, Bytes))
			{
				if (void* Target = GlobalLock(Memory))
				{
					std::memcpy(Target, Item.Text.c_str(), Bytes);
					GlobalUnlock(Memory);

					if (!SetClipboardData(CF_UNICODETEXT, Memory))
						GlobalFree(Memory);
				}
				else
				{
					GlobalFree(Memory);
				}
			}

			CloseClipboard();
		}

		return true;
	}
	default:
		return false;
	}
}

void PanelContent::ClearFocus()
{
	if (Valid(Focused))
		Nodes[Focused].Focus = false;

	Focused = -1;
}

int PanelContent::OpenNode() const
{
	for (int Node = 0; Node < static_cast<int>(Nodes.size()); Node++)
	{
		if (Nodes[Node].Kind == UiNodeKind::Dropdown && Nodes[Node].Open)
			return Node;
	}

	return -1;
}

rect PanelContent::DropdownPopup(int Node, rect Limit) const
{
	if (!Valid(Node))
		return rect();

	const UiNode& Item = Nodes[Node];
	const float ItemHeight = DropdownRow * Scale;
	const float Padding = DropdownPad * Scale;
	const float Edge = 4.f * Scale;
	const float Width = std::max(Item.Bounds.width(), 150.f * Scale);
	const int Rows = std::min(static_cast<int>(Item.Options.size()), DropdownMaxRows);
	const float Height = std::min(static_cast<float>(Rows) * ItemHeight + Padding * 2.f,
		std::max(ItemHeight + Padding * 2.f, Limit.height() - Edge * 2.f));

	const float Left = std::clamp(Item.Bounds.left, Limit.left + Edge, std::max(Limit.left + Edge, Limit.right - Width - Edge));
	float Top = Item.Bounds.bottom + 2.f * Scale;

	if (Top + Height > Limit.bottom - Edge)
		Top = Item.Bounds.top - Height - 2.f * Scale;

	Top = std::clamp(Top, Limit.top + Edge, std::max(Limit.top + Edge, Limit.bottom - Height - Edge));
	return rect(Left, Top, Left + Width, Top + Height);
}

int PanelContent::DropdownOptionAt(int Node, vec2 Point, rect Limit) const
{
	if (!Valid(Node))
		return -1;

	const UiNode& Item = Nodes[Node];
	const rect Popup = DropdownPopup(Node, Limit);
	const float Padding = DropdownPad * Scale;

	if (!PointInRect(rect(Popup.left, Popup.top + Padding, Popup.right, Popup.bottom - Padding), Point))
		return -1;

	const float Scroll = std::clamp(Item.PopupScroll, 0.f, PopupScrollMax(Item, Popup));
	const int Index = static_cast<int>(std::floor((Point.y - (Popup.top + Padding) + Scroll) / (DropdownRow * Scale)));

	if (Index < 0 || Index >= static_cast<int>(Item.Options.size()))
		return -1;

	return Index;
}

bool PanelContent::ScrollDropdown(int Node, float Delta, rect Limit)
{
	if (!Valid(Node) || !Nodes[Node].Open)
		return false;

	UiNode& Item = Nodes[Node];
	const float Max = PopupScrollMax(Item, DropdownPopup(Node, Limit));
	Item.PopupScroll = std::clamp(std::clamp(Item.PopupScroll, 0.f, Max) - Delta * DropdownRow * Scale * 2.f, 0.f, Max);
	return true;
}

void PanelContent::SelectOption(int Node, int Option)
{
	if (!Valid(Node))
		return;

	UiNode& Item = Nodes[Node];

	if (Option < 0 || Option >= static_cast<int>(Item.Options.size()))
		return;

	Item.Selected = Option;
	Item.Open = false;
	Invoke(Item.OnSelect, Option);
}

void PanelContent::CloseDropdown()
{
	for (UiNode& Item : Nodes)
		Item.Open = false;
}

void PanelContent::DrawShapes(const UiTheme& Theme, float ScaleValue, vec2 Mouse, bool HoverAllowed) const
{
	if (Valid(RootNode))
		DrawShapesNode(RootNode, Theme, ScaleValue, Mouse, HoverAllowed, false);
}

void PanelContent::DrawScrollbar(const UiNode& Item, const UiTheme& Theme, float ScaleValue) const
{
	// Overlay-style scrollbars: no visible track, and a thumb quiet enough not to compete with content.
	if (Item.ScrollMax > 0.f && Item.Bounds.width() > 12.f * ScaleValue)
	{
		const rect Track = ScrollTrack(Item);
		renderer::draw_filled_rounded_rect(ScrollThumb(Item), Track.width() * 0.5f, WithAlpha(Theme.Muted, 0.45f));
	}

	if (Item.ScrollMaxX > 0.f && Item.Bounds.height() > 12.f * ScaleValue)
	{
		const rect Track = ScrollTrackX(Item);

		if (Track.width() > 1.f)
			renderer::draw_filled_rounded_rect(ScrollThumbX(Item), Track.height() * 0.5f, WithAlpha(Theme.Muted, 0.45f));
	}
}

void PanelContent::DrawShapesNode(int Node, const UiTheme& Theme, float ScaleValue, vec2 Mouse, bool HoverAllowed, bool Disabled) const
{
	if (!Valid(Node))
		return;

	const UiNode& Item = Nodes[Node];

	if (!Item.Visible || !HasArea(Item.Bounds))
		return;

	const bool Off = Disabled || !Item.Enabled;
	const auto Tint = [Off](color Value) { return Off ? WithAlpha(Value, 0.45f) : Value; };
	const bool Hovered = HoverAllowed && !Off && IsInteractive(Item.Kind) && PointInRect(InteractiveRect(Item), Mouse);
	const float Radius = Item.Style.Radius * ScaleValue;
	const float Line = Item.Style.BorderThickness * ScaleValue;
	const float Pad = Item.Style.Padding * ScaleValue;
	const vec2 Center = Item.Bounds.center();

	switch (Item.Kind)
	{
	case UiNodeKind::Container:
		if (Item.Style.Background.Set)
			renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Panel)));

		if (Item.Style.Border.Set)
			renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));
		break;
	case UiNodeKind::Rect:
		renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Header)));

		if (Item.Style.Border.Set)
			renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));
		break;
	case UiNodeKind::Button:
	{
		color Background = ResolvePaint(Item.Style.Background, Theme, UiColorRole::Header);

		if (Item.Pressed)
			Background = Theme.AccentSoft;
		else if (Hovered)
			Background = Theme.TabHover;

		renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(Background));
		renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));

		if (Item.IconGlyph != UiIcon::None)
		{
			const float IconSize = ButtonIconSize(Item, ScaleValue);
			const float Left = ButtonContentLeft(Item, ScaleValue);
			const float Top = Item.Bounds.center().y - IconSize * 0.5f;
			DrawUiIcon(Item.IconGlyph, rect(Left, Top, Left + IconSize, Top + IconSize),
				Tint(Item.Enabled ? Theme.Text : WithAlpha(Theme.Muted, 0.45f)));
		}
		break;
	}
	case UiNodeKind::Input:
	{
		const color Border = Item.Focus ? Theme.Accent : (Hovered ? Theme.Muted : ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border));
		renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Workspace)));
		renderer::draw_rounded_rect(Item.Bounds, Radius, std::max(1.f, Line), Tint(Border));
		break;
	}
	case UiNodeKind::Dropdown:
	{
		const color Background = Hovered || Item.Open ? Theme.TabHover : ResolvePaint(Item.Style.Background, Theme, UiColorRole::Header);
		renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(Background));
		renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(Item.Open ? Theme.Accent : ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));

		// Icon-set chevron (one polyline with a proper join); two separately capped segments overlapped
		// at the tip and doubled its alpha when the control is disabled.
		const float Glyph = 16.f * ScaleValue;
		const vec2 Chevron(std::round(Item.Bounds.right - 16.f * ScaleValue), std::round(Center.y));
		DrawUiIcon(UiIcon::ChevronDown, rect(Chevron.x - Glyph * 0.5f, Chevron.y - Glyph * 0.5f, Chevron.x + Glyph * 0.5f, Chevron.y + Glyph * 0.5f),
			Tint(Theme.Muted), 1.4f * ScaleValue);
		break;
	}
	case UiNodeKind::Slider:
	case UiNodeKind::Progress:
	{
		// Label row on top (drawn by DrawTextNode), track pinned to the bottom so the two never overlap.
		const bool Slider = Item.Kind == UiNodeKind::Slider;
		const float TrackY = std::round(Item.Bounds.bottom - (Slider ? 9.f : 7.f) * ScaleValue);
		const float Half = (Slider ? 2.f : 3.f) * ScaleValue;
		const rect Track(Item.Bounds.left + 6.f * ScaleValue, TrackY - Half, Item.Bounds.right - 6.f * ScaleValue, TrackY + Half);
		const float FilledRight = Track.left + Track.width() * ValueRatio(Item);

		renderer::draw_filled_rounded_rect(Track, Half, Tint(UiMix(Theme.Border, Theme.Muted, 0.12f)));

		if (FilledRight > Track.left + 0.5f)
			renderer::draw_filled_rounded_rect(rect(Track.left, Track.top, FilledRight, Track.bottom), Half, Tint(Theme.Accent));

		if (Slider)
		{
			// Accent disc inside a panel-coloured rim (two fills, no stroke), centred on the track's own
			// pixel row: the knob used to sit half a pixel below the track it rides on.
			const float Knob = (Hovered ? 7.f : 6.f) * ScaleValue;
			const vec2 Centre(FilledRight, TrackY);
			renderer::draw_filled_circle(Centre, Knob, Tint(Opaque(Theme.Panel)));
			renderer::draw_filled_circle(Centre, std::max(Knob - 1.5f * ScaleValue, 0.5f), Tint(Theme.Accent));
		}
		break;
	}
	case UiNodeKind::Checkbox:
	{
		const float Size = CheckSize * ScaleValue;
		const float Top = std::round(Center.y - Size * 0.5f);
		const rect Box(Item.Bounds.left + Pad, Top, Item.Bounds.left + Pad + Size, Top + Size);
		const float Corner = 4.f * ScaleValue;

		if (Item.Checked)
			renderer::draw_filled_rounded_rect(Box, Corner, Tint(Theme.Accent));
		else
			UiFramedRoundedRect(Box, Corner, std::max(1.f, std::round(ScaleValue)), Tint(Hovered ? Theme.Accent : ControlEdge(Theme)), Theme.Workspace);

		if (Item.Checked)
		{
			const color Mark = Tint(color(1.f, 1.f, 1.f, 1.f));
			const vec2 A(Box.left + Size * 0.24f, Box.top + Size * 0.52f);
			const vec2 B(Box.left + Size * 0.42f, Box.top + Size * 0.70f);
			const vec2 C(Box.left + Size * 0.76f, Box.top + Size * 0.32f);
			renderer::draw_rounded_line(A, B, 1.7f * ScaleValue, Mark);
			renderer::draw_rounded_line(B, C, 1.7f * ScaleValue, Mark);
		}
		break;
	}
	case UiNodeKind::Radio:
	{
		// The ring is two filled circles rather than a stroked circle: a 1.5 px band is only ~6 samples
		// wide at 4x, so multisampling breaks it into uneven segments on the diagonals, while filled
		// circles give a single clean silhouette per edge.
		const float Ring = CheckSize * 0.5f * ScaleValue;
		const float Stroke = RingStroke * ScaleValue;
		const vec2 Dot = PixelCentre(vec2(Item.Bounds.left + Pad + Ring, Center.y));
		const color Ink = Tint(Item.Checked || Hovered ? Theme.Accent : ControlEdge(Theme));

		renderer::draw_filled_circle(Dot, Ring, Ink);

		// Checked: a solid accent disc with a light centre. The old ring + gap + dot packed three
		// concentric edges into 8 px of radius, which read as a muddy target rather than a selection.
		if (Item.Checked)
			renderer::draw_filled_circle(Dot, Ring * 0.38f, Tint(color(1.f, 1.f, 1.f, 1.f)));
		else
			renderer::draw_filled_circle(Dot, std::max(Ring - Stroke, 0.5f), Theme.Workspace);
		break;
	}
	case UiNodeKind::Toggle:
	{
		const float Height = ToggleHeight * ScaleValue;
		const float Top = std::round(Center.y - Height * 0.5f);
		const rect Track(Item.Bounds.left + Pad, Top, Item.Bounds.left + Pad + ToggleWidth * ScaleValue, Top + Height);
		const float KnobY = Track.center().y;

		if (Item.Checked)
			renderer::draw_filled_rounded_rect(Track, Height * 0.5f, Tint(Theme.Accent));
		else
			UiFramedRoundedRect(Track, Height * 0.5f, std::max(1.f, std::round(ScaleValue)), Tint(Hovered ? Theme.Accent : ControlEdge(Theme)), Theme.Workspace);

		const float Knob = Height * 0.5f - 4.f * ScaleValue;
		const float KnobX = Item.Checked ? Track.right - Height * 0.5f : Track.left + Height * 0.5f;
		renderer::draw_filled_circle(vec2(KnobX, KnobY), Knob, Tint(Item.Checked ? color(1.f, 1.f, 1.f, 1.f) : Theme.Muted));
		break;
	}
	case UiNodeKind::Separator:
	{
		const float Thickness = std::max(1.f, ScaleValue);

		if (Item.Bounds.width() >= Item.Bounds.height())
			renderer::draw_filled_rect(rect(Item.Bounds.left, Center.y - Thickness * 0.5f, Item.Bounds.right, Center.y + Thickness * 0.5f), Tint(Theme.Border));
		else
			renderer::draw_filled_rect(rect(Center.x - Thickness * 0.5f, Item.Bounds.top, Center.x + Thickness * 0.5f, Item.Bounds.bottom), Tint(Theme.Border));
		break;
	}
	case UiNodeKind::Image:
		if (Item.Texture)
		{
			const vec2 Size = renderer::texture_size(Item.Texture);
			const color TintColor = Item.Style.TextColor.Set
				? Tint(ResolvePaint(Item.Style.TextColor, Theme, UiColorRole::Text))
				: Tint(color(1.f, 1.f, 1.f, 1.f));

			// Cover grows past the bounds, so it is clipped; the other modes stay inside.
			if (Item.Fit == UiImageFit::Cover)
				renderer::push_clip(Item.Bounds);

			renderer::draw_image(Item.Texture, FitImage(Item.Bounds, Size, Item.Fit), TintColor);

			if (Item.Fit == UiImageFit::Cover)
				renderer::pop_clip();
		}
		else
		{
			renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(Theme.Header));
			renderer::draw_rounded_rect(Item.Bounds, Radius, 1.f * ScaleValue, Tint(Theme.Border));
		}
		break;
	case UiNodeKind::Icon:
		DrawUiIcon(Item.IconGlyph, Item.Bounds, Tint(Theme.Text));
		break;
	case UiNodeKind::ListBox:
	{
		renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Workspace)));
		renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));

		const float Row = ListRowHeight();
		const float Inset = ListPad * ScaleValue;
		const rect View(Item.Bounds.left + 1.f, Item.Bounds.top + 1.f, Item.Bounds.right - 1.f, Item.Bounds.bottom - 1.f);
		const int First = std::max(0, static_cast<int>(Item.Scroll / Row));
		const int Last = std::min(static_cast<int>(Item.Options.size()), First + static_cast<int>(Item.Bounds.height() / Row) + 2);
		const float RowRight = Item.Bounds.right - Inset - (Item.ScrollMax > 0.f ? 10.f * ScaleValue : 0.f);

		renderer::push_clip(View);

		for (int Index = First; Index < Last; Index++)
		{
			const float Top = Item.Bounds.top + Inset + static_cast<float>(Index) * Row - Item.Scroll;
			const rect RowRect(Item.Bounds.left + Inset, Top, RowRight, Top + Row);

			if (Index == Item.Selected)
				renderer::draw_filled_rounded_rect(RowRect, 4.f * ScaleValue, Tint(Theme.AccentSoft));
			else if (HoverAllowed && !Off && PointInRect(RowRect, Mouse) && PointInRect(View, Mouse))
				renderer::draw_filled_rounded_rect(RowRect, 4.f * ScaleValue, Theme.TabHover);
		}

		renderer::pop_clip();
		DrawScrollbar(Item, Theme, ScaleValue);
		break;
	}
	case UiNodeKind::TableList:
	{
		// Sticky header, striped body. Only rows crossing the viewport are painted.
		const float RowPx = Item.RowHeight * ScaleValue;
		const float HeaderPx = TableHeader * ScaleValue;
		const rect View = Item.Bounds;
		const float BodyTop = View.top + HeaderPx;
		const float HeaderBottom = std::min(View.bottom, BodyTop);
		// A full-width table stretches its last column to the node's edge instead of leaving dead space.
		const std::vector<float> Widths = TableWidths(Item);
		const color Stripe = Opaque(UiMix(Theme.Panel, Theme.Text, 0.045f));

		renderer::push_clip(View);
		renderer::draw_filled_rect(rect(View.left, View.top, View.right, HeaderBottom), Tint(Theme.Header));
		renderer::draw_filled_rect(rect(View.left, HeaderBottom - std::max(1.f, std::round(ScaleValue)), View.right, HeaderBottom),
			Tint(Theme.Border));

		{
			float SeparatorX = View.left;
			const float Hair = std::max(1.f, std::round(ScaleValue));

			for (size_t Column = 0; Column + 1 < Widths.size(); Column++)
			{
				SeparatorX += Widths[Column] * ScaleValue;

				if (SeparatorX <= View.left || SeparatorX >= View.right)
					continue;

				const float X = std::round(SeparatorX);
				renderer::draw_filled_rect(rect(X - Hair * 0.5f, View.top, X + Hair * 0.5f, HeaderBottom), Tint(Theme.Border));
			}
		}

		if (BodyTop < View.bottom && RowPx > 0.f)
		{
			const int First = std::max(0, static_cast<int>(std::floor(Item.Scroll / RowPx)));

			for (int Index = First; Index < static_cast<int>(Item.Rows.size()); Index++)
			{
				const float Top = BodyTop + static_cast<float>(Index) * RowPx - Item.Scroll;
				if (Top >= View.bottom)
					break;

				const rect RowRect(View.left, std::max(BodyTop, Top), View.right, std::min(View.bottom, Top + RowPx));

				if (Index & 1)
					renderer::draw_filled_rect(RowRect, Stripe);

				// Single-cell selection: only the clicked cell carries the tint.
				if (Index == Item.SelectedRow && Item.SelectedColumn >= 0 &&
					Item.SelectedColumn < static_cast<int>(Widths.size()))
				{
					float CellX = View.left;

					for (int Column = 0; Column < Item.SelectedColumn; Column++)
						CellX += Widths[static_cast<size_t>(Column)] * ScaleValue;

					const float Width = Widths[static_cast<size_t>(Item.SelectedColumn)] * ScaleValue;
					renderer::draw_filled_rect(rect(CellX, RowRect.top, CellX + Width, RowRect.bottom), Tint(Theme.AccentSoft));
				}

				// The cell being edited gets a dashed outline (style, not colour, of the reference).
				if (Index == Item.EditRow && Item.EditColumn >= 0 && Item.EditColumn < static_cast<int>(Widths.size()))
				{
					float CellX = View.left;

					for (int Column = 0; Column < Item.EditColumn; Column++)
						CellX += Widths[static_cast<size_t>(Column)] * ScaleValue;

					const float Width = Widths[static_cast<size_t>(Item.EditColumn)] * ScaleValue;
					DrawDashedRect(rect(CellX, RowRect.top, CellX + Width, RowRect.bottom), ScaleValue, Tint(Theme.Accent));
				}
			}
		}

		renderer::pop_clip();
		break;
	}
	case UiNodeKind::HexView:
	{
		const float RowPx = Item.RowHeight * ScaleValue;
		const uint64_t Rows = HexRowCount(Item);
		const uint64_t First = HexFirstRow(Item, RowPx);
		const uint64_t VisibleRows = static_cast<uint64_t>(std::max(0.f, std::floor(Item.Bounds.height() / RowPx)));
		const uint64_t Last = std::min(Rows, First + VisibleRows);

		renderer::push_clip(Item.Bounds);

		const HexGeometry Layout = HexLayout(Item, Item.Bounds.left, ScaleValue);
		const uint64_t Unit = static_cast<uint64_t>(Layout.Unit);
		const uint64_t Stride = Item.HexUtf16 ? 2 : 1;
		const float HalfChar = Layout.CharWidth * 0.5f;

		// The selection is highlighted only in the column it was made in.
		if (Item.HexHasSelection && Item.HexSelEnd > Item.HexSelStart)
		{
			const color Fill = Tint(Theme.AccentSoft);
			const float AddressRight = Layout.AddressX +
				static_cast<float>(Item.HexAddressChars > 0 ? Item.HexAddressChars : 8) * Layout.CharWidth;

			for (uint64_t Index = std::max(First, Item.HexSelStart / 16); Index < Last; Index++)
			{
				const uint64_t RowStart = Index * 16;
				const uint64_t Low = std::max(Item.HexSelStart, RowStart);
				const uint64_t High = std::min(Item.HexSelEnd, RowStart + 16);

				if (Low >= High)
					break;

				const float Top = Item.Bounds.top + static_cast<float>(Index - First) * RowPx;

				if (Item.HexSelColumn == 0)
				{
					renderer::draw_filled_rect(rect(Layout.AddressX - HalfChar, Top, AddressRight + HalfChar, Top + RowPx), Fill);
				}
				else if (Item.HexSelColumn == 1)
				{
					const float FirstCell = static_cast<float>((Low - RowStart) / Unit);
					const float LastCell = static_cast<float>((High - 1 - RowStart) / Unit);

					renderer::draw_filled_rect(rect(Layout.ValuesX + FirstCell * Layout.CellWidth - HalfChar, Top,
						Layout.ValuesX + LastCell * Layout.CellWidth + Layout.DigitsWidth + HalfChar, Top + RowPx), Fill);
				}
				else
				{
					const float FirstChar = static_cast<float>((Low - RowStart) / Stride);
					const float EndChar = static_cast<float>((High - 1 - RowStart) / Stride + 1);

					renderer::draw_filled_rect(rect(Layout.TextX + FirstChar * Layout.CharWidth, Top,
						Layout.TextX + EndChar * Layout.CharWidth, Top + RowPx), Fill);
				}
			}
		}

		// The cell being typed into gets the dashed outline used for renames elsewhere.
		if (Item.HexEditing)
		{
			const uint64_t Row = Item.HexEditAddress / 16;

			if (Row >= First && Row < Last)
			{
				const float Top = Item.Bounds.top + static_cast<float>(Row - First) * RowPx;
				const uint64_t Offset = Item.HexEditAddress % 16;
				rect Box;

				if (Item.HexEditColumn == 1)
				{
					const float Left = Layout.ValuesX + static_cast<float>(Offset / Unit) * Layout.CellWidth;
					Box = rect(Left - HalfChar, Top, Left + Layout.DigitsWidth + HalfChar, Top + RowPx);
				}
				else
				{
					const float Left = Layout.TextX + static_cast<float>(Offset / Stride) * Layout.CharWidth;
					Box = rect(Left - 1.f * ScaleValue, Top, Left + Layout.CharWidth + 1.f * ScaleValue, Top + RowPx);
				}

				DrawDashedRect(Box, ScaleValue, Tint(Theme.Accent));
			}
		}

		renderer::pop_clip();
		break;
	}
	case UiNodeKind::Collapsible:
	{
		const rect Header = InteractiveRect(Item);

		// Flat section header: no card, no outline. A hover wash and a hairline rule below the header
		// carry the structure, so the section reads as part of the panel.
		if (Hovered)
			renderer::draw_filled_rect(Header, Tint(Theme.TabHover));
		else if (Item.Style.Background.Set)
			renderer::draw_filled_rect(Item.Bounds, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Panel)));

		const float Rule = std::max(1.f, std::round(ScaleValue));
		renderer::draw_filled_rect(rect(Item.Bounds.left, Header.bottom - Rule, Item.Bounds.right, Header.bottom),
			Tint(WithAlpha(Theme.Border, 0.75f)));

		if (Item.Style.Border.Set)
			renderer::draw_rect(Item.Bounds, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));

		const float Glyph = 14.f * ScaleValue;
		const vec2 Chevron(std::round(Header.left + 8.f * ScaleValue), std::round(Header.center().y));
		DrawUiIcon(Item.Checked ? UiIcon::ChevronDown : UiIcon::ChevronRight,
			rect(Chevron.x - Glyph * 0.5f, Chevron.y - Glyph * 0.5f, Chevron.x + Glyph * 0.5f, Chevron.y + Glyph * 0.5f),
			Tint(Hovered ? Theme.Text : Theme.Muted), 1.4f * ScaleValue);
		break;
	}
	case UiNodeKind::DragHandle:
	{
		const float Hair = std::max(1.f, std::round(ScaleValue));
		const color ResizeTint = Hovered ? Theme.Accent : WithAlpha(Theme.Border, 0.65f);

		if (Item.ResizeVertical)
		{
			const float X = std::round(Item.Bounds.center().x);
			renderer::draw_filled_rect(rect(X - Hair * 0.5f, Item.Bounds.top, X + Hair * 0.5f, Item.Bounds.bottom), ResizeTint);
		}
		else
		{
			const float Y = std::round(Item.Bounds.center().y);
			renderer::draw_filled_rect(rect(Item.Bounds.left, Y - Hair * 0.5f, Item.Bounds.right, Y + Hair * 0.5f), ResizeTint);
		}
		break;
	}
	default:
		break;
	}

	const bool ClipChildren = Item.Style.Clip || Item.Style.Scroll || Item.Style.ScrollX;
	const bool ChildHover = HoverAllowed && (!ClipChildren || PointInRect(Item.Bounds, Mouse));

	if (ClipChildren)
		renderer::push_clip(Item.Bounds);

	for (int Child : Item.Children)
		DrawShapesNode(Child, Theme, ScaleValue, Mouse, ChildHover, Off);

	if (ClipChildren)
		renderer::pop_clip();
}

void PanelContent::SetOccluders(const std::vector<rect>& Rects)
{
	Occluders = Rects;
}

bool PanelContent::Occluded(rect Area) const
{
	for (const rect& Cut : Occluders)
	{
		if (IntersectsRect(Area, Cut))
			return true;
	}

	return false;
}

void PanelContent::DrawText(const UiTheme& Theme, float ScaleValue, bool CaretVisible, rect Visible) const
{
	if (!Valid(RootNode))
		return;

	// Glyphs paint after all geometry, so an open context menu cannot cover the panel's text by drawing
	// over it: every text line of the panel is scissored around the menu, then the menu draws last.
	const int Menu = OpenMenuNode();
	const size_t OccluderCount = Occluders.size();

	if (Menu >= 0)
		Occluders.push_back(MenuRect(Nodes[Menu]));

	DrawTextNode(RootNode, Theme, ScaleValue, CaretVisible, false, Visible);
	Occluders.resize(OccluderCount);

	if (Menu >= 0)
		DrawMenu(Menu, Theme, ScaleValue);
}

void PanelContent::DrawMenu(int Node, const UiTheme& Theme, float ScaleValue) const
{
	const UiNode& Item = Nodes[Node];
	const rect Menu = MenuRect(Item);
	const float ItemHeight = 20.f * ScaleValue;
	const float ItemFont = 11.5f * ScaleValue;
	const float ItemLine = renderer::line_height(ItemFont);
	const float ItemsTop = Menu.top + MenuItemsOffset(Item, ScaleValue);

	// Solid card: the shell glass must not show through the context menu.
	renderer::draw_filled_rounded_rect(Menu, 6.f * ScaleValue, Opaque(UiMix(Theme.Menu, Theme.Window, 0.25f)));
	renderer::draw_rounded_rect(Menu, 6.f * ScaleValue, 1.f * ScaleValue, Opaque(Theme.MenuBorder));

	if (!Item.MenuTitle.empty())
	{
		const float HeaderHeight = 24.f * ScaleValue;
		DrawTextLine(vec2(Menu.left + 8.f * ScaleValue, Menu.top + 6.f * ScaleValue), 11.f * ScaleValue, UiAlign::Start,
			Item.MenuTitle.c_str(), Theme.Muted);
		renderer::draw_filled_rect(rect(Menu.left + 6.f * ScaleValue, Menu.top + HeaderHeight,
			Menu.right - 6.f * ScaleValue, Menu.top + HeaderHeight + std::max(1.f, ScaleValue)), Opaque(Theme.MenuBorder));
	}

	for (size_t Index = 0; Index < Item.MenuItems.size(); Index++)
	{
		const float ItemTop = MenuItemTop(Item, static_cast<int>(Index), ScaleValue, ItemsTop);
		const float Top = ItemTop + (ItemHeight - ItemLine) * 0.5f;
		const bool Checked = Index < Item.MenuChecks.size() && Item.MenuChecks[Index] != 0;
		const bool Disabled = MenuItemDisabled(Item, static_cast<int>(Index));

		if (static_cast<int>(Index) == Item.MenuHover)
		{
			renderer::draw_filled_rect(rect(Menu.left + 4.f * ScaleValue, ItemTop, Menu.right - 4.f * ScaleValue, ItemTop + ItemHeight),
				Opaque(Theme.MenuHover));
		}

		if (Checked)
		{
			const float CheckSize = 12.f * ScaleValue;
			const rect Check(Menu.left + 7.f * ScaleValue, ItemTop + (ItemHeight - CheckSize) * 0.5f,
				Menu.left + 7.f * ScaleValue + CheckSize, ItemTop + (ItemHeight + CheckSize) * 0.5f);
			DrawUiIcon(UiIcon::Check, Check, Theme.Accent, 1.4f * ScaleValue);
		}
		else if (Index < Item.MenuIcons.size() && Item.MenuIcons[Index] != UiIcon::None)
		{
			// An icon replaces the check column for actions that have no state to show.
			const float IconSize = 13.f * ScaleValue;
			const rect Icon(Menu.left + 6.f * ScaleValue, ItemTop + (ItemHeight - IconSize) * 0.5f,
				Menu.left + 6.f * ScaleValue + IconSize, ItemTop + (ItemHeight + IconSize) * 0.5f);
			DrawUiIcon(Item.MenuIcons[Index], Icon, Disabled ? WithAlpha(Theme.Muted, 0.4f) : Theme.Muted, 1.3f * ScaleValue);
		}

		const float LabelX = Menu.left + (MenuHasDecoration(Item) ? 26.f : 10.f) * ScaleValue;
		DrawTextLine(vec2(LabelX, Top), ItemFont, UiAlign::Start, Item.MenuItems[Index].c_str(),
			Disabled ? WithAlpha(Theme.Text, 0.4f) : Theme.Text);

		if (Index < Item.MenuHints.size() && !Item.MenuHints[Index].empty())
		{
			DrawTextLine(vec2(Menu.right - 8.f * ScaleValue, Top), ItemFont, UiAlign::End,
				Item.MenuHints[Index].c_str(), Theme.Muted);
		}

		if (MenuHasSeparator(Item, static_cast<int>(Index)))
		{
			const float Hair = std::max(1.f, ScaleValue);
			const float Y = ItemTop + ItemHeight + 3.f * ScaleValue;
			renderer::draw_filled_rect(rect(Menu.left + 8.f * ScaleValue, Y, Menu.right - 8.f * ScaleValue, Y + Hair),
				Opaque(Theme.MenuBorder));
		}
	}
}

void PanelContent::DrawTextLine(vec2 Point, float Size, UiAlign Align, const wchar_t* Text, color Tint) const
{
	text_align Mode = text_align::left;

	if (TextAlpha < 1.f)
		Tint = WithAlpha(Tint, TextAlpha);

	if (Align == UiAlign::Center)
		Mode = text_align::center;
	else if (Align == UiAlign::End)
		Mode = text_align::right;

	if (!Text || !*Text || Occluders.empty())
	{
		renderer::draw_text(Point, Size, Mode, Text, Tint);
		return;
	}

	const rect Area = renderer::text_bounds(Point, Size, Mode, Text);

	if (!Occluded(Area))
	{
		renderer::draw_text(Point, Size, Mode, Text, Tint);
		return;
	}

	// The renderer batches glyphs after all geometry, so an overlay cannot cover text by painting over it.
	// Instead the whole string is drawn once per visible piece of its area, each piece a scissor rect that
	// excludes every occluder: glyphs are cut at the overlay edge pixel-exactly, never dropped or shifted.
	const float Margin = renderer::line_height(Size);
	std::vector<rect> Pieces{ rect(Area.left - Margin, Area.top - Margin, Area.right + Margin, Area.bottom + Margin) };
	std::vector<rect> Next;

	for (const rect& Cut : Occluders)
	{
		Next.clear();

		for (const rect& Piece : Pieces)
		{
			if (!IntersectsRect(Piece, Cut))
			{
				Next.push_back(Piece);
				continue;
			}

			const float Top = std::max(Piece.top, Cut.top);
			const float Bottom = std::min(Piece.bottom, Cut.bottom);

			if (Cut.top > Piece.top)
				Next.push_back(rect(Piece.left, Piece.top, Piece.right, Top));
			if (Cut.bottom < Piece.bottom)
				Next.push_back(rect(Piece.left, Bottom, Piece.right, Piece.bottom));
			if (Cut.left > Piece.left)
				Next.push_back(rect(Piece.left, Top, Cut.left, Bottom));
			if (Cut.right < Piece.right)
				Next.push_back(rect(Cut.right, Top, Piece.right, Bottom));
		}

		Pieces.swap(Next);
	}

	for (const rect& Piece : Pieces)
	{
		renderer::push_clip(Piece);
		renderer::draw_text(Point, Size, Mode, Text, Tint);
		renderer::pop_clip();
	}
}

void PanelContent::DrawTextNode(int Node, const UiTheme& Theme, float ScaleValue, bool CaretVisible, bool Disabled, rect Visible) const
{
	if (!Valid(Node))
		return;

	const UiNode& Item = Nodes[Node];

	if (!Item.Visible || !HasArea(Item.Bounds) || !TextVisible(Item.Bounds, Visible))
		return;

	const bool Off = Disabled || !Item.Enabled;
	const auto Tint = [Off](color Value) { return Off ? WithAlpha(Value, 0.45f) : Value; };

	const font_id PreviousFont = renderer::font();
	bool FontOverridden = false;

	if (!Item.FontName.empty())
		FontOverridden = renderer::select_font(Item.FontName.c_str());

	const float Font = Item.Style.FontSize * ScaleValue;
	const float LineHeight = renderer::line_height(Font);
	const float Pad = Item.Style.Padding * ScaleValue;
	const float CenteredTop = Item.Bounds.center().y - LineHeight * 0.5f;
	const color TextTint = Tint(ResolvePaint(Item.Style.TextColor, Theme, UiColorRole::Text));
	const color MutedTint = Tint(Theme.Muted);
	const bool ClipChildren = Item.Style.Clip || Item.Style.Scroll || Item.Style.ScrollX;
	const rect ChildVisible = ClipChildren ? IntersectionRect(Visible, Item.Bounds) : Visible;

	if (ClipChildren)
		renderer::push_clip(Item.Bounds);

	switch (Item.Kind)
	{
	case UiNodeKind::Text:
	{
		if (Item.Text.empty())
			break;

		float X = Item.Bounds.left + Pad;

		if (Item.Style.TextAlign == UiAlign::Center)
			X = Item.Bounds.center().x;
		else if (Item.Style.TextAlign == UiAlign::End)
			X = Item.Bounds.right - Pad;

		renderer::push_clip(Item.Bounds);

		if (Item.Style.Wrap)
		{
			float Top = Item.Bounds.top + Pad;

			for (const std::wstring& Line : Item.Wrap.Lines)
			{
				const rect LineRect(Item.Bounds.left + Pad, Top, Item.Bounds.right - Pad, Top + LineHeight);

				if (!Line.empty() && TextVisible(LineRect, ChildVisible))
					DrawTextLine(vec2(X, Top), Font, Item.Style.TextAlign, Line.c_str(), TextTint);

				Top += LineHeight;
			}
		}
		else
		{
			DrawTextLine(vec2(X, CenteredTop), Font, Item.Style.TextAlign, Item.Text.c_str(), TextTint);
		}

		renderer::pop_clip();
		break;
	}
	case UiNodeKind::Button:
		if (Item.Text.empty())
			break;

		renderer::push_clip(rect(Item.Bounds.left + 4.f * ScaleValue, Item.Bounds.top, Item.Bounds.right - 4.f * ScaleValue, Item.Bounds.bottom));
		if (Item.IconGlyph != UiIcon::None)
		{
			// Icon buttons lay the label out right of the icon, matching DrawShapesNode.
			const float TextX = ButtonContentLeft(Item, ScaleValue) + ButtonIconSize(Item, ScaleValue) + ButtonIconGap * ScaleValue;
			DrawTextLine(vec2(TextX, CenteredTop), Font, UiAlign::Start, Item.Text.c_str(), TextTint);
		}
		else
		{
			DrawTextLine(vec2(Item.Bounds.center().x, CenteredTop), Font, UiAlign::Center, Item.Text.c_str(), TextTint);
		}
		renderer::pop_clip();
		break;
	case UiNodeKind::Input:
	{
		const float Offset = Item.Focus ? InputScroll(Item) : 0.f;
		const float TextX = Item.Bounds.left + 8.f * ScaleValue - Offset;

		renderer::push_clip(rect(Item.Bounds.left + 4.f * ScaleValue, Item.Bounds.top, Item.Bounds.right - 4.f * ScaleValue, Item.Bounds.bottom));

		if (!Item.Text.empty())
			DrawTextLine(vec2(TextX, CenteredTop), Font, UiAlign::Start, Item.Text.c_str(), TextTint);
		else if (!Item.Placeholder.empty())
			DrawTextLine(vec2(TextX, CenteredTop), Font, UiAlign::Start, Item.Placeholder.c_str(), MutedTint);

		if (Item.Focus && CaretVisible)
		{
			const std::wstring Prefix = Item.Text.substr(0, static_cast<size_t>(std::clamp(Item.Caret, 0, static_cast<int>(Item.Text.size()))));
			const float X = TextX + renderer::measure_text(Prefix.c_str(), Font).width();
			renderer::draw_filled_rect(rect(X, CenteredTop, X + 1.f * ScaleValue, CenteredTop + LineHeight), Theme.Accent);
		}

		renderer::pop_clip();
		break;
	}
	case UiNodeKind::Dropdown:
	{
		const bool HasSelection = Item.Selected >= 0 && Item.Selected < static_cast<int>(Item.Options.size());
		const std::wstring& Label = HasSelection ? Item.Options[Item.Selected] : Item.Text;

		if (Label.empty())
			break;

		renderer::push_clip(rect(Item.Bounds.left + 4.f * ScaleValue, Item.Bounds.top, Item.Bounds.right - 28.f * ScaleValue, Item.Bounds.bottom));
		DrawTextLine(vec2(Item.Bounds.left + 10.f * ScaleValue, CenteredTop), Font, UiAlign::Start, Label.c_str(), HasSelection ? TextTint : MutedTint);
		renderer::pop_clip();
		break;
	}
	case UiNodeKind::Slider:
	case UiNodeKind::Progress:
	{
		// One step below body text: readable, but subordinate to the control itself. Label and value are
		// inset to line up with the ends of the track below them.
		const float Size = (UiBodyTextSize - 1.f) * ScaleValue;
		const float Inset = 6.f * ScaleValue;
		const float Top = Item.Bounds.top;
		wchar_t Buffer[64];

		if (Item.Kind == UiNodeKind::Slider)
			swprintf_s(Buffer, L"%.*f", ValueDecimals(Item), Item.Value);
		else
			swprintf_s(Buffer, L"%.0f%%", ValueRatio(Item) * 100.f);

		const float ValueWidth = renderer::measure_text(Buffer, Size).width();
		DrawTextLine(vec2(Item.Bounds.right - Inset, Top), Size, UiAlign::End, Buffer, MutedTint);

		if (!Item.Text.empty())
		{
			// Keep the label from running under the value readout on narrow sliders.
			renderer::push_clip(rect(Item.Bounds.left, Item.Bounds.top, Item.Bounds.right - ValueWidth - 10.f * ScaleValue, Item.Bounds.bottom));
			DrawTextLine(vec2(Item.Bounds.left + Inset, Top), Size, UiAlign::Start, Item.Text.c_str(), TextTint);
			renderer::pop_clip();
		}
		break;
	}
	case UiNodeKind::Checkbox:
	case UiNodeKind::Toggle:
	case UiNodeKind::Radio:
		if (!Item.Text.empty())
		{
			const float X = Item.Bounds.left + Pad + (IndicatorWidth(Item.Kind) + IndicatorGap) * ScaleValue;
			renderer::push_clip(Item.Bounds);
			DrawTextLine(vec2(X, CenteredTop), Font, UiAlign::Start, Item.Text.c_str(), TextTint);
			renderer::pop_clip();
		}
		break;
	case UiNodeKind::ListBox:
	{
		const float Row = ListRowHeight();
		const float Inset = ListPad * ScaleValue;
		const float RowFont = 12.f * ScaleValue;
		const float RowLine = renderer::line_height(RowFont);
		const int First = std::max(0, static_cast<int>(Item.Scroll / Row));
		const int Last = std::min(static_cast<int>(Item.Options.size()), First + static_cast<int>(Item.Bounds.height() / Row) + 2);
		const float TextRight = Item.Bounds.right - Inset - (Item.ScrollMax > 0.f ? 12.f * ScaleValue : 0.f);

		renderer::push_clip(rect(Item.Bounds.left + 1.f, Item.Bounds.top + 1.f, TextRight, Item.Bounds.bottom - 1.f));

		for (int Index = First; Index < Last; Index++)
		{
			const float Top = Item.Bounds.top + Inset + static_cast<float>(Index) * Row - Item.Scroll;
			const rect RowRect(Item.Bounds.left + 1.f, Top, TextRight, Top + Row);

			// Rows are culled to the viewport explicitly: the glyph pass is not reliably scissored.
			if (!TextVisible(RowRect, ChildVisible))
				continue;

			DrawTextLine(vec2(Item.Bounds.left + Inset + 8.f * ScaleValue, Top + (Row - RowLine) * 0.5f), RowFont, UiAlign::Start,
				Item.Options[Index].c_str(), Index == Item.Selected ? Tint(Theme.Accent) : TextTint);
		}

		renderer::pop_clip();
		break;
	}
	case UiNodeKind::TableList:
	{
		const float RowPx = Item.RowHeight * ScaleValue;
		const float HeaderPx = TableHeader * ScaleValue;
		const float ViewTop = Item.Bounds.top;
		const float BodyTop = std::min(Item.Bounds.bottom, ViewTop + HeaderPx);
		const float HeaderFont = 11.5f * ScaleValue;
		const float HeaderLine = renderer::line_height(HeaderFont);
		const float HeaderY = ViewTop + (HeaderPx - HeaderLine) * 0.5f;
		const float RowLine = renderer::line_height(Font);
		const std::vector<float> Widths = TableWidths(Item);

		float X = Item.Bounds.left;
		for (size_t Col = 0; Col < Item.Columns.size() && Col < Widths.size(); Col++)
		{
			const float Width = Widths[Col] * ScaleValue;
			renderer::push_clip(rect(X, ViewTop, X + Width, BodyTop));
			DrawTextLine(vec2(X + 4.f * ScaleValue, HeaderY), HeaderFont, UiAlign::Start, Item.Columns[Col].c_str(), Tint(Theme.Muted));
			renderer::pop_clip();
			X += Width;
		}

		if (BodyTop < Item.Bounds.bottom && RowPx > 0.f)
		{
			const int First = std::max(0, static_cast<int>(std::floor(Item.Scroll / RowPx)));

			for (int Index = First; Index < static_cast<int>(Item.Rows.size()); Index++)
			{
				const float Top = BodyTop + static_cast<float>(Index) * RowPx - Item.Scroll;
				if (Top >= Item.Bounds.bottom)
					break;

				if (Top + RowPx <= BodyTop)
					continue;

				const std::vector<std::wstring>& Row = Item.Rows[Index];
				float CellX = Item.Bounds.left;

				for (size_t Col = 0; Col < Row.size() && Col < Widths.size(); Col++)
				{
					const float Width = Widths[Col] * ScaleValue;
					const bool Editing = static_cast<int>(Col) == Item.EditColumn && Index == Item.EditRow;
					const std::wstring& Value = Editing ? Item.EditText : Row[Col];

					renderer::push_clip(rect(CellX, std::max(BodyTop, Top), CellX + Width, std::min(Item.Bounds.bottom, Top + RowPx)));
					DrawTextLine(vec2(CellX + 4.f * ScaleValue, Top + (RowPx - RowLine) * 0.5f), Font, UiAlign::Start,
						Value.c_str(), TextTint);

					if (Editing && CaretVisible)
					{
						const int Caret = std::clamp(Item.EditCaret, 0, static_cast<int>(Item.EditText.size()));
						const std::wstring Prefix = Item.EditText.substr(0, static_cast<size_t>(Caret));
						const float CaretX = CellX + 4.f * ScaleValue + renderer::measure_text(Prefix.c_str(), Font).width();
						renderer::draw_filled_rect(rect(CaretX, Top + 3.f * ScaleValue, CaretX + std::max(1.f, ScaleValue),
							Top + RowPx - 3.f * ScaleValue), Tint(Theme.Accent));
					}

					renderer::pop_clip();
					CellX += Width;
				}
			}
		}

		break;
	}
	case UiNodeKind::HexView:
	{
		const float RowPx = Item.RowHeight * ScaleValue;
		const float RowLine = renderer::line_height(Font);
		const uint64_t Rows = HexRowCount(Item);
		const uint64_t First = HexFirstRow(Item, RowPx);
		const uint64_t VisibleRows = static_cast<uint64_t>(std::max(0.f, std::floor(Item.Bounds.height() / RowPx)));
		const uint64_t Last = std::min(Rows, First + VisibleRows);
		const HexGeometry Layout = HexLayout(Item, Item.Bounds.left, ScaleValue);
		const bool Integer = Item.HexKind == 1;
		const color AddressTint = Tint(Theme.Muted);
		const color ChangedTint = Tint(Theme.Danger);
		const int Stride = Item.HexUtf16 ? 2 : 1;

		for (uint64_t Index = First; Index < Last; Index++)
		{
			const float Top = Item.Bounds.top + static_cast<float>(Index - First) * RowPx;
			const float TextY = Top + (RowPx - RowLine) * 0.5f;
			const uint64_t RowAddress = Index * 16;

			// Resolve the row once; unmapped bytes and bytes past the span render as '?'. Bytes that differ
			// from the baseline are drawn in a second, red pass over the same monospace grid.
			uint8_t RowBytes[16] = {};
			bool Present[16] = {};
			bool Changed[16] = {};

			for (uint64_t Byte = 0; Byte < 16; Byte++)
			{
				const uint64_t Address = RowAddress + Byte;
				uint64_t FileOffset = Address;

				if (Address >= Item.HexSpan || !Item.HexData)
					continue;

				if (Item.HexResolve && !Item.HexResolve(Address, FileOffset))
					continue;

				if (FileOffset >= Item.HexSize)
					continue;

				RowBytes[Byte] = Item.HexData[FileOffset];
				Present[Byte] = true;
				Changed[Byte] = Item.HexBaseline && Item.HexBaseline[FileOffset] != RowBytes[Byte];
			}

			if (Item.HexAddressText)
			{
				const std::wstring Label = Item.HexAddressText(RowAddress);
				DrawTextLine(vec2(Layout.AddressX, TextY), Font, UiAlign::Start, Label.c_str(), AddressTint);
			}
			else
			{
				wchar_t Address[24];
				swprintf_s(Address, L"%08llX", static_cast<unsigned long long>(RowAddress));
				DrawTextLine(vec2(Layout.AddressX, TextY), Font, UiAlign::Start, Address, AddressTint);
			}

			const bool EditRow = Item.HexEditing && Item.HexEditAddress / 16 == Index;
			const int EditOffset = EditRow ? static_cast<int>(Item.HexEditAddress % 16) : -1;

			// Every cell is exactly Digits characters plus one space, so the monospace strings line up with
			// the geometry used for hit testing and selection.
			const size_t CellChars = static_cast<size_t>(Layout.Digits + 1);
			std::wstring Values(static_cast<size_t>(Layout.Columns) * CellChars, L' ');
			std::wstring ValuesChanged = Values;
			bool AnyChanged = false;

			for (int Column = 0; Column < Layout.Columns; Column++)
			{
				const int Base = Column * Layout.Unit;
				const size_t At = static_cast<size_t>(Column) * CellChars;
				uint64_t Raw = 0;
				bool Complete = true;
				bool CellChanged = false;

				for (int Part = 0; Part < Layout.Unit; Part++)
				{
					Complete = Complete && Present[Base + Part];
					CellChanged = CellChanged || Changed[Base + Part];
					Raw |= static_cast<uint64_t>(RowBytes[Base + Part]) << (8 * Part);
				}

				// The edited cell is drawn separately below.
				if (EditRow && Item.HexEditColumn == 1 && EditOffset == Base)
					continue;

				if (!Complete)
				{
					// Hex cells pad every digit; integer cells right-align a single marker like the numbers.
					if (Integer)
						Values[At + static_cast<size_t>(Layout.Digits - 1)] = L'?';
					else
						Values.replace(At, static_cast<size_t>(Layout.Digits), static_cast<size_t>(Layout.Digits), L'?');

					continue;
				}

				const std::wstring Cell = HexFormatCell(Item, Raw, Layout.Unit, Layout.Digits, true);

				for (size_t Char = 0; Char < Cell.size() && Char < static_cast<size_t>(Layout.Digits); Char++)
				{
					// Hex digits map back to their byte (little-endian: the last byte prints first); a
					// decimal value is red as a whole when any of its bytes changed.
					const bool Red = Integer ? CellChanged : Changed[Base + Layout.Unit - 1 - static_cast<int>(Char / 2)];
					(Red ? ValuesChanged : Values)[At + Char] = Cell[Char];
					AnyChanged = AnyChanged || Red;
				}
			}

			DrawTextLine(vec2(Layout.ValuesX, TextY), Font, UiAlign::Start, Values.c_str(), TextTint);

			if (AnyChanged)
				DrawTextLine(vec2(Layout.ValuesX, TextY), Font, UiAlign::Start, ValuesChanged.c_str(), ChangedTint);

			std::wstring TextColumn(static_cast<size_t>(Layout.TextChars), L' ');
			std::wstring TextChanged = TextColumn;
			bool AnyTextChanged = false;

			for (int Char = 0; Char < Layout.TextChars; Char++)
			{
				const int Byte = Char * Stride;
				const bool Complete = Present[Byte] && (Stride == 1 || Present[Byte + 1]);
				const bool Red = Changed[Byte] || (Stride == 2 && Changed[Byte + 1]);
				const unsigned Value = static_cast<unsigned>(RowBytes[Byte]) |
					(Stride == 2 ? static_cast<unsigned>(RowBytes[Byte + 1]) << 8 : 0u);
				const wchar_t Glyph = Complete ? HexTextChar(Value, Item.HexUtf16) : L'?';

				(Red ? TextChanged : TextColumn)[static_cast<size_t>(Char)] = Glyph;
				AnyTextChanged = AnyTextChanged || Red;
			}

			DrawTextLine(vec2(Layout.TextX, TextY), Font, UiAlign::Start, TextColumn.c_str(), TextTint);

			if (AnyTextChanged)
				DrawTextLine(vec2(Layout.TextX, TextY), Font, UiAlign::Start, TextChanged.c_str(), ChangedTint);

			// Value being typed: pending digits with a caret, or the current value muted until typing starts.
			if (EditRow && Item.HexEditColumn == 1)
			{
				const float CellX = Layout.ValuesX + static_cast<float>(EditOffset / Layout.Unit) * Layout.CellWidth;

				if (Item.HexEditText.empty())
				{
					uint64_t Raw = 0;
					bool Complete = true;

					for (int Part = 0; Part < Layout.Unit; Part++)
					{
						Complete = Complete && Present[EditOffset + Part];
						Raw |= static_cast<uint64_t>(RowBytes[EditOffset + Part]) << (8 * Part);
					}

					if (Complete)
					{
						DrawTextLine(vec2(CellX, TextY), Font, UiAlign::Start,
							HexFormatCell(Item, Raw, Layout.Unit, Layout.Digits, true).c_str(), AddressTint);
					}
				}
				else
				{
					DrawTextLine(vec2(CellX, TextY), Font, UiAlign::Start, Item.HexEditText.c_str(), Tint(Theme.Accent));
				}

				if (CaretVisible)
				{
					const float CaretX = CellX + static_cast<float>(Item.HexEditText.size()) * Layout.CharWidth;
					renderer::draw_filled_rect(rect(CaretX, Top + 2.f * ScaleValue, CaretX + std::max(1.f, ScaleValue),
						Top + RowPx - 2.f * ScaleValue), Tint(Theme.Accent));
				}
			}
		}

		break;
	}
	case UiNodeKind::Collapsible:
	{
		const rect Header = InteractiveRect(Item);
		const float Top = Header.center().y - LineHeight * 0.5f;
		renderer::push_clip(rect(Header.left, Header.top, Header.right - 6.f * ScaleValue, Header.bottom));
		DrawTextLine(vec2(Header.left + 18.f * ScaleValue, Top), Font, UiAlign::Start, Item.Text.c_str(), TextTint);
		renderer::pop_clip();
		break;
	}
	default:
		break;
	}

	if (FontOverridden)
		renderer::set_font(PreviousFont);

	for (int Child : Item.Children)
		DrawTextNode(Child, Theme, ScaleValue, CaretVisible, Off, ChildVisible);

	if (ClipChildren)
		renderer::pop_clip();

	// Scrollbars paint in the text pass so glyphs never cover the thumbs; they are the last thing in
	// the node's subtree.
	if (Item.Style.Scroll || Item.Style.ScrollX)
		DrawScrollbar(Item, Theme, ScaleValue);
}

void PanelContent::DrawDebug(float ScaleValue) const
{
	if (Valid(RootNode))
		DrawDebugNode(RootNode, ScaleValue);
}

void PanelContent::DrawDebugNode(int Node, float ScaleValue) const
{
	if (!Valid(Node))
		return;

	const UiNode& Item = Nodes[Node];

	if (!Item.Visible || !HasArea(Item.Bounds))
		return;

	float Luma = 0.f;

	switch (Item.Kind)
	{
	case UiNodeKind::Button:
	case UiNodeKind::Input:
	case UiNodeKind::Dropdown:
	case UiNodeKind::Slider:
	case UiNodeKind::Checkbox:
	case UiNodeKind::Toggle:
	case UiNodeKind::Radio:
	case UiNodeKind::ListBox:
	case UiNodeKind::Collapsible:
		Luma = 0.f;
		break;
	default:
		Luma = 1.f;
		break;
	}

	const color Edge = Luma > 0.5f ? color(0.40f, 0.85f, 0.60f, 0.75f) : color(1.f, 0.72f, 0.22f, 0.95f);
	const float Thickness = std::max(1.f, ScaleValue);
	renderer::draw_rect(Item.Bounds, Thickness, Edge);

	if (Item.Style.Padding > 0.f)
		renderer::draw_rect(Item.Inner, Thickness, color(0.35f, 0.62f, 1.f, 0.45f));

	if (Item.Style.Absolute)
		renderer::draw_rect(Item.Bounds, Thickness * 2.f, color(0.80f, 0.45f, 1.f, 0.95f));

	if (Item.Style.Scroll && Item.ScrollMax > 0.f)
	{
		const rect Track = ScrollTrack(Item);
		const rect Thumb = ScrollThumb(Item);
		renderer::draw_rect(Track, Thickness, color(0.35f, 0.62f, 1.f, 0.55f));
		renderer::draw_rect(Thumb, Thickness, color(1.f, 0.36f, 0.55f, 0.85f));
	}

	if (Item.Style.ScrollX && Item.ScrollMaxX > 0.f)
	{
		const rect Track = ScrollTrackX(Item);
		const rect Thumb = ScrollThumbX(Item);
		renderer::draw_rect(Track, Thickness, color(0.35f, 0.62f, 1.f, 0.55f));
		renderer::draw_rect(Thumb, Thickness, color(1.f, 0.36f, 0.55f, 0.85f));
	}

	const wchar_t* Label = L"container";

	switch (Item.Kind)
	{
	case UiNodeKind::Text: Label = L"text"; break;
	case UiNodeKind::Button: Label = L"button"; break;
	case UiNodeKind::Slider: Label = L"slider"; break;
	case UiNodeKind::Input: Label = L"input"; break;
	case UiNodeKind::Dropdown: Label = L"dropdown"; break;
	case UiNodeKind::Rect: Label = L"rect"; break;
	case UiNodeKind::Spacer: Label = L"spacer"; break;
	case UiNodeKind::Checkbox: Label = L"checkbox"; break;
	case UiNodeKind::Toggle: Label = L"toggle"; break;
	case UiNodeKind::Radio: Label = L"radio"; break;
	case UiNodeKind::Progress: Label = L"progress"; break;
	case UiNodeKind::Separator: Label = L"separator"; break;
	case UiNodeKind::Image: Label = L"image"; break;
	case UiNodeKind::ListBox: Label = L"listbox"; break;
	case UiNodeKind::Collapsible: Label = L"collapsible"; break;
	case UiNodeKind::Icon: Label = L"icon"; break;
	default: break;
	}

	const float Font = 9.f * ScaleValue;
	const float Height = renderer::line_height(Font) + 2.f * ScaleValue;

	if (Item.Bounds.height() >= Height && Item.Bounds.width() >= 30.f * ScaleValue)
	{
		const rect Chip(Item.Bounds.left, Item.Bounds.top, std::min(Item.Bounds.right, Item.Bounds.left + 120.f * ScaleValue), Item.Bounds.top + Height);
		renderer::draw_filled_rect(Chip, color(0.f, 0.f, 0.f, 0.55f));
		renderer::draw_text_f(vec2(Chip.left + 2.f * ScaleValue, Chip.top + ScaleValue), Font, text_align::left, Edge,
			L"%d %s%s", Node, Label, Item.Enabled ? L"" : L" off");
	}

	for (int Child : Item.Children)
		DrawDebugNode(Child, ScaleValue);
}

void PanelContent::DrawDropdown(int Node, const UiTheme& Theme, float ScaleValue, vec2 Mouse, bool MouseInside, rect Limit) const
{
	if (!Valid(Node))
		return;

	const UiNode& Item = Nodes[Node];

	if (!Item.Open || Item.Options.empty())
		return;

	const rect Popup = DropdownPopup(Node, Limit);
	const float ItemHeight = DropdownRow * ScaleValue;
	const float Padding = DropdownPad * ScaleValue;
	const float MaxScroll = PopupScrollMax(Item, Popup);
	const float Scroll = std::clamp(Item.PopupScroll, 0.f, MaxScroll);
	const rect View(Popup.left, Popup.top + Padding, Popup.right, Popup.bottom - Padding);
	const float Font = 12.f * ScaleValue;
	const float RowRight = Popup.right - (MaxScroll > 0.f ? 10.f : 2.f) * ScaleValue;

	// Popups overlap other panels, so they are always painted opaque regardless of the glass surface alpha.
	renderer::draw_shadow(Popup, 16.f * ScaleValue, Theme.Shadow);
	renderer::draw_filled_rounded_rect(Popup, 6.f * ScaleValue, Opaque(Theme.Menu));
	renderer::draw_rounded_rect(Popup, 6.f * ScaleValue, 1.f * ScaleValue, Opaque(Theme.MenuBorder));

	renderer::push_clip(View);

	const int First = std::max(0, static_cast<int>(Scroll / ItemHeight));
	const int Last = std::min(static_cast<int>(Item.Options.size()), First + static_cast<int>(View.height() / ItemHeight) + 2);

	for (int Index = First; Index < Last; Index++)
	{
		const float Top = View.top + static_cast<float>(Index) * ItemHeight - Scroll;
		const rect Row(Popup.left + 2.f * ScaleValue, Top, RowRight, Top + ItemHeight);

		if (MouseInside && PointInRect(Row, Mouse) && PointInRect(View, Mouse))
			renderer::draw_filled_rounded_rect(Row, 4.f * ScaleValue, Opaque(Theme.MenuHover));

		const float TextTop = Row.center().y - renderer::line_height(Font) * 0.5f;
		renderer::draw_text(vec2(Row.left + 8.f * ScaleValue, TextTop), Font, text_align::left,
			Item.Options[Index].c_str(), Index == Item.Selected ? Theme.Accent : Theme.Text);
	}

	renderer::pop_clip();

	if (MaxScroll > 0.f)
	{
		const float TrackWidth = 4.f * ScaleValue;
		const rect Track(Popup.right - TrackWidth - 3.f * ScaleValue, View.top + 2.f * ScaleValue, Popup.right - 3.f * ScaleValue, View.bottom - 2.f * ScaleValue);
		const float ThumbHeight = std::min(Track.height(), std::max(20.f * ScaleValue, Track.height() * View.height() / (View.height() + MaxScroll)));
		const float Offset = (Scroll / MaxScroll) * (Track.height() - ThumbHeight);
		renderer::draw_filled_rounded_rect(Track, TrackWidth * 0.5f, WithAlpha(Theme.Border, 0.5f));
		renderer::draw_filled_rounded_rect(rect(Track.left, Track.top + Offset, Track.right, Track.top + Offset + ThumbHeight), TrackWidth * 0.5f, Theme.Muted);
	}
}
