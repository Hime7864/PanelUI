#pragma once

#include "graphics.hpp"
#include "theme.h"
#include "icons.h"

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
	Collapsible,
	Icon,
	DragHandle,
	TableList,
	HexView
};

// How an image is placed inside its widget bounds.
enum class UiImageFit
{
	Contain,  // scaled to fit inside, aspect preserved, letterboxed
	Cover,    // scaled to fill, aspect preserved, clipped
	Stretch   // fills the bounds, aspect ignored
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
	bool ScrollX = false;
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
	std::wstring FontName;
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
	UiIcon IconGlyph = UiIcon::None;
	UiImageFit Fit = UiImageFit::Contain;
	std::vector<std::wstring> Options;
	// Rows that draw a hairline instead of text (list separators). They are not selectable.
	std::vector<int> RowDividers;
	// Extra rows drawn with the selection tint (a selected block spanning several list rows).
	std::vector<int> RowAccent;
	std::vector<std::wstring> Columns;
	std::vector<std::vector<std::wstring>> Rows;
	std::vector<float> ColumnWidths;
	float RowHeight = 24.f;
	int SelectedRow = -1;
	int SelectedColumn = -1;
	// Inline cell edit (TableList). CanEdit decides per cell; OnCellEdit receives the committed text.
	int EditRow = -1;
	int EditColumn = -1;
	int EditCaret = 0;
	std::wstring EditText;
	std::function<bool(int, int)> CanEdit;
	std::function<void(int, int, const std::wstring&)> OnCellEdit;
	std::function<void(int, int)> OnCellClick;
	// Right-click context menu (TableList, HexView). OnMenuOpen runs before the menu shows so the owner can
	// fill the items for what was clicked; OnMenuSelect receives the item index. Only one menu per panel
	// is open at a time and it draws above every other node of the panel.
	std::vector<std::wstring> MenuItems;
	std::vector<std::wstring> MenuHints;
	std::vector<int> MenuChecks;
	// Optional icon per item, drawn in the same column as the check mark (the check wins when set).
	std::vector<UiIcon> MenuIcons;
	std::vector<int> MenuSeparators;
	// Items drawn muted and ignored by clicks (options that do not apply).
	std::vector<int> MenuDisabled;
	std::wstring MenuTitle;
	std::function<void()> OnMenuOpen;
	std::function<void(int)> OnMenuSelect;
	bool MenuOpen = false;
	vec2 MenuPoint;
	int MenuHover = -1;
	const uint8_t* HexData = nullptr;
	size_t HexSize = 0;
	// Pristine copy of HexData (same size). Bytes that differ from it render in the danger colour.
	const uint8_t* HexBaseline = nullptr;
	// Addressable span shown as rows; differs from HexSize when HexResolve maps a virtual address space.
	uint64_t HexSpan = 0;
	// Fixed width of the address column in characters; 0 uses the 8-digit default label.
	int HexAddressChars = 0;
	// Monospace advance in DIPs, measured during layout so input and drawing share one geometry.
	float HexCharWidth = 0.f;
	bool HexByRva = false;
	int HexUnit = 1;
	int HexKind = 0;
	int HexSnapRow = -1;
	std::function<bool(uint64_t, uint64_t&)> HexResolve;
	std::function<std::wstring(uint64_t)> HexAddressText;
	bool HexSigned = false;
	bool HexUtf16 = false;
	// Writes bytes at a view address (owner maps it to storage). Editing is enabled only when set.
	std::function<void(uint64_t, const std::vector<uint8_t>&)> HexWrite;
	// Byte under the pointer when the menu opened.
	uint64_t HexMenuAddress = 0;
	bool HexMenuOnByte = false;
	// Hover-to-identify: after two seconds over one byte, the dump shows that byte's address.
	uint64_t HexHoverAddress = 0;
	uint64_t HexHoverSince = 0;
	bool HexHoverTip = false;
	// Selection is a byte range shown in exactly one column: 0 address, 1 values, 2 text.
	int HexSelColumn = 1;
	uint64_t HexSelAnchor = 0;
	uint64_t HexSelStart = 0;
	uint64_t HexSelEnd = 0;
	bool HexHasSelection = false;
	// Notified whenever the dump selection changes (press or drag), so another view can follow it.
	std::function<void(uint64_t, uint64_t)> HexSelectionChanged;
	// Inline edit: column 1 types a cell value, column 2 overwrites characters; HexEditAddress is the
	// first byte of the cell or character being typed.
	bool HexEditing = false;
	int HexEditColumn = 1;
	uint64_t HexEditAddress = 0;
	std::wstring HexEditText;
	float Scroll = 0.f;
	// ListBox: row to bring into view on the next layout (only scrolls when it is off screen), -1 for none.
	// Survives the scroll restore of a rebuild, so a refresh can both keep the user's view and reveal.
	int RevealRow = -1;
	// ListBox text selection: a character range inside one row (TextSelRow), from anchor to caret. -1 when
	// nothing is selected. Row selection and text selection coexist: the row drives the owner's bindings,
	// the range is what a copy takes.
	int TextSelRow = -1;
	int TextSelAnchor = 0;
	int TextSelCaret = 0;
	// ListBox column boundaries, in characters from the row text start. A press within a few pixels starts a
	// boundary drag instead of a text selection; every move reports (boundary index, pointer column in
	// characters from the row text start) so the owner can place that divider under the pointer.
	std::vector<float> ListBoundaries;
	std::function<void(int, int)> OnBoundaryDrag;
	float ScrollMax = 0.f;
	float ScrollX = 0.f;
	float ScrollMaxX = 0.f;
	float PopupScroll = 0.f;
	UiWrapCache Wrap;
	std::function<void()> OnClick;
	std::function<void(double)> OnChange;
	std::function<void(const std::wstring&)> OnSubmit;
	std::function<void(const std::wstring&)> OnEdit;
	std::function<void(int)> OnSelect;
	std::function<void(bool)> OnToggle;
	std::function<void(float)> OnResize;
	bool ResizeVertical = false;
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
	int Button(int Parent, UiIcon Glyph, const std::wstring& Value, std::function<void()> Click);
	int Slider(int Parent, const std::wstring& Label, double Min, double Max, double Value, std::function<void(double)> Change, double Step = 0.0);
	int Input(int Parent, const std::wstring& Value, std::function<void(const std::wstring&)> Submit, const std::wstring& Placeholder = L"");
	int Dropdown(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select);
	int Checkbox(int Parent, const std::wstring& Label, bool Checked, std::function<void(bool)> Changed);
	int Toggle(int Parent, const std::wstring& Label, bool On, std::function<void(bool)> Changed);
	int Radio(int Parent, const std::wstring& Label, int Group, bool Checked, std::function<void(bool)> Changed);
	int RadioGroup(int Parent, const std::vector<std::wstring>& Options, int Selected, std::function<void(int)> Select);
	int Progress(int Parent, const std::wstring& Label, double Value, double Min = 0.0, double Max = 1.0);
	int Separator(int Parent);
	int Image(int Parent, texture_id Texture, float Width, float Height, UiImageFit Fit = UiImageFit::Contain);
	// Loads (once, cached by path) and shows an image file. Unsupported or missing files render the
	// placeholder. Tint comes from Style.TextColor when set.
	int ImageFile(int Parent, const wchar_t* Path, float Width, float Height, UiImageFit Fit = UiImageFit::Contain);
	int Icon(int Parent, UiIcon Glyph, float Size = 16.f);
	int ListBox(int Parent, const std::vector<std::wstring>& Items, int Selected, std::function<void(int)> Select, float Height = 140.f);
	int Collapsible(int Parent, const std::wstring& Title, bool Expanded, float Padding = 8.f, float Gap = 6.f);
	// Fixed-height table with a sticky header and vertical scrolling. Rows are virtualized when drawn,
	// so tens of thousands of rows cost one node. Widths are 96-DPI column widths.
	int TableList(int Parent, std::vector<std::wstring> Headers, std::vector<std::vector<std::wstring>> Rows,
		std::vector<float> Widths, float ViewHeight = 260.f, float RowHeight = 24.f);
	// Virtualized hex dump over caller-owned bytes. ByRva selects the address space shown; SnapOffset is
	// the address to scroll to. UnitBytes is 1/2/4/8; Kind is 0 hexadecimal, 1 integer (HexSigned picks
	// the sign). Bytes with no backing file data render as '?'.
	int HexView(int Parent, const uint8_t* Data, size_t Size, bool ByRva, uint64_t SnapOffset, float ViewHeight,
		int UnitBytes = 1, int Kind = 0);
	int Rect(int Parent, UiPaint Fill);
	int Spacer(int Parent, float Size = 0.f, float Grow = 1.f);
	// Thin strip that reports pointer drags along one axis in 96-DPI units. Vertical handles report
	// horizontal deltas (table column resizing); horizontal handles report vertical deltas (row resizing).
	int DragHandle(int Parent, bool Vertical, float Thickness);

