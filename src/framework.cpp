#include "framework.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace
{
	constexpr float CollapsibleHeader = 30.f;
	constexpr float ListRow = 26.f;
	constexpr float ListPad = 4.f;
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
		return Item.Kind == UiNodeKind::ListBox || Item.Style.Scroll;
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

int PanelContent::Collapsible(int Parent, const std::wstring& Title, bool Expanded, float Padding, float Gap)
{
	UiStyle Style;
	Style.Flow = UiFlow::Vertical;
	Style.Padding = Padding;
	Style.Gap = Gap;
	Style.Radius = 6.f;
	Style.Border = UiPaint::RolePaint(UiColorRole::Border);
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
	const float InnerTop = Bounds.top + Header + Pad;
	Item.Inner = rect(Bounds.left + Pad, InnerTop,
		std::max(Bounds.left + Pad, Bounds.right - Pad), std::max(InnerTop, Bounds.bottom - Pad));

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
	case UiNodeKind::Collapsible:
		if (!Item.Checked)
		{
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

	float Cursor = Vertical ? Parent.Inner.top : Parent.Inner.left;

	if (Parent.Style.Scroll && Vertical)
		Cursor -= Parent.Scroll;

	for (const FlowItem& Flow : Items)
	{
		const UiNode& Item = Nodes[Flow.Node];
		const float Main = Flow.Fill && FillWeight > 0.f ? Remaining * Flow.Weight / FillWeight : Flow.Main;
		const float CrossSpace = std::max(0.f, (Vertical ? Parent.Inner.width() : Parent.Inner.height()) - Flow.Margin * 2.f);
		const UiSize CrossMode = Vertical ? Item.Style.Width : Item.Style.Height;
		const float CrossValue = Vertical ? Item.Style.WidthValue : Item.Style.HeightValue;
		float Cross = 0.f;

		switch (CrossMode)
		{
		case UiSize::Fixed:
			Cross = CrossValue * Scale;
			break;
		case UiSize::Percent:
			Cross = (Vertical ? Parent.Inner.width() : Parent.Inner.height()) * CrossValue / 100.f;
			break;
		case UiSize::Fill:
			Cross = CrossSpace;
			break;
		default:
			Cross = Vertical ? MeasureWidth(Flow.Node, Main) : MeasureHeight(Flow.Node, Main);
			if (Vertical && Cross <= 0.f)
				Cross = CrossSpace;
			break;
		}

		if (Item.Style.Align == UiAlign::Stretch)
			Cross = CrossSpace;

		const float CrossStart = Vertical ? Parent.Inner.left : Parent.Inner.top;
		const float CrossEnd = Vertical ? Parent.Inner.right : Parent.Inner.bottom;
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
		return Flexible ? 0.f : Style.WidthValue * Scale;
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
	const float Right = Item.Bounds.right - std::round(3.f * Scale);
	return rect(Right - TrackWidth, Item.Bounds.top + 4.f * Scale, Right, Item.Bounds.bottom - 4.f * Scale);
}

rect PanelContent::ScrollThumb(const UiNode& Item) const
{
	const rect Track = ScrollTrack(Item);
	const float Visible = Item.Bounds.height();
	const float ThumbHeight = std::min(Track.height(), std::max(24.f * Scale, Track.height() * Visible / (Visible + Item.ScrollMax)));
	const float Travel = std::max(0.f, Track.height() - ThumbHeight);
	const float Offset = Item.ScrollMax > 0.f ? (Item.Scroll / Item.ScrollMax) * Travel : 0.f;
	const float Top = std::round(Track.top + Offset);
	return rect(Track.left, Top, Track.right, Top + std::round(ThumbHeight));
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

		if ((Ancestor.Style.Clip || Ancestor.Style.Scroll) && !PointInRect(Ancestor.Bounds, Point))
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

	if ((Item.Style.Clip || Item.Style.Scroll) && !PointInRect(Item.Bounds, Point))
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

	if ((Item.Style.Clip || Item.Style.Scroll) && !Inside)
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

		if (!IsScrollable(Item) || Item.ScrollMax <= 0.f || !Item.Enabled || !HasArea(Item.Bounds))
			continue;

		const rect Track = ScrollTrack(Item);
		const rect Hit(Track.left - 6.f * Scale, Track.top, Item.Bounds.right, Track.bottom);

		if (!PointInRect(Hit, Point) || !PointVisible(Node, Point))
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
		if (!IsScrollable(Item) || Item.ScrollMax <= 0.f || !PointInRect(Item.Bounds, Point) || !PointVisible(Node, Point))
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

	const int Bar = ScrollbarAt(Point);

	if (Bar >= 0)
	{
		ClearFocus();
		UiNode& Item = Nodes[Bar];
		const rect Thumb = ScrollThumb(Item);

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
}

void PanelContent::OnMouseUp(vec2 Point)
{
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

bool PanelContent::OnWheel(vec2 Point, float Delta)
{
	const int Node = FindScrollNode(Point);

	if (Node < 0)
		return false;

	UiNode& Item = Nodes[Node];
	const float Step = Item.Kind == UiNodeKind::ListBox ? ListRowHeight() * 2.f : 48.f * Scale;
	Item.Scroll = std::clamp(Item.Scroll - Delta * Step, 0.f, Item.ScrollMax);
	return true;
}

bool PanelContent::OnChar(wchar_t Char)
{
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
	if (Item.ScrollMax <= 0.f || Item.Bounds.width() <= 12.f * ScaleValue)
		return;

	// Overlay-style scrollbar: no visible track, and a thumb quiet enough not to compete with content.
	const rect Track = ScrollTrack(Item);
	const float Radius = Track.width() * 0.5f;
	renderer::draw_filled_rounded_rect(ScrollThumb(Item), Radius, WithAlpha(Theme.Muted, 0.45f));
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
	case UiNodeKind::Collapsible:
	{
		const rect Header = InteractiveRect(Item);

		if (Item.Style.Background.Set)
			renderer::draw_filled_rounded_rect(Item.Bounds, Radius, Tint(ResolvePaint(Item.Style.Background, Theme, UiColorRole::Panel)));

		renderer::draw_filled_rounded_rect(Header, Radius, Tint(Hovered ? Theme.TabHover : Theme.Header));

		if (Item.Style.Border.Set)
			renderer::draw_rounded_rect(Item.Bounds, Radius, Line, Tint(ResolvePaint(Item.Style.Border, Theme, UiColorRole::Border)));

		const float Glyph = 16.f * ScaleValue;
		const vec2 Chevron(std::round(Header.left + 14.f * ScaleValue), std::round(Header.center().y));
		DrawUiIcon(Item.Checked ? UiIcon::ChevronDown : UiIcon::ChevronRight,
			rect(Chevron.x - Glyph * 0.5f, Chevron.y - Glyph * 0.5f, Chevron.x + Glyph * 0.5f, Chevron.y + Glyph * 0.5f),
			Tint(Theme.Muted), 1.4f * ScaleValue);
		break;
	}
	default:
		break;
	}

	const bool ClipChildren = Item.Style.Clip || Item.Style.Scroll;
	const bool ChildHover = HoverAllowed && (!ClipChildren || PointInRect(Item.Bounds, Mouse));

	if (ClipChildren)
		renderer::push_clip(Item.Bounds);

	for (int Child : Item.Children)
		DrawShapesNode(Child, Theme, ScaleValue, Mouse, ChildHover, Off);

	if (ClipChildren)
		renderer::pop_clip();

	if (Item.Style.Scroll)
		DrawScrollbar(Item, Theme, ScaleValue);
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
	if (Valid(RootNode))
		DrawTextNode(RootNode, Theme, ScaleValue, CaretVisible, false, Visible);
}

void PanelContent::DrawTextLine(vec2 Point, float Size, UiAlign Align, const wchar_t* Text, color Tint) const
{
	text_align Mode = text_align::left;

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

	// The renderer batches glyphs separately from geometry, so a partially covered string is emitted as
	// runs of characters that clear every occluder instead of being drawn through an opaque surface.
	const std::wstring Value(Text);
	const float Width = renderer::measure_text(Text, Size).width();
	float Cursor = Point.x;

	if (Mode == text_align::center)
		Cursor -= Width * 0.5f;
	else if (Mode == text_align::right)
		Cursor -= Width;

	const float LineHeight = renderer::line_height(Size);
	std::wstring Run;
	float RunX = Cursor;

	const auto Flush = [&]()
	{
		if (Run.empty())
			return;

		renderer::draw_text(vec2(RunX, Point.y), Size, text_align::left, Run.c_str(), Tint);
		Run.clear();
	};

	for (size_t Index = 0; Index < Value.size(); Index++)
	{
		const wchar_t Char = Value[Index];
		wchar_t Single[2] = { Char, 0 };
		const float CharWidth = renderer::measure_text(Single, Size).width();
		const rect CharBox(Cursor, Point.y, Cursor + CharWidth, Point.y + LineHeight);

		if (Occluded(CharBox))
		{
			Flush();
			RunX = Cursor + CharWidth;
		}
		else
		{
			if (Run.empty())
				RunX = Cursor;

			Run.push_back(Char);
		}

		Cursor += CharWidth;
	}

	Flush();
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
	const float Font = Item.Style.FontSize * ScaleValue;
	const float LineHeight = renderer::line_height(Font);
	const float Pad = Item.Style.Padding * ScaleValue;
	const float CenteredTop = Item.Bounds.center().y - LineHeight * 0.5f;
	const color TextTint = Tint(ResolvePaint(Item.Style.TextColor, Theme, UiColorRole::Text));
	const color MutedTint = Tint(Theme.Muted);
	const bool ClipChildren = Item.Style.Clip || Item.Style.Scroll;
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
	case UiNodeKind::Collapsible:
	{
		const rect Header = InteractiveRect(Item);
		const float Top = Header.center().y - LineHeight * 0.5f;
		renderer::push_clip(rect(Header.left, Header.top, Header.right - 6.f * ScaleValue, Header.bottom));
		DrawTextLine(vec2(Header.left + 26.f * ScaleValue, Top), Font, UiAlign::Start, Item.Text.c_str(), TextTint);
		renderer::pop_clip();
		break;
	}
	default:
		break;
	}

	for (int Child : Item.Children)
		DrawTextNode(Child, Theme, ScaleValue, CaretVisible, Off, ChildVisible);

	if (ClipChildren)
		renderer::pop_clip();
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
