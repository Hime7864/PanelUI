#pragma once

#include "graphics.hpp"
#include "theme.h"

#include <functional>
#include <string>
#include <vector>

enum class UiFlow
{
	Vertical,
	Horizontal
};

enum class UiAlign
{
	Start,
	Center,
	End,
	Stretch
};

enum class UiSize
{
	Auto,
	Fixed,
	Percent,
	Fill
};

enum class UiNodeKind
{
	Container,
	Text,
	Button,
	Slider,
	Input,
	Dropdown,
	Rect,
	Spacer,
	Checkbox,
	Toggle,
	Radio,
	Progress,
	Separator,
	Image,
	ListBox,
	Collapsible
};

enum class UiColorRole
{
	Custom,
	Window,
	Workspace,
	Panel,
	Header,
	TabHover,
	TabActive,
	Border,
	Text,
	Muted,
	Accent,
	AccentSoft,
	Danger,
	Menu,
	MenuHover,
	MenuBorder
};

struct UiPaint
{
	UiColorRole Role = UiColorRole::Custom;
	color Value;
	bool Set = false;

	static UiPaint RolePaint(UiColorRole Role);
	static UiPaint Solid(color Value);
	color Resolve(const UiTheme& Theme) const;
};

struct UiStyle
{
	UiFlow Flow = UiFlow::Vertical;
	UiAlign Align = UiAlign::Stretch;
	float Padding = 0.f;
	float Gap = 0.f;
	float Margin = 0.f;
	UiSize Width = UiSize::Auto;
	float WidthValue = 0.f;
	UiSize Height = UiSize::Auto;
	float HeightValue = 0.f;
	float Grow = 0.f;
	bool Absolute = false;
	bool AnchorLeft = false;
	bool AnchorRight = false;
	bool AnchorTop = false;
	bool AnchorBottom = false;
	float Left = 0.f;
	float Top = 0.f;
	float Right = 0.f;
	float Bottom = 0.f;
	std::wstring AnchorTo;
	bool Clip = false;
	bool Scroll = false;
	bool Wrap = false;
	UiPaint Background;
	UiPaint Border;
	float BorderThickness = 1.f;
	float Radius = 4.f;
	UiPaint TextColor;
	float FontSize = 13.f;
	UiAlign TextAlign = UiAlign::Start;
};

struct UiWrapCache
{
	std::wstring Source;
	float Width = -1.f;
	float Font = 0.f;
	std::vector<std::wstring> Lines;
};

struct UiNode
{
	UiNodeKind Kind = UiNodeKind::Container;
	UiStyle Style;
	std::wstring Id;
	std::wstring Text;
	std::wstring Placeholder;
	std::wstring Tooltip;
	int Parent = -1;
	std::vector<int> Children;
	rect Bounds;
	rect Inner;
	bool Visible = true;
	bool Enabled = true;
	bool Pressed = false;
	bool Focus = false;
	bool Checked = false;
	bool Open = false;
	int Caret = 0;
	double Value = 0.0;
	double Min = 0.0;
	double Max = 1.0;
	double Step = 0.0;
	int Decimals = -1;
	int Selected = 0;
	int RadioGroup = 0;
	texture_id Texture = 0;
	std::vector<std::wstring> Options;
	float Scroll = 0.f;
	float ScrollMax = 0.f;
	float PopupScroll = 0.f;
	UiWrapCache Wrap;
	std::function<void()> OnClick;
	std::function<void(double)> OnChange;
	std::function<void(const std::wstring&)> OnSubmit;
	std::function<void(const std::wstring&)> OnEdit;
	std::function<void(int)> OnSelect;
	std::function<void(bool)> OnToggle;
};

class PanelContent
{
public:
	int Init(UiFlow Flow = UiFlow::Vertical, float Padding = 12.f, float Gap = 8.f);
	int Root() const { return RootNode; }
	int Add(UiNodeKind Kind, int Parent, const UiStyle& Style, const std::wstring& Text = L"");
	UiNode& Node(int Id) { return Nodes[Id]; }
	const UiNode& Node(int Id) const { return Nodes[Id]; }
	int NodeCount() const { return static_cast<int>(Nodes.size()); }
	int Find(const std::wstring& Id) const;