	void SetVisible(int Node, bool Visible);
	void SetEnabled(int Node, bool Enabled);
	void SetChecked(int Node, bool Checked);
	void SetValue(int Node, double Value);
	// Fades every glyph this panel draws (glyph tint alpha), used to push content behind a modal overlay.
	void SetTextAlpha(float Alpha);

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
	bool OnWheel(vec2 Point, float Delta, bool Horizontal = false);
	bool OnRightMouseDown(vec2 Point);
	bool OnChar(wchar_t Char);
	bool OnKey(int Key);
	// What a hex selection copies: the column it was made in, or one part of the row explicitly.
	enum class HexCopy
	{
		Column,
		Values,
		Text,
		Address,
		All
	};

	// Copies the dump selection as shown in its column, or as the requested part: spaced values, decoded
	// text, address lines, or address + values + text once per row.
	void HexCopySelection(int Node, HexCopy Mode = HexCopy::Column) const;
	// Copies a ListBox's selected characters, or the whole row when there is no text selection. Returns
	// false when the node has nothing to give.
	bool ListBoxCopySelection(int Node) const;
	// True when the point is on a draggable ListBox column boundary, so the shell can show a resize cursor.
	bool ColumnBoundaryAt(vec2 Point) const;
	// Starts editing a TableList cell when CanEdit allows it; the cell becomes an inline text field.
	bool BeginCellEdit(int Node, int Row, int Column);
	void CloseMenus();
	// Carries scroll offsets, expanded sections and table selections over from the content this one
	// replaces. Nodes are matched by position and kind, so it applies when a rebuild keeps the structure.
	void AdoptViewState(const PanelContent& Previous);
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
	// List column drag in progress: which boundary of the dragged list.
	int BoundaryIndex = -1;
	vec2 DragLast;
	int ResizeTableNode = -1;
	int ResizeTableColumn = -1;
	float ResizeStartWidth = 0.f;
	vec2 ResizeOrigin;
	uint64_t LastHexClickTime = 0;
	int LastHexClickNode = -1;
	int LastHexClickColumn = -1;
	uint64_t LastHexClickAddress = 0;
	int RadioGroups = 0;
	std::vector<int> DeferredAbsolute;
	mutable std::vector<rect> Occluders;
	float TextAlpha = 1.f;
	// Pointer position of the last move, used to place hover tips.
	vec2 HoverPoint;

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
	rect ScrollTrackX(const UiNode& Item) const;
	rect ScrollThumbX(const UiNode& Item) const;
	rect VisibleBounds(const UiNode& Item) const;
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
	int TableListRowAt(const UiNode& Item, vec2 Point) const;
	int ListBoxRowAt(const UiNode& Item, vec2 Point) const;
	// Nearest character boundary in a ListBox row to an X position, in the row's own text coordinates.
	int ListBoxColumnAt(const UiNode& Item, int Row, float X) const;
	// Index of the column boundary under the point, or -1.
	int ListBoxBoundaryAt(const UiNode& Item, vec2 Point) const;
	// Advance of one character in the list's own font, so drags convert pixels to characters exactly.
	float ListBoxCharWidth(const UiNode& Item) const;
	// Measures text in the node's own font, restoring the previous one.
	float ListBoxMeasure(const UiNode& Item, const wchar_t* Text, float Size) const;
	// Selected characters of a ListBox row: [Low, High), empty when nothing is selected.
	void ListBoxCharRange(const UiNode& Item, int Row, int& Low, int& High) const;
	// Where a row's text starts and its character metrics, so painting and hit testing agree.
	float ListBoxTextX(const UiNode& Item) const;
	int TableListColumnAt(const UiNode& Item, vec2 Point) const;
	// Column widths in DIPs, with any width beyond the configured columns given to the last one so a
	// full-width table spans its node exactly.
	std::vector<float> TableWidths(const UiNode& Item) const;
	// Hit test against the dump grid. Column receives 0 address, 1 values, 2 text; Width is the bytes one
	// click covers there. ForceColumn >= 0 clamps the point into that column and the visible rows (drags).
	bool HexHit(const UiNode& Item, vec2 Point, int& Column, uint64_t& Address, uint64_t& Width, int ForceColumn = -1) const;
	bool HexReadByte(const UiNode& Item, uint64_t Address, uint8_t& Value) const;
	// Full row rendered like the view: space-separated value cells, and the decoded text column.
	std::wstring HexRowValues(const UiNode& Item, uint64_t RowAddress) const;
	std::wstring HexRowText(const UiNode& Item, uint64_t RowAddress) const;
	// Address of one row without the column's alignment padding.
	std::wstring HexAddressLabel(const UiNode& Item, uint64_t Address) const;
	void HexBeginEdit(UiNode& Item, int Column, uint64_t Address);
	bool HexCommitEdit(UiNode& Item);
	void HexEndEdit(UiNode& Item, bool Commit);
	rect MenuRect(const UiNode& Item) const;
	int MenuIndexAt(const UiNode& Item, vec2 Point) const;
	int OpenMenuNode() const;
	void OpenMenu(int Node, vec2 Point);
	void DrawMenu(int Node, const UiTheme& Theme, float Scale) const;
	void CommitTableEdit(int Node);
	void CancelTableEdit(int Node);
	bool PointVisible(int Node, vec2 Point) const;
	void DrawShapesNode(int Node, const UiTheme& Theme, float Scale, vec2 Mouse, bool HoverAllowed, bool Disabled) const;
	void DrawTextNode(int Node, const UiTheme& Theme, float Scale, bool CaretVisible, bool Disabled, rect Visible) const;
	void DrawDebugNode(int Node, float Scale) const;
	bool Occluded(rect Area) const;
	void DrawScrollbar(const UiNode& Item, const UiTheme& Theme, float Scale) const;
	void DrawTextLine(vec2 Point, float Size, UiAlign Align, const wchar_t* Text, color Tint) const;
	color ResolvePaint(UiPaint Paint, const UiTheme& Theme, UiColorRole Fallback) const;
};