	int Container(int Parent, UiFlow Flow, float Padding = 8.f, float Gap = 6.f);
	int Text(int Parent, const std::wstring& Value, float Size = 13.f);
	int Paragraph(int Parent, const std::wstring& Value, float Size = 13.f);
	int Button(int Parent, const std::wstring& Value, std::function<void()> Click);
	int Slider(int Parent, const std::wstring& Label, double Min, double Max, double Value, std::function<void(double)> Change, double Step = 0.0);
	int Input(int Parent, const std::wstring& Value, std::function<void(const std::wstring&)> Submit, const std::wstring& Placeholder = L"");
	int Dropdown(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select);
	int Checkbox(int Parent, const std::wstring& Label, bool Checked, std::function<void(bool)> Changed);
	int Toggle(int Parent, const std::wstring& Label, bool On, std::function<void(bool)> Changed);
	int Radio(int Parent, const std::wstring& Label, int Group, bool Checked, std::function<void(bool)> Changed);
	int RadioGroup(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select);
	int Progress(int Parent, const std::wstring& Label, double Value, double Min = 0.0, double Max = 1.0);
	int Separator(int Parent);
	int Image(int Parent, texture_id Texture, float Width, float Height);
	int ListBox(int Parent, const std::vector<std::wstring>& Items, int Selected, std::function<void(int)> Select, float Height = 140.f);
	int Collapsible(int Parent, const std::wstring& Title, bool Expanded, float Padding = 8.f, float Gap = 6.f);
	int Rect(int Parent, UiPaint Fill);
	int Spacer(int Parent, float Size = 0.f, float Grow = 1.f);

	void SetVisible(int Node, bool Visible);
	void SetEnabled(int Node, bool Enabled);
	void SetChecked(int Node, bool Checked);
	void SetValue(int Node, double Value);

	void Layout(rect Area, float Scale);
	void DrawShapes(const UiTheme& Theme, float Scale, vec2 Mouse, bool HoverAllowed) const;
	void DrawText(const UiTheme& Theme, float Scale, bool CaretVisible, rect Visible) const;
	void DrawDebug(float Scale) const;
	void SetOccluders(const std::vector<rect>& Rects);

	int InteractiveAt(vec2 Point) const;
	int TooltipAt(vec2 Point) const;
	bool OnMouseDown(vec2 Point, int& DragNode, int& DragKind);
	void OnMouseMove(vec2 Point, int DragNode, int DragKind);
	void OnMouseUp(vec2 Point);
	void CancelInteraction();
	bool OnWheel(vec2 Point, float Delta);
	bool OnChar(wchar_t Char);
	bool OnKey(int Key);
	void ClearFocus();
	int FocusedNode() const { return Focused; }
	bool HasFocus() const { return Focused >= 0; }

	int OpenNode() const;
	rect DropdownPopup(int Node, rect Limit) const;
	int DropdownOptionAt(int Node, vec2 Point, rect Limit) const;
	bool ScrollDropdown(int Node, float Delta, rect Limit);
	void DrawDropdown(int Node, const UiTheme& Theme, float Scale, vec2 Mouse, bool MouseInside, rect Limit) const;
	void SelectOption(int Node, int Option);
	void CloseDropdown();

private:
	std::vector<UiNode> Nodes;
	int RootNode = -1;
	float Scale = 1.f;
	rect ContentArea;
	int Pressed = -1;
	int Focused = -1;
	float ScrollGrab = 0.f;
	int RadioGroups = 0;
	std::vector<int> DeferredAbsolute;
	std::vector<rect> Occluders;

	bool Valid(int Node) const { return Node >= 0 && Node < static_cast<int>(Nodes.size()); }
	void LayoutNode(int Node, rect Bounds);
	void LayoutFlow(int Node, bool Vertical);
	void LayoutAbsolute(int Node);
	void HideSubtree(int Node);
	float MeasureHeight(int Node, float Width);
	float MeasureWidth(int Node, float Height);
	float MeasureChildren(int Node, float Cross, bool Height);
	void UpdateWrap(UiNode& Item, float Width);
	rect ResolveReference(const UiNode& Item) const;
	rect InteractiveRect(const UiNode& Item) const;
	rect ScrollTrack(const UiNode& Item) const;
	rect ScrollThumb(const UiNode& Item) const;
	float ListRowHeight() const;
	float InputScroll(const UiNode& Item) const;
	float PopupScrollMax(const UiNode& Item, rect Popup) const;
	bool InSubtree(int Node, int Ancestor) const;
	void NotifyEdit(int Node);
	void SetSliderFromPoint(UiNode& Item, float X);
	void InsertText(UiNode& Item, const std::wstring& Value);
	void Activate(int Node);
	int HitTestNode(int Node, vec2 Point) const;
	int TooltipNode(int Node, vec2 Point) const;
	int ScrollbarAt(vec2 Point) const;
	int FindScrollNode(vec2 Point) const;
	bool PointVisible(int Node, vec2 Point) const;
	void DrawShapesNode(int Node, const UiTheme& Theme, float Scale, vec2 Mouse, bool HoverAllowed, bool Disabled) const;
	void DrawTextNode(int Node, const UiTheme& Theme, float Scale, bool CaretVisible, bool Disabled, rect Visible) const;
	void DrawDebugNode(int Node, float Scale) const;
	bool Occluded(rect Area) const;
	void DrawScrollbar(const UiNode& Item, const UiTheme& Theme, float Scale) const;
	void DrawTextLine(vec2 Point, float Size, UiAlign Align, const wchar_t* Text, color Tint) const;
	color ResolvePaint(UiPaint Paint, const UiTheme& Theme, UiColorRole Fallback) const;
};
