#include "ui.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <dwmapi.h>


#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace
{
	struct ColorRole
	{
		const wchar_t* Name;
		color UiTheme::* Field;
	};

	const ColorRole ColorRoles[] =
	{
		{ L"Accent", &UiTheme::Accent },
		{ L"Panel", &UiTheme::Panel },
		{ L"Header", &UiTheme::Header },
		{ L"Tab active", &UiTheme::TabActive },
		{ L"Border", &UiTheme::Border },
		{ L"Text", &UiTheme::Text },
		{ L"Muted", &UiTheme::Muted },
		{ L"Workspace", &UiTheme::Workspace },
		{ L"Menu", &UiTheme::Menu }
	};

	color HsvToRgb(float Hue, float Sat, float Val)
	{
		Hue = Hue - std::floor(Hue);
		const float C = Val * Sat;
		const float X = C * (1.f - std::fabs(std::fmod(Hue * 6.f, 2.f) - 1.f));
		const float M = Val - C;
		float R = 0.f;
		float G = 0.f;
		float B = 0.f;

		switch (static_cast<int>(Hue * 6.f) % 6)
		{
		case 0:
			R = C;
			G = X;
			break;
		case 1:
			R = X;
			G = C;
			break;
		case 2:
			G = C;
			B = X;
			break;
		case 3:
			G = X;
			B = C;
			break;
		case 4:
			R = X;
			B = C;
			break;
		default:
			R = C;
			B = X;
			break;
		}

		return color(R + M, G + M, B + M, 1.f);
	}

	void RgbToHsv(color Value, float& Hue, float& Sat, float& Val)
	{
		const float Max = std::max({ Value.r, Value.g, Value.b });
		const float Min = std::min({ Value.r, Value.g, Value.b });
		const float Delta = Max - Min;

		Val = Max;
		Sat = Max <= 0.f ? 0.f : Delta / Max;

		if (Delta <= 0.0001f)
		{
			Hue = 0.f;
			return;
		}

		if (Max == Value.r)
			Hue = std::fmod((Value.g - Value.b) / Delta + 6.f, 6.f);
		else if (Max == Value.g)
			Hue = (Value.b - Value.r) / Delta + 2.f;
		else
			Hue = (Value.r - Value.g) / Delta + 4.f;

		Hue /= 6.f;
	}

	const wchar_t* TabMenuLabels[] =
	{
		L"Rename",
		L"Float panel",
		L"New panel",
		L"Close",
		L"Close others"
	};

	rect IntersectRect(rect Left, rect Right)
	{
		const float L = std::max(Left.left, Right.left);
		const float T = std::max(Left.top, Right.top);
		const float R = std::min(Left.right, Right.right);
		const float B = std::min(Left.bottom, Right.bottom);

		if (R <= L || B <= T)
			return rect();

		return rect(L, T, R, B);
	}

	rect LerpRect(rect From, rect To, float T)
	{
		return rect(
			From.left + (To.left - From.left) * T,
			From.top + (To.top - From.top) * T,
			From.right + (To.right - From.right) * T,
			From.bottom + (To.bottom - From.bottom) * T);
	}

	std::vector<rect> SubtractRect(const std::vector<rect>& Rects, rect Cut)
	{
		std::vector<rect> Result;
		Result.reserve(Rects.size() + 4);

		for (rect R : Rects)
		{
			const rect Overlap = IntersectRect(R, Cut);

			if (Overlap.width() <= 0.f || Overlap.height() <= 0.f)
			{
				Result.push_back(R);
				continue;
			}

			if (R.top < Overlap.top)
				Result.push_back(rect(R.left, R.top, R.right, Overlap.top));

			if (R.bottom > Overlap.bottom)
				Result.push_back(rect(R.left, Overlap.bottom, R.right, R.bottom));

			if (R.left < Overlap.left)
				Result.push_back(rect(R.left, Overlap.top, Overlap.left, Overlap.bottom));

			if (R.right > Overlap.right)
				Result.push_back(rect(Overlap.right, Overlap.top, R.right, Overlap.bottom));
		}

		return Result;
	}

	const DockZone GuideZones[5] =
	{
		DockZone::Center,
		DockZone::Left,
		DockZone::Right,
		DockZone::Top,
		DockZone::Bottom
	};

	color WithAlpha(color Value, float Alpha)
	{
		Value.a = std::clamp(Value.a * Alpha, 0.f, 1.f);
		return Value;
	}

	// Surfaces that overlap other panels (menus, popovers, modals) ignore the glass alpha so the
	// text underneath cannot bleed through them.
	color Opaque(color Value)
	{
		Value.a = 1.f;
		return Value;
	}

	const wchar_t* ZoneName(DockZone Zone)
	{
		switch (Zone)
		{
		case DockZone::Center:
			return L"center";
		case DockZone::Left:
			return L"left";
		case DockZone::Right:
			return L"right";
		case DockZone::Top:
			return L"top";
		case DockZone::Bottom:
			return L"bottom";
		case DockZone::TopLeft:
			return L"topleft";
		case DockZone::TopRight:
			return L"topright";
		case DockZone::BottomLeft:
			return L"bottomleft";
		case DockZone::BottomRight:
			return L"bottomright";
		default:
			return L"none";
		}
	}
}


void UiShell::Initialize(HWND InWindow)
{
	Window = InWindow;

	const std::vector<UiTheme>& Themes = UiThemes();
	if (!Themes.empty())
	{
		ThemeIndex = 0;
		ThemeBase = Themes[0];
		Theme = ThemeBase;
	}

	ApplyGlass();
}

const UiTheme& UiShell::CurrentTheme() const
{
	return Theme;
}

void UiShell::SetStartupLayout(std::vector<UiPanelInit> Panels)
{
	StartupPanels = std::move(Panels);
}

void UiShell::SetTitle(std::wstring Value)
{
	if (!Value.empty())
		Title = std::move(Value);
}

void UiShell::SetDefaultPanelBuild(std::function<void(PanelContent&)> Build)
{
	DefaultBuild = std::move(Build);
}

void UiShell::EnsureStartup()
{
	const font_id Fallbacks[] =
	{
		font_id::segoe_ui,
		font_id::segoe_ui_semibold,
		font_id::tahoma,
		font_id::consolas,
		font_id::arial,
		font_id::roboto
	};

	for (font_id Id : Fallbacks)
	{
		if (renderer::set_font(Id))
			break;
	}

	// The startup layout needs the real workspace rect; with an empty rect every group
	// animated in from the window's top-left corner.
	UpdateMetrics();

	std::vector<UiPanelInit> Layout = StartupPanels;

	if (Layout.empty())
	{
		// Nothing registered by the host: one empty panel keeps the shell usable.
		UiPanelInit Default;
		Default.Title = L"Panel";
		Default.Zone = DockZone::Center;
		Layout.push_back(Default);
	}

	std::vector<int> Created;

	for (size_t Index = 0; Index < Layout.size(); Index++)
	{
		UiPanelInit& Definition = Layout[Index];
		int TargetGroup = -1;

		if (Definition.Floating)
		{
			rect Bounds = Definition.FloatBounds;

			if (Bounds.width() < 10.f || Bounds.height() < 10.f)
				Bounds = rect(120.f, 120.f, 480.f, 400.f);

			TargetGroup = Workspace.CreateFloatingGroup(Bounds);
		}
		else
		{
			if (Workspace.Root < 0)
				Workspace.EnsureGroup(WorkspaceRect);

			if (Definition.RelativeTo >= 0 && Definition.RelativeTo < static_cast<int>(Created.size()))
			{
				const int RelativePanel = Created[Definition.RelativeTo];

				if (RelativePanel >= 0 && RelativePanel < static_cast<int>(Workspace.Panels.size()))
					TargetGroup = Workspace.Panels[RelativePanel].Group;
			}

			if (TargetGroup < 0)
				TargetGroup = Workspace.DefaultGroup();
		}

		const int Panel = CreatePanelWithContent(Definition.Title, TargetGroup, true, Definition.Build);
		Created.push_back(Panel);

		if (!Definition.Floating && Index > 0 && Definition.Zone != DockZone::Center && Panel >= 0 && TargetGroup >= 0)
			Workspace.DockPanel(Panel, TargetGroup, Definition.Zone, 0);
	}

	if (!Created.empty() && Created[0] >= 0 && Created[0] < static_cast<int>(Workspace.Panels.size()))
		ActiveGroup = Workspace.Panels[Created[0]].Group;

	if (ActiveGroup < 0)
		ActiveGroup = Workspace.DefaultGroup();

	PanelCounter = static_cast<int>(Created.size());
	Ready = true;
}

int UiShell::CreatePanelWithContent(const std::wstring& Title, int Group, bool Activate, const std::function<void(PanelContent&)>& Build)
{
	const int Panel = Workspace.CreatePanel(Title, Group, Activate);

	if (Panel >= 0)
		EnsurePanelContent(Panel, Build);

	return Panel;
}

void UiShell::EnsurePanelContent(int Panel, const std::function<void(PanelContent&)>& Build)
{
	if (Panel < 0)
		return;

	if (FocusedPanel == Panel)
		ClearContentFocus();

	if (OpenDropPanel == Panel)
	{
		OpenDropPanel = -1;
		OpenDropNode = -1;
	}

	if (Panel >= static_cast<int>(PanelContents.size()))
		PanelContents.resize(Workspace.Panels.size());

	std::unique_ptr<PanelContent> Content = std::make_unique<PanelContent>();

	// No Build callback means an empty panel: the library never invents content.
	if (Build)
		Build(*Content);

	PanelContents[Panel] = std::move(Content);
}

PanelContent* UiShell::ContentForPanel(int Panel)
{
	if (Panel < 0 || Panel >= static_cast<int>(PanelContents.size()))
		return nullptr;

	return PanelContents[Panel].get();
}

int UiShell::ActivePanel(int Group) const
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return -1;

	return Workspace.Groups[Group].Active;
}

PanelContent* UiShell::ActiveContent(int Group)
{
	return ContentForPanel(ActivePanel(Group));
}

rect UiShell::GroupContentBounds(int Group) const
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return rect();

	const GroupItem& Item = Workspace.Groups[Group];
	const float Top = Item.Header.height() > 1.f ? Item.Header.bottom : Item.Bounds.top;
	return rect(Item.Bounds.left, std::min(Top, Item.Bounds.bottom), Item.Bounds.right, Item.Bounds.bottom);
}

bool UiShell::IsPanelActive(int Panel) const
{
	if (Panel < 0 || Panel >= static_cast<int>(Workspace.Panels.size()) || !Workspace.Panels[Panel].Open)
		return false;

	const int Group = Workspace.Panels[Panel].Group;

	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return false;

	return Workspace.Groups[Group].Active == Panel;
}

void UiShell::ClearContentFocus()
{
	if (FocusedPanel >= 0)
	{
		if (PanelContent* Content = ContentForPanel(FocusedPanel))
			Content->ClearFocus();
	}

	FocusedPanel = -1;
}

void UiShell::LayoutContents(float Delta)
{
	for (size_t Group = 0; Group < Workspace.Groups.size(); Group++)
	{
		if (!Workspace.Groups[Group].InUse)
			continue;

		PanelContent* Content = ActiveContent(static_cast<int>(Group));

		if (Content)
			Content->Layout(GroupContentBounds(static_cast<int>(Group)), Scale);
	}

	if (FocusedPanel >= 0 && !IsPanelActive(FocusedPanel))
		ClearContentFocus();

	if (OpenDropPanel >= 0 && !IsPanelActive(OpenDropPanel))
	{
		OpenDropPanel = -1;
		OpenDropNode = -1;
	}

	if (FocusedPanel >= 0 && IsPanelActive(FocusedPanel))
	{
		InputCaretTimer += Delta;
		InputCaretVisible = (static_cast<int>(InputCaretTimer) % 2) == 0;
	}
	else
	{
		InputCaretTimer = 0.f;
		InputCaretVisible = true;
	}
}

void UiShell::DrawGroupContent(int Group, const UiTheme& Theme)
{
	PanelContent* Content = ActiveContent(Group);
	if (!Content)
		return;

	const rect Bounds = GroupContentBounds(Group);
	if (Bounds.width() < 2.f || Bounds.height() < 2.f)
		return;

	renderer::push_clip(Bounds);
	Content->DrawShapes(Theme, Scale, Mouse, Group == HoverGroup);
	renderer::pop_clip();
}

void UiShell::DrawGroupContentText(int Group, const UiTheme& Theme, const std::vector<rect>& Clips)
{
	PanelContent* Content = ActiveContent(Group);
	if (!Content)
		return;

	const rect Bounds = GroupContentBounds(Group);

	for (const rect& Visible : Clips)
	{
		const rect Clip = IntersectRect(Bounds, Visible);
		if (Clip.width() <= 0.f || Clip.height() <= 0.f)
			continue;

		renderer::push_clip(Clip);
		Content->DrawText(Theme, Scale, InputCaretVisible, Clip);
		renderer::pop_clip();
	}
}

void UiShell::DispatchContentMouseDown(int Group, vec2 Point)
{
	const int Panel = ActivePanel(Group);
	PanelContent* Content = ContentForPanel(Panel);
	if (!Content)
		return;

	int DragNode = -1;
	int DragKind = 0;
	const bool Consumed = Content->OnMouseDown(Point, DragNode, DragKind);

	if (Consumed)
	{
		ContentDragPanel = Panel;
		ContentDragNode = DragNode;
		ContentDragKind = DragKind;
	}

	const int Open = Content->OpenNode();

	if (Open >= 0)
	{
		OpenDropPanel = Panel;
		OpenDropNode = Open;
	}
	else if (OpenDropPanel == Panel)
	{
		OpenDropPanel = -1;
		OpenDropNode = -1;
	}

	if (Content->HasFocus())
	{
		FocusedPanel = Panel;
		InputCaretTimer = 0.f;
		InputCaretVisible = true;
	}
	else
	{
		FocusedPanel = -1;
	}
}

void UiShell::DrawContentOverlay()
{
	if (OpenDropPanel < 0 || OpenDropNode < 0)
		return;

	if (!IsPanelActive(OpenDropPanel))
		return;

	PanelContent* Content = ContentForPanel(OpenDropPanel);
	if (!Content)
		return;

	const UiTheme& Theme = CurrentTheme();
	Content->DrawDropdown(OpenDropNode, Theme, Scale, Mouse, MouseInside && !ModalOpen(),
		rect(0.f, 0.f, ClientSize.x, ClientSize.y));
}

bool UiShell::HandleContentOverlay(vec2 Point)
{
	if (OpenDropPanel < 0 || OpenDropNode < 0)
		return false;

	PanelContent* Content = ContentForPanel(OpenDropPanel);

	if (!Content || !IsPanelActive(OpenDropPanel) || Content->OpenNode() != OpenDropNode)
	{
		OpenDropPanel = -1;
		OpenDropNode = -1;
		return false;
	}

	const rect Limit(0.f, 0.f, ClientSize.x, ClientSize.y);
	const int Option = Content->DropdownOptionAt(OpenDropNode, Point, Limit);

	if (Option >= 0)
		Content->SelectOption(OpenDropNode, Option);
	else
		Content->CloseDropdown();

	OpenDropPanel = -1;
	OpenDropNode = -1;
	return true;
}

bool UiShell::ModalOpen() const
{
	return SettingsOpen || MenuOpen || PickerOpen;
}

void UiShell::UpdateHover(float Delta)
{
	// Only the topmost surface under the cursor may show hover feedback; otherwise docked panels
	// light up underneath floating panels, popups, and modal overlays.
	HoverGroup = -1;

	bool OverPopup = false;

	if (OpenDropPanel >= 0 && IsPanelActive(OpenDropPanel))
	{
		if (PanelContent* Content = ContentForPanel(OpenDropPanel))
			OverPopup = PointInRect(Content->DropdownPopup(OpenDropNode, rect(0.f, 0.f, ClientSize.x, ClientSize.y)), Mouse);
	}

	const bool Blocked = !MouseInside || ModalOpen() || OverPopup || Drag.Started || Drag.Kind == 3 || Drag.Kind == 4;

	if (!Blocked && PointInRect(WorkspaceRect, Mouse))
		HoverGroup = Workspace.GroupAt(Mouse);

	int Panel = -1;
	int Node = -1;

	if (HoverGroup >= 0 && ContentDragPanel < 0 && PointInRect(GroupContentBounds(HoverGroup), Mouse))
	{
		Panel = ActivePanel(HoverGroup);

		if (PanelContent* Content = ContentForPanel(Panel))
			Node = Content->TooltipAt(Mouse);
	}

	if (Node < 0)
		Panel = -1;

	if (Panel != TooltipPanel || Node != TooltipNode)
	{
		TooltipPanel = Panel;
		TooltipNode = Node;
		TooltipTimer = 0.f;
		TooltipDismissed = false;
	}
	else if (Node >= 0)
	{
		TooltipTimer += Delta;
	}

	TooltipBox = rect();

	if (TooltipNode < 0 || TooltipDismissed)
		return;

	PanelContent* Content = ContentForPanel(TooltipPanel);

	if (!Content || TooltipNode >= Content->NodeCount())
		return;

	const std::wstring& Text = Content->Node(TooltipNode).Tooltip;

	if (Text.empty())
		return;

	const float Size = 11.5f * Scale;
	const float Width = renderer::measure_text(Text.c_str(), Size).width() + 16.f * Scale;
	const float Height = renderer::line_height(Size) + 10.f * Scale;
	const float Edge = 4.f * Scale;
	const float Left = std::clamp(Mouse.x + 12.f * Scale, Edge, std::max(Edge, ClientSize.x - Width - Edge));
	float Top = Mouse.y + 20.f * Scale;

	if (Top + Height > ClientSize.y - Edge)
		Top = std::max(Edge, Mouse.y - Height - 8.f * Scale);

	TooltipBox = rect(Left, Top, Left + Width, Top + Height);
}

bool UiShell::OccludedByOverlay(rect Area) const
{
	for (const rect& Cut : OverlayOccluders)
	{
		if (Area.left < Cut.right && Cut.left < Area.right && Area.top < Cut.bottom && Cut.top < Area.bottom)
			return true;
	}

	return false;
}

void UiShell::BuildOccluders()
{
	OverlayOccluders.clear();

	if (SettingsOpen)
	{
		// The card floats over the workspace, so only the content it covers has to stop drawing text.
		OverlayOccluders.push_back(SettingsCard);
	}

	if (OpenDropPanel >= 0 && IsPanelActive(OpenDropPanel))
	{
		if (PanelContent* Content = ContentForPanel(OpenDropPanel))
			OverlayOccluders.push_back(Content->DropdownPopup(OpenDropNode, rect(0.f, 0.f, ClientSize.x, ClientSize.y)));
	}

	if (MenuOpen)
		OverlayOccluders.push_back(MenuRect);

	if (PickerOpen)
		OverlayOccluders.push_back(PickerRect);

	// Only an active drag paints the ghost (DrawDragOverlay returns unless Drag.Started). Registering
	// the occluder earlier made a plain press on a tab cut every glyph under the cursor.
	if (Drag.Kind == 1 && Drag.Started && Drag.Panel >= 0 && Drag.Panel < static_cast<int>(Workspace.Panels.size()))
		OverlayOccluders.push_back(rect(Mouse.x + 14.f * Scale, Mouse.y + 12.f * Scale,
			Mouse.x + 14.f * Scale + 164.f * Scale, Mouse.y + 12.f * Scale + 34.f * Scale));

	if (TooltipBox.width() > 0.f && TooltipTimer >= 0.5f)
		OverlayOccluders.push_back(TooltipBox);
}

void UiShell::DrawTooltip()
{
	const float Delay = 0.5f;

	if (TooltipNode < 0 || TooltipDismissed || TooltipTimer < Delay || TooltipBox.width() <= 0.f)
		return;

	PanelContent* Content = ContentForPanel(TooltipPanel);
	if (!Content || TooltipNode >= Content->NodeCount())
		return;

	const std::wstring& Text = Content->Node(TooltipNode).Tooltip;
	if (Text.empty())
		return;

	const UiTheme& Theme = CurrentTheme();
	const float Alpha = std::clamp((TooltipTimer - Delay) / 0.12f, 0.f, 1.f);
	const float Size = 11.5f * Scale;
	const rect& Box = TooltipBox;
	renderer::draw_shadow(Box, 12.f * Scale, WithAlpha(Theme.Shadow, Alpha));
	renderer::draw_filled_rounded_rect(Box, 5.f * Scale, WithAlpha(Opaque(Theme.Menu), Alpha));
	renderer::draw_rounded_rect(Box, 5.f * Scale, 1.f * Scale, WithAlpha(Opaque(Theme.MenuBorder), Alpha));
	renderer::draw_text(vec2(Box.left + 8.f * Scale, Box.top + 5.f * Scale), Size, text_align::left, Text.c_str(), WithAlpha(Theme.Text, Alpha));
}

void UiShell::OnCaptureLost()
{
	if (ContentDragPanel >= 0)
	{
		if (PanelContent* Content = ContentForPanel(ContentDragPanel))
			Content->CancelInteraction();

		ContentDragPanel = -1;
		ContentDragNode = -1;
		ContentDragKind = 0;
	}

	Drag = DragInfo{};
	DraggingGlass = false;
	DraggingSurface = false;
	DraggingPickerSv = false;
	DraggingPickerHue = false;
	UpdateHoverCursor(Mouse);
}

rect UiShell::PickerCloseRect() const
{
	return rect(PickerRect.right - 30.f * Scale, PickerRect.top + 9.f * Scale,
		PickerRect.right - 10.f * Scale, PickerRect.top + 29.f * Scale);
}

void UiShell::UpdateMetrics()
{
	ClientSize = renderer::display_size();

	const float BarHeight = UiAppBarHeight * Scale;
	const float Pad = 8.f * Scale;

	AppBarRect = rect(0.f, 0.f, ClientSize.x, BarHeight);
	WorkspaceRect = rect(Pad, BarHeight + Pad, std::max(Pad, ClientSize.x - Pad), std::max(BarHeight + Pad, ClientSize.y - Pad));

	const float ButtonSize = 30.f * Scale;
	SettingsButton = rect(10.f * Scale, (BarHeight - ButtonSize) * 0.5f,
		10.f * Scale + ButtonSize, (BarHeight + ButtonSize) * 0.5f);

	const float ControlWidth = 46.f * Scale;
	WindowClose = rect(ClientSize.x - ControlWidth, 0.f, ClientSize.x, BarHeight);
	WindowMax = rect(ClientSize.x - ControlWidth * 2.f, 0.f, ClientSize.x - ControlWidth, BarHeight);
	WindowMin = rect(ClientSize.x - ControlWidth * 3.f, 0.f, ClientSize.x - ControlWidth * 2.f, BarHeight);

	const float NewRight = WindowMin.left - 10.f * Scale;
	const float NewTextWidth = renderer::measure_text(L"New Panel", 13.f * Scale).width();
	const float NewWidth = NewTextWidth + 54.f * Scale;
	NewPanelButton = rect(NewRight - NewWidth, (BarHeight - ButtonSize) * 0.5f,
		NewRight, (BarHeight + ButtonSize) * 0.5f);

	const float DebugWidth = renderer::measure_text(L"Rects", UiBodyTextSize * Scale).width() + 30.f * Scale;
	DebugButton = rect(NewPanelButton.left - 10.f * Scale - DebugWidth, (BarHeight - ButtonSize) * 0.5f,
		NewPanelButton.left - 10.f * Scale, (BarHeight + ButtonSize) * 0.5f);

	const float CardWidth = 700.f * Scale;
	const float CardHeight = 430.f * Scale;
	const float CardLeft = (ClientSize.x - CardWidth) * 0.5f;
	const float CardTop = (ClientSize.y - CardHeight) * 0.5f;
	SettingsCard = rect(CardLeft, CardTop, CardLeft + CardWidth, CardTop + CardHeight);
	SettingsClose = rect(CardLeft + CardWidth - 40.f * Scale, CardTop + 12.f * Scale,
		CardLeft + CardWidth - 16.f * Scale, CardTop + 36.f * Scale);

	ThemeRows.clear();
	const float RowLeft = CardLeft + 20.f * Scale;
	const float CellWidth = 130.f * Scale;
	const float CellHeight = 34.f * Scale;
	const float CellGapX = 10.f * Scale;
	const float CellGapY = 6.f * Scale;
	const float GridTop = CardTop + 92.f * Scale;

	for (size_t Index = 0; Index < UiThemes().size(); Index++)
	{
		const size_t Column = Index % 2;
		const size_t Row = Index / 2;
		const float Left = RowLeft + static_cast<float>(Column) * (CellWidth + CellGapX);
		const float Top = GridTop + static_cast<float>(Row) * (CellHeight + CellGapY);
		ThemeRows.push_back(rect(Left, Top, Left + CellWidth, Top + CellHeight));
	}

	GlassToggle = rect(RowLeft, CardTop + 278.f * Scale, RowLeft + 150.f * Scale, CardTop + 306.f * Scale);
	GlassSlider = rect(RowLeft, CardTop + 332.f * Scale, RowLeft + 250.f * Scale, CardTop + 356.f * Scale);
	SurfaceSlider = rect(RowLeft, CardTop + 382.f * Scale, RowLeft + 250.f * Scale, CardTop + 406.f * Scale);

	ColorRows.clear();
	const float ColorLeft = CardLeft + 330.f * Scale;
	const float ColorRight = CardLeft + CardWidth - 20.f * Scale;
	float ColorTop = CardTop + 94.f * Scale;

	for (size_t Index = 0; Index < sizeof(ColorRoles) / sizeof(ColorRoles[0]); Index++)
	{
		ColorRows.push_back(rect(ColorLeft, ColorTop, ColorRight, ColorTop + 30.f * Scale));
		ColorTop += 32.f * Scale;
	}
}

void UiShell::Frame()
{
	if (!Window)
		return;

	renderer::begin_scene();

	const UINT Dpi = GetDpiForWindow(Window);
	Scale = static_cast<float>(Dpi ? Dpi : 96) / UiScaleBase;

	if (!Ready)
		EnsureStartup();

	UpdateMetrics();

	const UiTheme& Theme = CurrentTheme();
	renderer::draw_filled_rect(rect(0.f, 0.f, ClientSize.x, ClientSize.y), Theme.Workspace);

	const bool AreaChanged = LayoutSeen &&
		(LastLayoutArea.left != WorkspaceRect.left || LastLayoutArea.top != WorkspaceRect.top ||
		 LastLayoutArea.right != WorkspaceRect.right || LastLayoutArea.bottom != WorkspaceRect.bottom);

	if (AreaChanged)
		Workspace.BeginAnimationAll(0.14f);

	Workspace.Layout(WorkspaceRect, Scale);

	const float Delta = std::clamp(renderer::delta_time(), 0.f, 0.05f);
	Workspace.StepAnimations(Delta);
	UpdateMotion(Delta);

	if (RenamingPanel >= 0)
	{
		CaretTimer += Delta;
		CaretVisible = (static_cast<int>(CaretTimer) % 2) == 0;
	}
	else
	{
		CaretTimer = 0.f;
		CaretVisible = true;
	}

	if (ActiveGroup >= 0 && (ActiveGroup >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[ActiveGroup].InUse))
		ActiveGroup = Workspace.DefaultGroup();

	LayoutContents(Delta);
	BuildGroupClips();
	UpdateHover(Delta);
	BuildOccluders();

	LastLayoutArea = WorkspaceRect;
	LayoutSeen = true;

	DrawAppBar();

	for (int Group : DockedOrder)
		DrawGroup(Group);

	DrawDividers();

	for (int Group : Workspace.FloatingOrder)
		DrawGroup(Group);

	for (int Group : DockedOrder)
		DrawGroupText(Group);

	for (int Group : Workspace.FloatingOrder)
		DrawGroupText(Group);

	DrawContentOverlay();
	DrawTooltip();
	DrawDragOverlay();

	if (MenuOpen)
		DrawContextMenu();

	if (SettingsOpen)
		DrawSettings();

	if (PickerOpen)
		DrawColorPicker();

	if (DebugRects)
		DrawDebugOverlay();

	renderer::end_scene(false);
}

void UiShell::DrawAppBar()
{
	const UiTheme& Theme = CurrentTheme();

	renderer::draw_filled_rect(AppBarRect, Theme.Window);
	renderer::draw_filled_rect(rect(0.f, AppBarRect.bottom - Scale, ClientSize.x, AppBarRect.bottom), Theme.Border);

	const bool BarHover = MouseInside && !ModalOpen();
	const bool SettingsHover = BarHover && PointInRect(SettingsButton, Mouse);
	if (SettingsHover || SettingsOpen)
		renderer::draw_filled_rounded_rect(SettingsButton, 8.f * Scale, Theme.TabHover);

	DrawGear(SettingsButton.center(), 7.f * Scale, SettingsHover || SettingsOpen ? Theme.Text : Theme.Muted);

	const float TitleSize = UiTitleTextSize * Scale;
	const float TitleTop = AppBarRect.center().y - renderer::line_height(TitleSize) * 0.5f;
	const vec2 TitlePoint(SettingsButton.right + 12.f * Scale, TitleTop);

	if (!OccludedByOverlay(renderer::text_bounds(TitlePoint, TitleSize, text_align::left, Title.c_str())))
		renderer::draw_text(TitlePoint, TitleSize, text_align::left, Title.c_str(), Theme.Text);

	const bool DebugHover = BarHover && PointInRect(DebugButton, Mouse);
	renderer::draw_filled_rounded_rect(DebugButton, 8.f * Scale, DebugHover ? Theme.TabHover : Theme.Header);
	renderer::draw_rounded_rect(DebugButton, 8.f * Scale, 1.f, DebugRects ? Theme.Accent : Theme.Border);

	const float DebugTextSize = UiBodyTextSize * Scale;
	const float DebugTextTop = DebugButton.center().y - renderer::line_height(DebugTextSize) * 0.5f;
	const vec2 DebugPoint(DebugButton.center().x, DebugTextTop);

	if (!OccludedByOverlay(renderer::text_bounds(DebugPoint, DebugTextSize, text_align::center, L"Rects")))
		renderer::draw_text(DebugPoint, DebugTextSize, text_align::center, L"Rects",
			DebugRects ? Theme.Accent : (DebugHover ? Theme.Text : Theme.Muted));

	const bool NewHover = BarHover && PointInRect(NewPanelButton, Mouse);
	renderer::draw_filled_rounded_rect(NewPanelButton, 8.f * Scale, NewHover ? Theme.TabHover : Theme.Header);
	renderer::draw_rounded_rect(NewPanelButton, 8.f * Scale, 1.f, Theme.Border);

	const float PlusSize = 8.f * Scale;
	const vec2 PlusCenter(NewPanelButton.left + 18.f * Scale, NewPanelButton.center().y);
	DrawPlusGlyph(rect(PlusCenter.x - PlusSize, PlusCenter.y - PlusSize, PlusCenter.x + PlusSize, PlusCenter.y + PlusSize), Theme.Accent);

	const float TextSize = UiBodyTextSize * Scale;
	const float TextTop = NewPanelButton.center().y - renderer::line_height(TextSize) * 0.5f;
	const vec2 NewPoint(NewPanelButton.left + 32.f * Scale, TextTop);

	if (!OccludedByOverlay(renderer::text_bounds(NewPoint, TextSize, text_align::left, L"New Panel")))
		renderer::draw_text(NewPoint, TextSize, text_align::left, L"New Panel", NewHover ? Theme.Text : Theme.Muted);

	const bool Maximized = IsZoomed(Window) != 0;
	const rect Controls[3] = { WindowMin, WindowMax, WindowClose };
	const color White = color(1.f, 1.f, 1.f, 1.f);

	for (int Index = 0; Index < 3; Index++)
	{
		const bool Hovered = MouseInside && PointInRect(Controls[Index], Mouse);
		const bool CloseButton = Index == 2;

		if (Hovered)
			renderer::draw_filled_rect(Controls[Index], CloseButton ? Theme.Danger : Theme.TabHover);

		const color Tint = Hovered ? (CloseButton ? White : Theme.Text) : Theme.Muted;
		const vec2 Center = Controls[Index].center();
		const float Extent = 5.f * Scale;

		if (Index == 0)
		{
			renderer::draw_filled_rect(rect(Center.x - Extent, Center.y - 0.5f * Scale, Center.x + Extent, Center.y + 0.5f * Scale), Tint);
		}
		else if (Index == 1)
		{
			if (Maximized)
			{
				renderer::draw_rect(rect(Center.x - Extent, Center.y - Extent + 2.f * Scale, Center.x + Extent - 2.f * Scale, Center.y + Extent), Scale, Tint);
				renderer::draw_rect(rect(Center.x - Extent + 2.f * Scale, Center.y - Extent, Center.x + Extent, Center.y + Extent - 2.f * Scale), Scale, Tint);
			}
			else
			{
				renderer::draw_rect(rect(Center.x - Extent, Center.y - Extent, Center.x + Extent, Center.y + Extent), Scale, Tint);
			}
		}
		else
		{
			renderer::draw_rounded_line(vec2(Center.x - Extent, Center.y - Extent), vec2(Center.x + Extent, Center.y + Extent), 1.4f * Scale, Tint);
			renderer::draw_rounded_line(vec2(Center.x + Extent, Center.y - Extent), vec2(Center.x - Extent, Center.y + Extent), 1.4f * Scale, Tint);
		}
	}
}

void UiShell::CollectGroups(int Node)
{
	if (Node < 0 || Node >= static_cast<int>(Workspace.Nodes.size()) || !Workspace.Nodes[Node].InUse)
		return;

	if (Workspace.Nodes[Node].Leaf)
	{
		DockedOrder.push_back(Workspace.Nodes[Node].Group);
		return;
	}

	CollectGroups(Workspace.Nodes[Node].First);
	CollectGroups(Workspace.Nodes[Node].Second);
}

void UiShell::BuildGroupClips()
{
	DockedOrder.clear();
	CollectGroups(Workspace.Root);

	DrawOrder = DockedOrder;

	for (int Group : Workspace.FloatingOrder)
	{
		if (Group >= 0 && Group < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[Group].InUse)
			DrawOrder.push_back(Group);
	}

	GroupClips.assign(Workspace.Groups.size(), std::vector<rect>());
	GroupOpaque.assign(Workspace.Groups.size(), false);

	for (size_t Index = 0; Index < DrawOrder.size(); Index++)
	{
		const int Group = DrawOrder[Index];
		if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
			continue;

		const rect Bounds = Workspace.Groups[Group].Bounds;

		for (size_t Below = 0; Below < Index; Below++)
		{
			const int Lower = DrawOrder[Below];
			if (Lower < 0 || Lower >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Lower].InUse)
				continue;

			const rect Overlap = IntersectRect(Bounds, Workspace.Groups[Lower].Bounds);

			if (Overlap.width() > 0.5f && Overlap.height() > 0.5f)
			{
				GroupOpaque[Group] = true;
				break;
			}
		}

		std::vector<rect> Clips;

		if (Bounds.width() > 0.5f && Bounds.height() > 0.5f)
			Clips.push_back(Bounds);

		for (size_t Above = Index + 1; Above < DrawOrder.size(); Above++)
		{
			const int Higher = DrawOrder[Above];
			if (Higher < 0 || Higher >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Higher].InUse)
				continue;

			const rect Cut = Workspace.Groups[Higher].Bounds;
			if (Cut.width() <= 0.5f || Cut.height() <= 0.5f)
				continue;

			Clips = SubtractRect(Clips, Cut);

			if (Clips.empty())
				break;
		}

		GroupClips[Group] = Clips;
	}
}

void UiShell::DrawGroupText(int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	if (Group >= static_cast<int>(GroupClips.size()))
		return;

	// The renderer paints every glyph after all geometry, so an opaque overlay drawn later cannot hide
	// text submitted earlier. Overlays and every group stacked above this one therefore cut the text.
	std::vector<rect> Cuts = OverlayOccluders;
	size_t Self = DrawOrder.size();

	for (size_t Index = 0; Index < DrawOrder.size(); Index++)
	{
		if (DrawOrder[Index] == Group)
		{
			Self = Index;
			break;
		}
	}

	for (size_t Above = Self + 1; Above < DrawOrder.size(); Above++)
	{
		const int Higher = DrawOrder[Above];

		if (Higher >= 0 && Higher < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[Higher].InUse)
			Cuts.push_back(Workspace.Groups[Higher].Bounds);
	}

	std::vector<rect> Clips = GroupClips[Group];

	for (rect Cut : Cuts)
		Clips = SubtractRect(Clips, Cut);

	if (Clips.empty())
		return;

	if (PanelContent* Content = ActiveContent(Group))
		Content->SetOccluders(Cuts);

	const UiTheme& Theme = CurrentTheme();
	const GroupItem& Item = Workspace.Groups[Group];
	const float Size = UiTabTextSize * Scale;

	for (const TabRect& Tab : Item.Tabs)
	{
		if (Tab.Panel < 0 || Tab.Panel >= static_cast<int>(Workspace.Panels.size()))
			continue;

		const bool Active = Tab.Panel == Item.Active;
		const bool Renaming = Tab.Panel == RenamingPanel;
		const float TextRight = Tab.Close.width() > 0.f ? Tab.Close.left - 2.f * Scale : Tab.Bounds.right - 4.f * Scale;
		const rect TextClip(Tab.Bounds.left + 2.f * Scale, Tab.Bounds.top, TextRight, Tab.Bounds.bottom);

		if (OccludedByOverlay(TextClip))
			continue;

		bool CutOut = false;

		for (const rect& Cut : Cuts)
		{
			if (TextClip.left < Cut.right && Cut.left < TextClip.right && TextClip.top < Cut.bottom && Cut.top < TextClip.bottom)
			{
				CutOut = true;
				break;
			}
		}

		if (CutOut)
			continue;

		for (const rect& Visible : Clips)
		{
			const rect Clip = IntersectRect(TextClip, Visible);

			if (Clip.width() <= 0.f || Clip.height() <= 0.f)
				continue;

			renderer::push_clip(Clip);

			if (Renaming)
			{
				DrawRenameField(Tab, Theme);
			}
			else
			{
				const float Top = Tab.Bounds.center().y - renderer::line_height(Size) * 0.5f;
				renderer::draw_text(vec2(Tab.Bounds.left + 10.f * Scale, Top), Size, text_align::left,
					Workspace.Panels[Tab.Panel].Title.c_str(), Active ? Theme.Text : Theme.Muted);
			}

			renderer::pop_clip();
		}
	}

	DrawGroupContentText(Group, Theme, Clips);
}

void UiShell::DrawGroup(int GroupIndex)
{
	if (GroupIndex < 0 || GroupIndex >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[GroupIndex].InUse)
		return;

	const UiTheme& Theme = CurrentTheme();
	const GroupItem& Item = Workspace.Groups[GroupIndex];
	const rect Bounds = Item.Bounds;

	if (Bounds.width() < 2.f || Bounds.height() < 2.f)
		return;

	const float Radius = UiRadius * Scale;
	color Fill = Theme.Panel;

	if (GroupIndex < static_cast<int>(GroupOpaque.size()) && GroupOpaque[GroupIndex])
		Fill.a = 1.f;

	renderer::draw_filled_rounded_rect(Bounds, Radius, Fill);
	DrawTabStrip(GroupIndex);
	DrawGroupContent(GroupIndex, Theme);

	if (Item.Floating)
		renderer::draw_rounded_rect(Bounds, Radius, 1.f, Theme.Border);

	if (GroupIndex == ActiveGroup)
	{
		const float Outset = 1.f * Scale;
		const float OutlineRadius = Radius + Outset;
		renderer::draw_rounded_rect(rect(Bounds.left - Outset, Bounds.top - Outset, Bounds.right + Outset, Bounds.bottom + Outset),
			OutlineRadius, 1.f * Scale, WithAlpha(Theme.Accent, GlassMode ? 0.30f : 0.20f));
	}
}

void UiShell::DrawTabStrip(int GroupIndex)
{
	const UiTheme& Theme = CurrentTheme();
	const GroupItem& Item = Workspace.Groups[GroupIndex];

	if (Item.Header.width() <= 1.f || Item.Header.height() <= 1.f)
		return;

	const float Radius = UiRadius * Scale;

	renderer::draw_filled_rounded_rect(Item.Header, Radius, Theme.Header);
	renderer::draw_filled_rect(rect(Item.Header.left, Item.Header.center().y, Item.Header.right, Item.Header.bottom), Theme.Header);
	renderer::draw_filled_rect(rect(Item.Header.left, Item.Header.bottom - Scale, Item.Header.right, Item.Header.bottom), Theme.Border);

	if (Item.Floating)
		DrawGripGlyph(Item.Grip, Theme.Muted);

	const bool HoverHere = GroupIndex == HoverGroup;

	for (const TabRect& Tab : Item.Tabs)
	{
		const bool Active = Tab.Panel == Item.Active;
		const bool Hovered = HoverHere && PointInRect(Tab.Bounds, Mouse);
		const bool Renaming = Tab.Panel == RenamingPanel;

		if (Active || Renaming)
		{
			renderer::draw_filled_rounded_rect(Tab.Bounds, 4.f * Scale, Theme.TabActive);
			renderer::draw_filled_rect(rect(Tab.Bounds.left, Tab.Bounds.center().y, Tab.Bounds.right, Tab.Bounds.bottom), Theme.TabActive);
		}
		else if (Hovered)
		{
			renderer::draw_filled_rounded_rect(Tab.Bounds, 4.f * Scale, Theme.TabHover);
			renderer::draw_filled_rect(rect(Tab.Bounds.left, Tab.Bounds.center().y, Tab.Bounds.right, Tab.Bounds.bottom), Theme.TabHover);
		}

		if (Renaming)
		{
			renderer::draw_rounded_rect(Tab.Bounds, 4.f * Scale, 1.5f * Scale, Theme.Accent);
		}

		const bool HasClose = Tab.Close.width() > 0.f;
		const bool CloseHover = HasClose && HoverHere && PointInRect(Tab.Close, Mouse);
		const bool ShowClose = Active || Hovered || CloseHover;

		if (CloseHover)
		{
			const float CircleRadius = std::min(Tab.Close.width(), Tab.Close.height()) * 0.5f;
			renderer::draw_filled_circle(Tab.Close.center(), CircleRadius, Theme.Danger);
			DrawCloseGlyph(Tab.Close, color(1.f, 1.f, 1.f, 1.f));
		}
		else if (ShowClose && HasClose)
		{
			DrawCloseGlyph(Tab.Close, Active ? Theme.Muted : WithAlpha(Theme.Muted, 0.75f));
		}
	}

	const bool NewHover = HoverHere && PointInRect(Item.NewTab, Mouse);
	if (NewHover)
		renderer::draw_filled_rounded_rect(Item.NewTab, 6.f * Scale, Theme.TabHover);

	DrawPlusGlyph(Item.NewTab, NewHover ? Theme.Text : Theme.Muted);
}

void UiShell::DrawTabDropMarker(int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	const GroupItem& Item = Workspace.Groups[Group];
	if (Item.Tabs.empty())
		return;

	float X = Item.Tabs.front().Bounds.left;

	if (Drag.TabIndex <= 0)
		X = Item.Tabs.front().Bounds.left;
	else if (Drag.TabIndex >= static_cast<int>(Item.Tabs.size()))
		X = Item.Tabs.back().Bounds.right;
	else
		X = (Item.Tabs[Drag.TabIndex - 1].Bounds.right + Item.Tabs[Drag.TabIndex].Bounds.left) * 0.5f;

	const rect TabBounds = Item.Tabs.front().Bounds;
	const float Half = 1.5f * Scale;
	renderer::draw_filled_rounded_rect(rect(X - Half, TabBounds.top, X + Half, TabBounds.bottom), Half, CurrentTheme().Accent);
}

void UiShell::DrawRenameField(const TabRect& Tab, const UiTheme& Theme)
{
	if (Tab.Panel < 0 || Tab.Panel >= static_cast<int>(Workspace.Panels.size()))
		return;

	const float Size = UiTabTextSize * Scale;
	const float Top = Tab.Bounds.center().y - renderer::line_height(Size) * 0.5f;

	renderer::draw_text(vec2(Tab.Bounds.left + 10.f * Scale, Top), Size, text_align::left, RenameText.c_str(), Theme.Text);

	const float TextWidth = renderer::measure_text(RenameText.c_str(), Size).width();
	const float CaretX = Tab.Bounds.left + 10.f * Scale + TextWidth + Scale;

	if (CaretVisible)
	{
		renderer::draw_filled_rect(rect(CaretX, Top, CaretX + Scale, Top + renderer::line_height(Size)), Theme.Accent);
	}
}

void UiShell::ApplyBackdrop()
{
	if (!Window)
		return;

	const int Backdrop = GlassMode ? 3 : 1;
	DwmSetWindowAttribute(Window, DWMWA_SYSTEMBACKDROP_TYPE, &Backdrop, sizeof(Backdrop));

	const float Luma = Theme.Window.r * 0.2126f + Theme.Window.g * 0.7152f + Theme.Window.b * 0.0722f;
	const int Dark = Luma < 0.5f ? 1 : 0;
	DwmSetWindowAttribute(Window, DWMWA_USE_IMMERSIVE_DARK_MODE, &Dark, sizeof(Dark));
}

void UiShell::ApplyGlass()
{
	Theme = ThemeBase;

	if (!GlassMode)
	{
		ApplyBackdrop();
		return;
	}

	const float Tint = std::clamp(GlassTint, 0.12f, 0.95f);
	const float Surface = std::clamp(SurfaceOpacity, 0.20f, 0.95f);

	Theme.Workspace.a = std::clamp(Theme.Workspace.a * Tint, 0.f, 1.f);
	Theme.Window.a = std::clamp(Theme.Window.a * std::min(1.f, Tint + 0.30f), 0.f, 1.f);
	Theme.Panel.a = std::clamp(Theme.Panel.a * Surface, 0.f, 1.f);
	Theme.Header.a = std::clamp(Theme.Header.a * Surface * 0.82f, 0.f, 1.f);
	Theme.TabActive.a = std::clamp(Theme.TabActive.a * std::min(1.f, Surface + 0.12f), 0.f, 1.f);
	Theme.TabHover.a = std::clamp(Theme.TabHover.a * Surface * 0.80f, 0.f, 1.f);
	Theme.Border.a = std::clamp(Theme.Border.a * std::max(0.40f, Surface * 0.90f), 0.f, 1.f);
	Theme.Menu.a = std::clamp(Theme.Menu.a * std::min(1.f, Surface + 0.30f), 0.f, 1.f);

	ApplyBackdrop();
}

void UiShell::OpenPicker(int Role)
{
	if (Role < 0 || Role >= static_cast<int>(sizeof(ColorRoles) / sizeof(ColorRoles[0])))
		return;

	PickerRole = Role;
	PickerOpen = true;

	const color Value = ThemeBase.*(ColorRoles[Role].Field);
	RgbToHsv(Value, PickerHue, PickerSat, PickerVal);
	PickerAlpha = Value.a;

	const float Width = 250.f * Scale;
	const float Height = 292.f * Scale;
	const rect Row = Role < static_cast<int>(ColorRows.size()) ? ColorRows[Role] : ColorRows.back();

	float Left = SettingsCard.right - Width - 20.f * Scale;
	float Top = Row.center().y - 60.f * Scale;
	Left = std::clamp(Left, 10.f * Scale, std::max(10.f * Scale, ClientSize.x - Width - 10.f * Scale));
	Top = std::clamp(Top, 10.f * Scale, std::max(10.f * Scale, ClientSize.y - Height - 10.f * Scale));

	PickerRect = rect(Left, Top, Left + Width, Top + Height);
	PickerSv = rect(Left + 14.f * Scale, Top + 44.f * Scale, Left + Width - 14.f * Scale, Top + 44.f * Scale + 150.f * Scale);
	PickerHueBar = rect(Left + 14.f * Scale, PickerSv.bottom + 14.f * Scale, Left + Width - 14.f * Scale, PickerSv.bottom + 28.f * Scale);
}

void UiShell::BuildPickerTextures()
{
	const int SvSize = 192;

	if (!PickerSvTexture)
		PickerSvTexture = renderer::create_texture_dynamic(SvSize, SvSize);

	if (!PickerHueTexture)
	{
		const int HueSize = 256;
		const int HueHeight = 8;
		std::vector<uint32_t> Pixels(HueSize * HueHeight);

		for (int Y = 0; Y < HueHeight; Y++)
		{
			for (int X = 0; X < HueSize; X++)
			{
				const color Value = HsvToRgb(static_cast<float>(X) / static_cast<float>(HueSize - 1), 1.f, 1.f);
				const uint32_t R = static_cast<uint32_t>(std::clamp(Value.r, 0.f, 1.f) * 255.f + 0.5f);
				const uint32_t G = static_cast<uint32_t>(std::clamp(Value.g, 0.f, 1.f) * 255.f + 0.5f);
				const uint32_t B = static_cast<uint32_t>(std::clamp(Value.b, 0.f, 1.f) * 255.f + 0.5f);
				Pixels[Y * HueSize + X] = (255u << 24) | (B << 16) | (G << 8) | R;
			}
		}

		PickerHueTexture = renderer::create_texture(HueSize, HueHeight, Pixels.data(), HueSize * 4);
	}

	if (PickerSvTexture && (PickerTextureHue < 0.f || std::fabs(PickerHue - PickerTextureHue) > 0.0005f))
	{
		std::vector<uint32_t> Pixels(SvSize * SvSize);

		for (int Y = 0; Y < SvSize; Y++)
		{
			const float Val = 1.f - static_cast<float>(Y) / static_cast<float>(SvSize - 1);

			for (int X = 0; X < SvSize; X++)
			{
				const float Sat = static_cast<float>(X) / static_cast<float>(SvSize - 1);
				const color Value = HsvToRgb(PickerHue, Sat, Val);
				const uint32_t R = static_cast<uint32_t>(std::clamp(Value.r, 0.f, 1.f) * 255.f + 0.5f);
				const uint32_t G = static_cast<uint32_t>(std::clamp(Value.g, 0.f, 1.f) * 255.f + 0.5f);
				const uint32_t B = static_cast<uint32_t>(std::clamp(Value.b, 0.f, 1.f) * 255.f + 0.5f);
				Pixels[Y * SvSize + X] = (255u << 24) | (B << 16) | (G << 8) | R;
			}
		}

		if (renderer::update_texture(PickerSvTexture, Pixels.data(), SvSize * 4))
			PickerTextureHue = PickerHue;
	}
}

void UiShell::UpdatePickerColor()
{
	if (PickerRole < 0 || PickerRole >= static_cast<int>(sizeof(ColorRoles) / sizeof(ColorRoles[0])))
		return;

	color Value = HsvToRgb(PickerHue, PickerSat, PickerVal);
	Value.a = PickerAlpha;
	ThemeBase.*(ColorRoles[PickerRole].Field) = Value;
	ThemeIndex = -1;
	BuildPickerTextures();
	ApplyGlass();
}

void UiShell::DrawColorPicker()
{
	if (!PickerOpen || PickerRole < 0 || PickerRole >= static_cast<int>(sizeof(ColorRoles) / sizeof(ColorRoles[0])))
		return;

	const UiTheme& Theme = CurrentTheme();
	const color Value = ThemeBase.*(ColorRoles[PickerRole].Field);

	renderer::draw_shadow(PickerRect, 20.f * Scale, Theme.Shadow);
	renderer::draw_filled_rounded_rect(PickerRect, 10.f * Scale, Opaque(Theme.Menu));
	renderer::draw_rounded_rect(PickerRect, 10.f * Scale, 1.f * Scale, Theme.MenuBorder);

	const float TitleSize = 12.5f * Scale;
	renderer::draw_text(vec2(PickerRect.left + 14.f * Scale, PickerRect.top + 12.f * Scale), TitleSize, text_align::left,
		ColorRoles[PickerRole].Name, Theme.Text);

	const rect Preview(PickerSv.left, PickerHueBar.bottom + 14.f * Scale, PickerSv.left + 54.f * Scale,
		PickerHueBar.bottom + 42.f * Scale);

	const float PreviewSize = 11.f * Scale;
	const float PreviewTop = Preview.center().y - renderer::line_height(PreviewSize) * 0.5f;
	const int Red = static_cast<int>(std::clamp(Value.r, 0.f, 1.f) * 255.f + 0.5f);
	const int Green = static_cast<int>(std::clamp(Value.g, 0.f, 1.f) * 255.f + 0.5f);
	const int Blue = static_cast<int>(std::clamp(Value.b, 0.f, 1.f) * 255.f + 0.5f);

	BuildPickerTextures();

	if (PickerSvTexture)
		renderer::draw_image(PickerSvTexture, PickerSv, color(1.f, 1.f, 1.f, 1.f));
	else
		renderer::draw_filled_rect(PickerSv, Theme.MenuHover);

	if (PickerHueTexture)
		renderer::draw_image(PickerHueTexture, PickerHueBar, color(1.f, 1.f, 1.f, 1.f));

	renderer::draw_rect(PickerSv, 1.f * Scale, Theme.MenuBorder);
	renderer::draw_rounded_rect(PickerHueBar, 3.f * Scale, 1.f * Scale, Theme.MenuBorder);

	const vec2 SvPoint(PickerSv.left + PickerSat * PickerSv.width(), PickerSv.top + (1.f - PickerVal) * PickerSv.height());
	renderer::draw_circle(SvPoint, 5.f * Scale, 2.f * Scale, color(0.f, 0.f, 0.f, 0.85f));
	renderer::draw_circle(SvPoint, 5.f * Scale, 1.f * Scale, color(1.f, 1.f, 1.f, 1.f));

	const float HueX = PickerHueBar.left + PickerHue * PickerHueBar.width();
	renderer::draw_filled_rect(rect(HueX - 1.5f * Scale, PickerHueBar.top - 2.f * Scale,
		HueX + 1.5f * Scale, PickerHueBar.bottom + 2.f * Scale), color(1.f, 1.f, 1.f, 1.f));
	renderer::draw_rect(rect(HueX - 2.5f * Scale, PickerHueBar.top - 3.f * Scale,
		HueX + 2.5f * Scale, PickerHueBar.bottom + 3.f * Scale), 1.f * Scale, color(0.f, 0.f, 0.f, 0.7f));

	renderer::draw_filled_rounded_rect(Preview, 5.f * Scale, Value);
	renderer::draw_rounded_rect(Preview, 5.f * Scale, 1.f * Scale, Theme.MenuBorder);
	renderer::draw_text_f(vec2(Preview.right + 12.f * Scale, PreviewTop), PreviewSize, text_align::left, Theme.Text,
		L"#%02X%02X%02X", Red, Green, Blue);

	const rect Close = PickerCloseRect();
	const bool CloseHover = MouseInside && PointInRect(Close, Mouse);
	DrawCloseGlyph(Close, CloseHover ? Theme.Text : Theme.Muted);
}

void UiShell::DrawDividers()
{
	const UiTheme& Theme = CurrentTheme();

	for (size_t Index = 0; Index < Workspace.Nodes.size(); Index++)
	{
		const LayoutNode& Node = Workspace.Nodes[Index];
		if (!Node.InUse || Node.Leaf)
			continue;

		const bool Hovered = (MouseInside && !ModalOpen() && Drag.Kind == 0 && PointInRect(Node.Divider, Mouse) && Workspace.GroupAt(Mouse) < 0) ||
			(Drag.Kind == 3 && Drag.Divider == static_cast<int>(Index));

		const color Tint = Hovered ? Theme.Accent : Theme.Border;
		const vec2 Center = Node.Divider.center();

		if (Node.Horizontal)
		{
			renderer::draw_filled_rounded_rect(rect(Center.x - 1.5f * Scale, Center.y - 18.f * Scale,
				Center.x + 1.5f * Scale, Center.y + 18.f * Scale), 1.5f * Scale, Tint);
		}
		else
		{
			renderer::draw_filled_rounded_rect(rect(Center.x - 18.f * Scale, Center.y - 1.5f * Scale,
				Center.x + 18.f * Scale, Center.y + 1.5f * Scale), 1.5f * Scale, Tint);
		}
	}
}

void UiShell::DrawDragOverlay()
{
	if (!Drag.Started)
		return;

	const UiTheme& Theme = CurrentTheme();

	if (Drag.EdgeActive && Drag.EdgeZone != DockZone::None)
	{
		DrawEdgePreview(EdgeAlpha);
		return;
	}

	if (Drag.RootDrop)
	{
		const rect RootRegion = RootDropRegion();
		const float Inset = 2.f * Scale + (1.f - GuideAlpha) * 12.f * Scale;
		const rect ZoneRect(RootRegion.left + Inset, RootRegion.top + Inset,
			RootRegion.right - Inset, RootRegion.bottom - Inset);
		renderer::draw_filled_rounded_rect(ZoneRect, 10.f * Scale, WithAlpha(Theme.AccentSoft, GuideAlpha * 2.4f));
		renderer::draw_rounded_rect(ZoneRect, 10.f * Scale, 2.f * Scale, WithAlpha(Theme.Accent, GuideAlpha));
	}
	else if (Drag.Target >= 0 && Drag.Target < static_cast<int>(Workspace.Groups.size()) &&
		Workspace.Groups[Drag.Target].InUse)
	{
		const rect Bounds = Workspace.Groups[Drag.Target].Bounds;

		if (Workspace.Groups[Drag.Target].Floating)
		{
			if (Drag.Zone == DockZone::Center)
			{
				const float Inset = 2.f * Scale;
				const rect ZoneRect(Bounds.left + Inset, Bounds.top + Inset, Bounds.right - Inset, Bounds.bottom - Inset);
				renderer::draw_filled_rounded_rect(ZoneRect, 8.f * Scale, WithAlpha(Theme.AccentSoft, GuideAlpha));
				renderer::draw_rounded_rect(ZoneRect, 8.f * Scale, 2.f * Scale, WithAlpha(Theme.Accent, GuideAlpha));

				if (Drag.Kind == 1)
					DrawTabDropMarker(Drag.Target);
			}
		}
		else
		{
			if (Drag.Zone != DockZone::None)
			{
				const rect Source = ZoneAnimValid ? ZoneAnimRect : Bounds;
				const float Strength = Drag.Zone == DockZone::Center ? 0.5f : 1.f;
				const float Inset = 2.f * Scale;
				const rect ZoneRect(Source.left + Inset, Source.top + Inset, Source.right - Inset, Source.bottom - Inset);
				renderer::draw_filled_rounded_rect(ZoneRect, 8.f * Scale, WithAlpha(Theme.AccentSoft, GuideAlpha * Strength));
				renderer::draw_rounded_rect(ZoneRect, 8.f * Scale, 2.f * Scale, WithAlpha(Theme.Accent, GuideAlpha * Strength));
			}

			DrawDockGuide(Drag.Target, GuideAlpha);

			if (Drag.Kind == 1 && Drag.Zone == DockZone::Center)
				DrawTabDropMarker(Drag.Target);

			return;
		}
	}

	if (Drag.Kind != 1 || Drag.Panel < 0 || Drag.Panel >= static_cast<int>(Workspace.Panels.size()))
		return;

	if (!Workspace.Panels[Drag.Panel].Open)
		return;

	const float Width = 164.f * Scale;
	const float Height = 34.f * Scale;
	const rect Ghost(Mouse.x + 14.f * Scale, Mouse.y + 12.f * Scale,
		Mouse.x + 14.f * Scale + Width, Mouse.y + 12.f * Scale + Height);

	renderer::draw_shadow(Ghost, 14.f * Scale, Theme.Shadow);
	renderer::draw_filled_rounded_rect(Ghost, 8.f * Scale, Opaque(Theme.Panel));
	renderer::draw_rounded_rect(Ghost, 8.f * Scale, 1.f, Theme.Accent);

	const float Size = UiTabTextSize * Scale;
	const float Top = Ghost.center().y - renderer::line_height(Size) * 0.5f;

	renderer::push_clip(Ghost);
	renderer::draw_text(vec2(Ghost.left + 12.f * Scale, Top), Size, text_align::left,
		Workspace.Panels[Drag.Panel].Title.c_str(), Theme.Text);
	renderer::pop_clip();
}

void UiShell::DrawDockGuide(int Group, float Alpha)
{
	if (Alpha <= 0.01f)
		return;

	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	const UiTheme& Theme = CurrentTheme();
	const rect Bounds = Workspace.Groups[Group].Bounds;

	if (Bounds.width() < 60.f * Scale || Bounds.height() < 60.f * Scale)
		return;

	rect Cells[5];
	ComputeGuideCells(Bounds, Cells);

	rect Plate = Cells[0];
	for (int Index = 1; Index < 5; Index++)
	{
		Plate.left = std::min(Plate.left, Cells[Index].left);
		Plate.top = std::min(Plate.top, Cells[Index].top);
		Plate.right = std::max(Plate.right, Cells[Index].right);
		Plate.bottom = std::max(Plate.bottom, Cells[Index].bottom);
	}

	const float Pad = 7.f * Scale;
	Plate = rect(Plate.left - Pad, Plate.top - Pad, Plate.right + Pad, Plate.bottom + Pad);

	const float Pop = 0.92f + 0.08f * Alpha;
	renderer::push_scale(Pop, Pop, Bounds.center());

	renderer::draw_filled_rounded_rect(Plate, 12.f * Scale, WithAlpha(Opaque(Theme.Panel), Alpha * 0.97f));
	renderer::draw_rounded_rect(Plate, 12.f * Scale, 1.f * Scale, WithAlpha(Theme.Border, Alpha));

	for (int Index = 0; Index < 5; Index++)
	{
		renderer::draw_filled_rounded_rect(Cells[Index], 7.f * Scale, WithAlpha(Theme.Header, Alpha));
	}

	if (GuideCellValid)
	{
		renderer::draw_filled_rounded_rect(GuideAnimCell, 7.f * Scale, WithAlpha(Theme.Accent, Alpha));
	}

	for (int Index = 0; Index < 5; Index++)
	{
		const DockZone Zone = GuideZones[Index];
		const bool Active = Drag.Zone == Zone;

		renderer::draw_rounded_rect(Cells[Index], 7.f * Scale, 1.f * Scale,
			WithAlpha(Active ? Theme.Accent : Theme.Border, Alpha));

		DrawGuideIcon(Cells[Index], Zone, Active ? color(1.f, 1.f, 1.f, 1.f) : WithAlpha(Theme.Muted, Alpha));
	}

	renderer::pop_scale();
}

void UiShell::DrawGuideIcon(rect Cell, DockZone Zone, color Tint)
{
	const float Inset = std::min(Cell.width(), Cell.height()) * 0.26f;
	const rect Inner(Cell.left + Inset, Cell.top + Inset, Cell.right - Inset, Cell.bottom - Inset);
	const float Radius = 2.5f * Scale;

	renderer::draw_rounded_rect(Inner, Radius, 1.4f * Scale, WithAlpha(Tint, 0.55f));

	rect Fill = Inner;

	switch (Zone)
	{
	case DockZone::Left:
		Fill.right = Inner.left + Inner.width() * 0.42f;
		break;
	case DockZone::Right:
		Fill.left = Inner.right - Inner.width() * 0.42f;
		break;
	case DockZone::Top:
		Fill.bottom = Inner.top + Inner.height() * 0.42f;
		break;
	case DockZone::Bottom:
		Fill.top = Inner.bottom - Inner.height() * 0.42f;
		break;
	case DockZone::Center:
	{
		const float Shrink = std::min(Inner.width(), Inner.height()) * 0.24f;
		Fill = rect(Inner.left + Shrink, Inner.top + Shrink, Inner.right - Shrink, Inner.bottom - Shrink);
		break;
	}
	default:
		break;
	}

	renderer::draw_filled_rounded_rect(Fill, 1.5f * Scale, Tint);
}

void UiShell::DrawEdgePreview(float Alpha)
{
	if (Alpha <= 0.01f)
		return;

	const UiTheme& Theme = CurrentTheme();
	const float Inset = 2.f * Scale + (1.f - Alpha) * 12.f * Scale;
	const rect Region = EdgeAnimValid ? EdgeAnimRect : Drag.EdgeRect;
	const rect Preview(Region.left + Inset, Region.top + Inset, Region.right - Inset, Region.bottom - Inset);

	if (Preview.width() < 2.f || Preview.height() < 2.f)
		return;

	renderer::draw_filled_rounded_rect(Preview, 10.f * Scale, WithAlpha(Theme.AccentSoft, Alpha * 2.4f));
	renderer::draw_rounded_rect(Preview, 10.f * Scale, 2.f * Scale, WithAlpha(Theme.Accent, Alpha));

	const bool Half = Drag.EdgeZone == DockZone::Left || Drag.EdgeZone == DockZone::Right ||
		Drag.EdgeZone == DockZone::Top || Drag.EdgeZone == DockZone::Bottom;
	const float LabelSize = 10.f * Scale;
	const float LabelTop = Preview.center().y - renderer::line_height(LabelSize) * 0.5f;
	renderer::draw_text(vec2(Preview.center().x, LabelTop), LabelSize, text_align::center,
		Half ? L"DOCK" : L"SNAP", WithAlpha(Theme.Text, Alpha));
}

void UiShell::DrawDebugOverlay()
{
	const UiTheme& Theme = CurrentTheme();
	const float FontSize = 9.5f * Scale;
	const color DockedTint = color(0.24f, 0.85f, 0.56f, 0.95f);
	const color FloatTint = color(1.f, 0.78f, 0.32f, 0.95f);
	const color SurfaceTint = color(0.60f, 0.46f, 1.f, 0.95f);
	const color HitTint = color(1.f, 0.36f, 0.55f, 0.95f);
	const color GuideTint = color(0.35f, 0.62f, 1.f, 0.95f);

	renderer::draw_rect(WorkspaceRect, 1.f * Scale, WithAlpha(GuideTint, 0.7f));

	// Widget outlines for every panel: the panel-system Rects toggle covers the layout tree, this
	// covers the nodes inside each panel's content.
	for (int Group : DrawOrder)
	{
		if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
			continue;

		if (PanelContent* Content = ActiveContent(Group))
		{
			const rect Bounds = GroupContentBounds(Group);
			renderer::push_clip(Bounds);
			Content->DrawDebug(Scale);
			renderer::pop_clip();
		}
	}

	if (Workspace.Root < 0)
	{
		renderer::draw_rect(RootDropRegion(), 1.f * Scale, WithAlpha(GuideTint, 0.6f));
		renderer::draw_text_f(vec2(RootDropRegion().center().x, RootDropRegion().bottom - 13.f * Scale), 8.f * Scale,
			text_align::center, WithAlpha(GuideTint, 0.8f), L"root drop");
	}

	for (size_t Index = 0; Index < Workspace.Nodes.size(); Index++)
	{
		const LayoutNode& Node = Workspace.Nodes[Index];
		if (!Node.InUse || Node.Leaf)
			continue;

		renderer::draw_rect(Node.Bounds, 1.f * Scale, WithAlpha(GuideTint, 0.25f));
		renderer::draw_filled_rect(Node.Divider, WithAlpha(HitTint, 0.30f));
		renderer::draw_rect(Node.Divider, 1.f * Scale, HitTint);
		renderer::draw_text_f(Node.Divider.center() + vec2(4.f * Scale, 0.f), FontSize, text_align::left,
			HitTint, L"N%zu H%d", Index, Node.Horizontal ? 1 : 0);
	}

	for (size_t Index = 0; Index < Workspace.Groups.size(); Index++)
	{
		const GroupItem& Item = Workspace.Groups[Index];
		if (!Item.InUse)
			continue;

		const color Tone = Item.Floating ? FloatTint : DockedTint;

		renderer::draw_rect(Item.Bounds, 1.f * Scale, Tone);

		if (Item.AnimRemaining > 0.f)
			renderer::draw_rect(Item.TargetBounds, 1.f * Scale, WithAlpha(GuideTint, 0.9f));

		if (Item.Header.width() > 1.f)
			renderer::draw_filled_rect(Item.Header, WithAlpha(SurfaceTint, 0.10f));

		for (const TabRect& Tab : Item.Tabs)
		{
			renderer::draw_filled_rect(Tab.Bounds, WithAlpha(SurfaceTint, 0.16f));
			renderer::draw_rect(Tab.Bounds, 1.f * Scale, WithAlpha(SurfaceTint, 0.9f));
			renderer::draw_rect(Tab.Close, 1.f * Scale, WithAlpha(HitTint, 0.8f));
			renderer::draw_filled_circle(Tab.Bounds.center(), 2.f * Scale, HitTint);
		}

		if (Item.Floating)
		{
			renderer::draw_rect(Item.Grip, 1.f * Scale, SurfaceTint);

			const float Edge = 6.f * Scale;
			renderer::draw_filled_rect(rect(Item.Bounds.left, Item.Bounds.top, Item.Bounds.right, Item.Bounds.top + Edge), WithAlpha(FloatTint, 0.18f));
			renderer::draw_filled_rect(rect(Item.Bounds.left, Item.Bounds.bottom - Edge, Item.Bounds.right, Item.Bounds.bottom), WithAlpha(FloatTint, 0.18f));
			renderer::draw_filled_rect(rect(Item.Bounds.left, Item.Bounds.top, Item.Bounds.left + Edge, Item.Bounds.bottom), WithAlpha(FloatTint, 0.18f));
			renderer::draw_filled_rect(rect(Item.Bounds.right - Edge, Item.Bounds.top, Item.Bounds.right, Item.Bounds.bottom), WithAlpha(FloatTint, 0.18f));
		}

		if (Item.Header.width() > 1.f)
		{
			renderer::draw_filled_circle(Item.Header.center(), 2.f * Scale, SurfaceTint);

			if (Item.NewTab.width() > 1.f)
			{
				renderer::draw_rect(Item.NewTab, 1.f * Scale, SurfaceTint);
				renderer::draw_filled_circle(Item.NewTab.center(), 1.8f * Scale, SurfaceTint);
			}

			if (Item.Floating && Item.Grip.width() > 1.f)
			{
				renderer::draw_rect(Item.Grip, 1.f * Scale, HitTint);
				renderer::draw_filled_circle(Item.Grip.center(), 1.8f * Scale, HitTint);
			}
		}

		int Z = -1;
		for (size_t Order = 0; Order < Workspace.FloatingOrder.size(); Order++)
		{
			if (Workspace.FloatingOrder[Order] == static_cast<int>(Index))
				Z = static_cast<int>(Order);
		}

		const float LabelHeight = 15.f * Scale;
		const float LabelTop = std::min(Item.Bounds.bottom - LabelHeight - 2.f * Scale, Item.Header.bottom + 3.f * Scale);
		const float LabelRight = Item.Bounds.right - 4.f * Scale;
		const rect LabelBox(std::max(Item.Bounds.left + 4.f * Scale, LabelRight - 342.f * Scale), LabelTop,
			LabelRight, LabelTop + LabelHeight);

		if (LabelBox.width() > 20.f && LabelBox.height() > 4.f)
		{
			renderer::draw_filled_rounded_rect(LabelBox, 4.f * Scale, WithAlpha(Theme.Window, 0.80f));
			renderer::draw_rounded_rect(LabelBox, 4.f * Scale, 1.f * Scale, WithAlpha(Tone, 0.8f));

			if (Item.Floating)
				renderer::draw_text_f(vec2(LabelBox.left + 4.f * Scale, LabelBox.top + 2.f * Scale), FontSize, text_align::left, Tone,
					L"G%zu float z%d %.0fx%.0f @%.0f,%.0f a%.2f", Index, Z, Item.Bounds.width(), Item.Bounds.height(),
					Item.Bounds.left, Item.Bounds.top, Item.AnimRemaining);
			else
				renderer::draw_text_f(vec2(LabelBox.left + 4.f * Scale, LabelBox.top + 2.f * Scale), FontSize, text_align::left, Tone,
					L"G%zu dock %.0fx%.0f @%.0f,%.0f a%.2f", Index, Item.Bounds.width(), Item.Bounds.height(),
					Item.Bounds.left, Item.Bounds.top, Item.AnimRemaining);
		}
	}

	if (Drag.Target >= 0 && Drag.Target < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[Drag.Target].InUse)
	{
		renderer::draw_rect(Workspace.Groups[Drag.Target].Bounds, 2.f * Scale, WithAlpha(GuideTint, 0.95f));
	}

	const int HoverGroup = Workspace.GroupAt(Mouse);

	if (HoverGroup >= 0 && HoverGroup < static_cast<int>(Workspace.Groups.size()))
	{
		rect Cells[5];
		ComputeGuideCells(Workspace.Groups[HoverGroup].Bounds, Cells);

		for (int Cell = 0; Cell < 5; Cell++)
		{
			renderer::draw_rect(Cells[Cell], 1.f * Scale, WithAlpha(HitTint, 0.9f));
			renderer::draw_filled_circle(Cells[Cell].center(), 1.8f * Scale, HitTint);
			renderer::draw_text_f(Cells[Cell].center() + vec2(0.f, 8.f * Scale), 8.f * Scale, text_align::center,
				HitTint, L"%s", ZoneName(GuideZones[Cell]));
		}
	}

	const float Band = 30.f * Scale;
	const rect Bands[4] =
	{
		rect(WorkspaceRect.left, WorkspaceRect.top, WorkspaceRect.left + Band, WorkspaceRect.bottom),
		rect(WorkspaceRect.right - Band, WorkspaceRect.top, WorkspaceRect.right, WorkspaceRect.bottom),
		rect(WorkspaceRect.left, WorkspaceRect.top, WorkspaceRect.right, WorkspaceRect.top + Band),
		rect(WorkspaceRect.left, WorkspaceRect.bottom - Band, WorkspaceRect.right, WorkspaceRect.bottom)
	};

	for (int BandIndex = 0; BandIndex < 4; BandIndex++)
	{
		renderer::draw_filled_rect(Bands[BandIndex], WithAlpha(GuideTint, 0.07f));
		renderer::draw_rect(Bands[BandIndex], 1.f * Scale, WithAlpha(GuideTint, 0.5f));
	}

	if (Drag.EdgeActive && Drag.EdgeZone != DockZone::None)
	{
		renderer::draw_filled_rect(Drag.EdgeRect, WithAlpha(FloatTint, 0.10f));
		renderer::draw_rect(Drag.EdgeRect, 2.f * Scale, FloatTint);
	}

	if (Drag.Started)
	{
		renderer::draw_rect(Drag.StartBounds, 1.f * Scale, WithAlpha(HitTint, 0.8f));
		renderer::draw_rounded_line(Drag.Origin, Mouse, 1.2f * Scale, WithAlpha(HitTint, 0.8f));
		renderer::draw_filled_circle(Drag.Origin, 2.5f * Scale, HitTint);
	}

	const float Cross = 7.f * Scale;
	renderer::draw_rounded_line(vec2(Mouse.x - Cross, Mouse.y), vec2(Mouse.x + Cross, Mouse.y), 1.f * Scale, HitTint);
	renderer::draw_rounded_line(vec2(Mouse.x, Mouse.y - Cross), vec2(Mouse.x, Mouse.y + Cross), 1.f * Scale, HitTint);
	renderer::draw_circle(Mouse, 4.f * Scale, 1.f * Scale, HitTint);

	int ActiveGroups = 0;
	for (const GroupItem& Item : Workspace.Groups)
	{
		if (Item.InUse)
			ActiveGroups++;
	}

	int ActiveNodes = 0;
	for (const LayoutNode& Node : Workspace.Nodes)
	{
		if (Node.InUse)
			ActiveNodes++;
	}

	const DockZone HoverZone = HoverGroup >= 0 ? Workspace.ZoneAt(HoverGroup, Mouse) : DockZone::None;
	const int HoverDivider = Workspace.DividerAt(Mouse);
	const float StatusWidth = std::min(820.f * Scale, WorkspaceRect.width() - 16.f * Scale);
	const rect StatusBar(WorkspaceRect.left + 8.f * Scale, WorkspaceRect.bottom - 76.f * Scale,
		WorkspaceRect.left + 8.f * Scale + StatusWidth, WorkspaceRect.bottom - 8.f * Scale);

	renderer::draw_filled_rounded_rect(StatusBar, 6.f * Scale, WithAlpha(Theme.Window, 0.94f));
	renderer::draw_rounded_rect(StatusBar, 6.f * Scale, 1.f * Scale, WithAlpha(Theme.Border, 0.9f));
	renderer::draw_text_f(vec2(StatusBar.left + 6.f * Scale, StatusBar.top + 3.f * Scale), FontSize, text_align::left, Theme.Text,
		L"F1 rects  mouse %.0f,%.0f  hit G%d  zone %s  divider %d  nodes %d",
		Mouse.x, Mouse.y, HoverGroup, ZoneName(HoverZone), HoverDivider, ActiveNodes);
	renderer::draw_text_f(vec2(StatusBar.left + 6.f * Scale, StatusBar.top + 18.f * Scale), FontSize, text_align::left, Theme.Muted,
		L"drag %d started %d target G%d zone %s edge %d root %d  floats %zu  groups %d",
		Drag.Kind, Drag.Started ? 1 : 0, Drag.Target, ZoneName(Drag.Zone), Drag.EdgeActive ? 1 : 0, Drag.RootDrop ? 1 : 0,
		Workspace.FloatingOrder.size(), ActiveGroups);
	renderer::draw_text_f(vec2(StatusBar.left + 6.f * Scale, StatusBar.top + 33.f * Scale), FontSize, text_align::left, Theme.Muted,
		L"dock green  float amber  surface purple  hit pink  target/guide blue");
	renderer::draw_text_f(vec2(StatusBar.left + 6.f * Scale, StatusBar.top + 48.f * Scale), FontSize, text_align::left, Theme.Muted,
		L"widgets: control amber  layout green  inner blue  absolute violet");
}

void UiShell::UpdateMotion(float Delta)
{
	const float Step = std::clamp(Delta, 0.f, 0.05f);
	const float Factor = 1.f - std::exp(-Step / 0.04f);
	const float MorphFactor = 1.f - std::exp(-Step / 0.055f);

	const bool TargetValid = Drag.Target >= 0 && Drag.Target < static_cast<int>(Workspace.Groups.size()) &&
		Workspace.Groups[Drag.Target].InUse;
	const bool ZoneVisible = Drag.Started && !Drag.EdgeActive && TargetValid &&
		!Workspace.Groups[Drag.Target].Floating && Drag.Zone != DockZone::None;
	const bool GuideHint = Drag.Kind == 2 && TargetValid && !Workspace.Groups[Drag.Target].Floating;
	const bool GuideVisible = Drag.Started && !Drag.EdgeActive &&
		(Drag.RootDrop || (TargetValid && (Drag.Zone != DockZone::None || GuideHint)));
	const bool EdgeVisible = Drag.Started && Drag.EdgeActive;

	if (ZoneVisible)
	{
		const rect Bounds = Workspace.Groups[Drag.Target].Bounds;
		rect Target = Bounds;

		switch (Drag.Zone)
		{
		case DockZone::Left:
			Target.right = Bounds.center().x;
			break;
		case DockZone::Right:
			Target.left = Bounds.center().x;
			break;
		case DockZone::Top:
			Target.bottom = Bounds.center().y;
			break;
		case DockZone::Bottom:
			Target.top = Bounds.center().y;
			break;
		default:
			break;
		}

		if (!ZoneAnimValid || ZoneMotionGroup != Drag.Target)
		{
			ZoneAnimRect = Target;
			ZoneAnimValid = true;
			ZoneMotionGroup = Drag.Target;
		}

		ZoneTargetRect = Target;
		ZoneAnimRect = LerpRect(ZoneAnimRect, ZoneTargetRect, MorphFactor);

		rect Cells[5];
		ComputeGuideCells(Bounds, Cells);

		for (int Index = 0; Index < 5; Index++)
		{
			if (GuideZones[Index] == Drag.Zone)
				GuideTargetCell = Cells[Index];
		}

		if (!GuideCellValid)
			GuideAnimCell = GuideTargetCell;

		GuideCellValid = true;
		GuideAnimCell = LerpRect(GuideAnimCell, GuideTargetCell, MorphFactor);
	}
	else
	{
		GuideCellValid = false;
	}

	if (EdgeVisible && Drag.EdgeZone != DockZone::None)
	{
		if (!EdgeAnimValid)
			EdgeAnimRect = Drag.EdgeRect;

		EdgeAnimValid = true;
		EdgeAnimRect = LerpRect(EdgeAnimRect, Drag.EdgeRect, MorphFactor);
	}

	GuideAlpha += ((GuideVisible ? 1.f : 0.f) - GuideAlpha) * Factor;
	EdgeAlpha += ((EdgeVisible ? 1.f : 0.f) - EdgeAlpha) * Factor;

	if (!GuideVisible && GuideAlpha < 0.005f)
	{
		ZoneAnimValid = false;
		GuideCellValid = false;
		ZoneMotionGroup = -1;
	}

	if (!EdgeVisible && EdgeAlpha < 0.005f)
		EdgeAnimValid = false;
}

void UiShell::ComputeGuideCells(rect Bounds, rect Cells[5]) const
{
	const float Cell = std::clamp(std::min(Bounds.width(), Bounds.height()) * 0.17f, 24.f * Scale, 34.f * Scale);
	const float Step = Cell + 5.f * Scale;
	const vec2 Center = Bounds.center();
	const float Half = Cell * 0.5f;

	Cells[0] = rect(Center.x - Half, Center.y - Half, Center.x + Half, Center.y + Half);
	Cells[1] = rect(Center.x - Half - Step, Center.y - Half, Center.x + Half - Step, Center.y + Half);
	Cells[2] = rect(Center.x - Half + Step, Center.y - Half, Center.x + Half + Step, Center.y + Half);
	Cells[3] = rect(Center.x - Half, Center.y - Half - Step, Center.x + Half, Center.y + Half - Step);
	Cells[4] = rect(Center.x - Half, Center.y - Half + Step, Center.x + Half, Center.y + Half + Step);
}

DockZone UiShell::GuideZoneAt(vec2 Point, rect Bounds) const
{
	rect Cells[5];
	ComputeGuideCells(Bounds, Cells);

	for (int Index = 0; Index < 5; Index++)
	{
		if (PointInRect(Cells[Index], Point))
			return GuideZones[Index];
	}

	return DockZone::None;
}

rect UiShell::RootDropRegion() const
{
	const vec2 Center = WorkspaceRect.center();
	const float Width = WorkspaceRect.width() * 0.5f;
	const float Height = WorkspaceRect.height() * 0.5f;
	return rect(Center.x - Width * 0.5f, Center.y - Height * 0.5f,
		Center.x + Width * 0.5f, Center.y + Height * 0.5f);
}

rect UiShell::EdgeRegion(DockZone Zone) const
{
	const rect Area = WorkspaceRect;
	const vec2 Center = Area.center();

	switch (Zone)
	{
	case DockZone::Left:
		return rect(Area.left, Area.top, Center.x, Area.bottom);
	case DockZone::Right:
		return rect(Center.x, Area.top, Area.right, Area.bottom);
	case DockZone::Top:
		return rect(Area.left, Area.top, Area.right, Center.y);
	case DockZone::Bottom:
		return rect(Area.left, Center.y, Area.right, Area.bottom);
	case DockZone::TopLeft:
		return rect(Area.left, Area.top, Center.x, Center.y);
	case DockZone::TopRight:
		return rect(Center.x, Area.top, Area.right, Center.y);
	case DockZone::BottomLeft:
		return rect(Area.left, Center.y, Center.x, Area.bottom);
	case DockZone::BottomRight:
		return rect(Center.x, Center.y, Area.right, Area.bottom);
	default:
		return Area;
	}
}

void UiShell::DrawSettings()
{
	const UiTheme& Theme = CurrentTheme();

	renderer::draw_shadow(SettingsCard, 30.f * Scale, Theme.Shadow);
	renderer::draw_filled_rounded_rect(SettingsCard, 14.f * Scale, Opaque(Theme.Panel));
	renderer::draw_rounded_rect(SettingsCard, 14.f * Scale, 1.f, Theme.Border);

	const float TitleSize = UiTitleTextSize * Scale;
	const float TitleTop = SettingsCard.top + 16.f * Scale;
	renderer::draw_text(vec2(SettingsCard.left + 20.f * Scale, TitleTop), TitleSize, text_align::left, L"Settings", Theme.Text);

	const bool CloseHover = MouseInside && PointInRect(SettingsClose, Mouse);
	DrawCloseGlyph(SettingsClose, CloseHover ? Theme.Text : Theme.Muted);

	const float DividerTop = SettingsCard.top + 58.f * Scale;
	renderer::draw_filled_rect(rect(SettingsCard.left + 1.f * Scale, DividerTop, SettingsCard.right - 1.f * Scale, DividerTop + Scale), Theme.Border);

	const float LabelSize = 10.5f * Scale;
	const float Left = SettingsCard.left + 20.f * Scale;

	renderer::draw_text(vec2(Left, SettingsCard.top + 74.f * Scale), LabelSize, text_align::left, L"THEME", Theme.Muted);

	const std::vector<UiTheme>& Themes = UiThemes();

	for (size_t Index = 0; Index < ThemeRows.size(); Index++)
	{
		const rect Row = ThemeRows[Index];
		const bool Hovered = MouseInside && !PickerOpen && PointInRect(Row, Mouse);
		const bool Selected = static_cast<int>(Index) == ThemeIndex;

		if (Selected)
			renderer::draw_filled_rounded_rect(Row, 7.f * Scale, Theme.AccentSoft);
		else if (Hovered)
			renderer::draw_filled_rounded_rect(Row, 7.f * Scale, Theme.TabHover);

		if (Selected)
			renderer::draw_rounded_rect(Row, 7.f * Scale, 1.f * Scale, Theme.Accent);

		const float RowSize = 12.5f * Scale;
		const float RowTop = Row.center().y - renderer::line_height(RowSize) * 0.5f;

		if (Index < Themes.size())
			renderer::draw_text(vec2(Row.left + 10.f * Scale, RowTop), RowSize, text_align::left, Themes[Index].Name,
				Selected || Hovered ? Theme.Text : Theme.Muted);

		const vec2 Dot(Row.right - 13.f * Scale, Row.center().y);
		renderer::draw_filled_circle(Dot, 4.f * Scale, Themes[Index].Accent);
		renderer::draw_circle(Dot, 4.f * Scale, 1.f * Scale, Theme.Border);
	}

	renderer::draw_text(vec2(Left, GlassToggle.top - 16.f * Scale), LabelSize, text_align::left, L"GLASS", Theme.Muted);

	const float ToggleTop = GlassToggle.center().y - 10.f * Scale;
	const rect ToggleTrack(GlassToggle.left, ToggleTop, GlassToggle.left + 40.f * Scale, ToggleTop + 20.f * Scale);
	renderer::draw_filled_rounded_rect(ToggleTrack, 10.f * Scale, GlassMode ? Theme.Accent : Theme.Header);
	renderer::draw_rounded_rect(ToggleTrack, 10.f * Scale, 1.f * Scale, GlassMode ? Theme.Accent : Theme.Border);

	const float KnobX = GlassMode ? ToggleTrack.right - 10.f * Scale : ToggleTrack.left + 10.f * Scale;
	renderer::draw_filled_circle(vec2(KnobX, ToggleTrack.center().y), 7.f * Scale, GlassMode ? color(1.f, 1.f, 1.f, 1.f) : Theme.Muted);

	const float GlassTextSize = UiBodyTextSize * Scale;
	const float GlassTextTop = ToggleTrack.center().y - renderer::line_height(GlassTextSize) * 0.5f;
	renderer::draw_text(vec2(ToggleTrack.right + 12.f * Scale, GlassTextTop), GlassTextSize, text_align::left, L"Glassy background", Theme.Text);

	const float TintTop = GlassSlider.top - 14.f * Scale;
	renderer::draw_text(vec2(Left, TintTop), LabelSize, text_align::left, L"TINT", Theme.Muted);
	renderer::draw_text_f(vec2(GlassSlider.right, TintTop), LabelSize, text_align::right, Theme.Muted, L"%.0f%%", GlassTint * 100.f);

	const float Fraction = std::clamp((GlassTint - 0.12f) / 0.83f, 0.f, 1.f);
	const float TrackY = GlassSlider.center().y;
	const rect Track(GlassSlider.left, TrackY - 2.f * Scale, GlassSlider.right, TrackY + 2.f * Scale);
	renderer::draw_filled_rounded_rect(Track, 2.f * Scale, GlassMode ? Theme.Border : WithAlpha(Theme.Border, 0.5f));

	if (GlassMode)
	{
		const rect Fill(GlassSlider.left, TrackY - 2.f * Scale, GlassSlider.left + GlassSlider.width() * Fraction, TrackY + 2.f * Scale);
		renderer::draw_filled_rounded_rect(Fill, 2.f * Scale, Theme.Accent);
	}

	renderer::draw_filled_circle(vec2(GlassSlider.left + GlassSlider.width() * Fraction, TrackY), 5.f * Scale,
		GlassMode ? Theme.Accent : Theme.Muted);
	renderer::draw_circle(vec2(GlassSlider.left + GlassSlider.width() * Fraction, TrackY), 5.f * Scale, 1.f * Scale, Theme.Border);

	const float SurfaceTop = SurfaceSlider.top - 14.f * Scale;
	renderer::draw_text(vec2(Left, SurfaceTop), LabelSize, text_align::left, L"SURFACE", Theme.Muted);
	renderer::draw_text_f(vec2(SurfaceSlider.right, SurfaceTop), LabelSize, text_align::right, Theme.Muted, L"%.0f%%", SurfaceOpacity * 100.f);

	const float SurfaceFraction = std::clamp((SurfaceOpacity - 0.20f) / 0.75f, 0.f, 1.f);
	const float SurfaceY = SurfaceSlider.center().y;
	const rect SurfaceTrack(SurfaceSlider.left, SurfaceY - 2.f * Scale, SurfaceSlider.right, SurfaceY + 2.f * Scale);
	renderer::draw_filled_rounded_rect(SurfaceTrack, 2.f * Scale, GlassMode ? Theme.Border : WithAlpha(Theme.Border, 0.5f));

	if (GlassMode)
	{
		const rect SurfaceFill(SurfaceSlider.left, SurfaceY - 2.f * Scale, SurfaceSlider.left + SurfaceSlider.width() * SurfaceFraction, SurfaceY + 2.f * Scale);
		renderer::draw_filled_rounded_rect(SurfaceFill, 2.f * Scale, Theme.Accent);
	}

	renderer::draw_filled_circle(vec2(SurfaceSlider.left + SurfaceSlider.width() * SurfaceFraction, SurfaceY), 5.f * Scale,
		GlassMode ? Theme.Accent : Theme.Muted);
	renderer::draw_circle(vec2(SurfaceSlider.left + SurfaceSlider.width() * SurfaceFraction, SurfaceY), 5.f * Scale, 1.f * Scale, Theme.Border);

	const float ColorLeft = ColorRows.empty() ? SettingsCard.left + 330.f * Scale : ColorRows.front().left;
	renderer::draw_text(vec2(ColorLeft, SettingsCard.top + 74.f * Scale), LabelSize, text_align::left, L"COLORS", Theme.Muted);

	for (size_t Index = 0; Index < ColorRows.size() && Index < sizeof(ColorRoles) / sizeof(ColorRoles[0]); Index++)
	{
		const rect Row = ColorRows[Index];
		const bool Hovered = MouseInside && !PickerOpen && PointInRect(Row, Mouse);

		if (Hovered)
			renderer::draw_filled_rounded_rect(Row, 6.f * Scale, Theme.TabHover);

		const float RowSize = UiBodyTextSize * Scale;
		const float RowTop = Row.center().y - renderer::line_height(RowSize) * 0.5f;
		renderer::draw_text(vec2(Row.left + 8.f * Scale, RowTop), RowSize, text_align::left, ColorRoles[Index].Name, Theme.Text);

		const color Value = ThemeBase.*(ColorRoles[Index].Field);
		const rect Swatch(Row.right - 30.f * Scale, Row.center().y - 9.f * Scale, Row.right - 6.f * Scale, Row.center().y + 9.f * Scale);

		renderer::draw_filled_rounded_rect(Swatch, 5.f * Scale, Value);
		renderer::draw_rounded_rect(Swatch, 5.f * Scale, 1.f * Scale, Hovered ? Theme.Accent : Theme.Border);
	}

	if (!ColorRows.empty())
	{
		renderer::draw_text(vec2(ColorLeft, ColorRows.back().bottom + 8.f * Scale), 9.5f * Scale, text_align::left,
			L"Click a swatch to open the color picker.", Theme.Muted);
	}
}

void UiShell::DrawContextMenu()
{
	const UiTheme& Theme = CurrentTheme();

	renderer::draw_shadow(MenuRect, 18.f * Scale, Theme.Shadow);
	renderer::draw_filled_rounded_rect(MenuRect, 8.f * Scale, Opaque(Theme.Menu));
	renderer::draw_rounded_rect(MenuRect, 8.f * Scale, 1.f * Scale, Theme.MenuBorder);

	const float ItemSize = UiBodyTextSize * Scale;
	const float SeparatorY = TabMenuItemRect(3).top - 3.5f * Scale;
	renderer::draw_filled_rect(rect(MenuRect.left + 8.f * Scale, SeparatorY, MenuRect.right - 8.f * Scale, SeparatorY + Scale), Theme.MenuBorder);

	for (int Index = 0; Index < 5; Index++)
	{
		const rect Row = TabMenuItemRect(Index);
		const bool Hovered = MenuHover == Index;

		if (Hovered)
			renderer::draw_filled_rounded_rect(Row, 5.f * Scale, Theme.MenuHover);

		const bool Destructive = Index >= 3;
		const color Tint = Theme.Text;
		const color Danger = Theme.Danger;
		const float Top = Row.center().y - renderer::line_height(ItemSize) * 0.5f;

		renderer::draw_text(vec2(Row.left + 10.f * Scale, Top), ItemSize, text_align::left, TabMenuLabels[Index],
			Destructive ? Danger : Tint);
	}
}

void UiShell::DrawGear(vec2 Center, float Radius, color Tint)
{
	for (int Index = 0; Index < 8; Index++)
	{
		const float Angle = static_cast<float>(Index) * 0.78539816f;
		const vec2 Direction(std::cos(Angle), std::sin(Angle));
		const vec2 Inner(Center.x + Direction.x * Radius * 0.52f, Center.y + Direction.y * Radius * 0.52f);
		const vec2 Outer(Center.x + Direction.x * Radius, Center.y + Direction.y * Radius);
		renderer::draw_rounded_line(Inner, Outer, 2.4f * Scale, Tint);
	}

	renderer::draw_circle(Center, Radius * 0.55f, 2.2f * Scale, Tint);
}

void UiShell::DrawCloseGlyph(rect Bounds, color Tint)
{
	const float Inset = std::min(Bounds.width(), Bounds.height()) * 0.29f;
	renderer::draw_rounded_line(vec2(Bounds.left + Inset, Bounds.top + Inset), vec2(Bounds.right - Inset, Bounds.bottom - Inset), 1.5f * Scale, Tint);
	renderer::draw_rounded_line(vec2(Bounds.right - Inset, Bounds.top + Inset), vec2(Bounds.left + Inset, Bounds.bottom - Inset), 1.5f * Scale, Tint);
}

void UiShell::DrawPlusGlyph(rect Bounds, color Tint)
{
	const vec2 Center = Bounds.center();
	const float Extent = std::min(Bounds.width(), Bounds.height()) * 0.26f;
	renderer::draw_rounded_line(vec2(Center.x - Extent, Center.y), vec2(Center.x + Extent, Center.y), 1.7f * Scale, Tint);
	renderer::draw_rounded_line(vec2(Center.x, Center.y - Extent), vec2(Center.x, Center.y + Extent), 1.7f * Scale, Tint);
}

void UiShell::DrawGripGlyph(rect Bounds, color Tint)
{
	if (Bounds.width() < 2.f || Bounds.height() < 2.f)
		return;

	const vec2 Center = Bounds.center();
	const float StepX = 4.f * Scale;
	const float StepY = 4.5f * Scale;

	for (int Row = -1; Row <= 1; Row++)
	{
		for (int Column = 0; Column <= 1; Column++)
		{
			const vec2 Dot(Center.x + (static_cast<float>(Column) - 0.5f) * StepX, Center.y + static_cast<float>(Row) * StepY);
			renderer::draw_filled_circle(Dot, 1.2f * Scale, Tint);
		}
	}
}

int UiShell::TargetGroup() const
{
	if (ActiveGroup >= 0 && ActiveGroup < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[ActiveGroup].InUse)
		return ActiveGroup;

	return Workspace.DefaultGroup();
}

void UiShell::CreatePanelInTarget()
{
	int Group = TargetGroup();

	if (Group < 0)
	{
		Workspace.EnsureGroup(WorkspaceRect);
		Group = TargetGroup();
	}

	if (Group < 0)
		return;

	PanelCounter++;
	CreatePanelWithContent(L"Panel " + std::to_wstring(PanelCounter), Group, true, DefaultBuild);
	ActiveGroup = Group;
}

void UiShell::AddPanelToGroup(int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	PanelCounter++;
	CreatePanelWithContent(L"Panel " + std::to_wstring(PanelCounter), Group, true, DefaultBuild);
	ActiveGroup = Group;
}

void UiShell::StartTabDrag(int Group, int Panel, vec2 Point)
{
	Drag = DragInfo{};
	Drag.Kind = 1;
	Drag.Panel = Panel;
	Drag.Group = Group;
	Drag.Origin = Point;
}

void UiShell::StartGroupDrag(int Group, vec2 Point)
{
	Drag = DragInfo{};
	Drag.Kind = 2;
	Drag.Group = Group;
	Drag.Origin = Point;
	Drag.StartBounds = Workspace.Groups[Group].Bounds;
}

void UiShell::HandleGroupPress(int GroupIndex, vec2 Point, bool Floating)
{
	GroupItem& Item = Workspace.Groups[GroupIndex];
	ActiveGroup = GroupIndex;

	if (Floating)
	{
		const int Edge = ResizeEdgeAt(Item.Bounds, Point);
		if (Edge != 0)
		{
			Drag = DragInfo{};
			Drag.Kind = 4;
			Drag.Group = GroupIndex;
			Drag.Edge = Edge;
			Drag.Started = true;
			Drag.Origin = Point;
			Drag.StartBounds = Item.Bounds;
			HoverCursor = EdgeCursor(Edge);
			return;
		}
	}

	for (const TabRect& Tab : Item.Tabs)
	{
		if (PointInRect(Tab.Close, Point))
		{
			Workspace.ClosePanel(Tab.Panel);
			return;
		}
	}

	if (PointInRect(Item.NewTab, Point))
	{
		AddPanelToGroup(GroupIndex);
		return;
	}

	if (Floating && PointInRect(Item.Grip, Point))
	{
		StartGroupDrag(GroupIndex, Point);
		return;
	}

	for (const TabRect& Tab : Item.Tabs)
	{
		if (PointInRect(Tab.Bounds, Point))
		{
			Workspace.ActivatePanel(GroupIndex, Tab.Panel);
			StartTabDrag(GroupIndex, Tab.Panel, Point);
			return;
		}
	}

	if (PointInRect(Item.Header, Point))
	{
		StartGroupDrag(GroupIndex, Point);
		return;
	}

	if (Point.y >= Item.Header.bottom && PointInRect(GroupContentBounds(GroupIndex), Point))
	{
		DispatchContentMouseDown(GroupIndex, Point);
	}
}

void UiShell::UpdateDropTarget(vec2 Point, int IgnoreGroup)
{
	Drag.Target = -1;
	Drag.Zone = DockZone::None;
	Drag.TabIndex = 0;
	Drag.EdgeActive = false;
	Drag.RootDrop = false;
	Drag.EdgeZone = DockZone::None;
	Drag.EdgeRect = rect();

	const float Margin = 12.f * Scale;
	const bool InX = Point.x >= WorkspaceRect.left - Margin && Point.x <= WorkspaceRect.right + Margin;
	const bool InY = Point.y >= WorkspaceRect.top - Margin && Point.y <= WorkspaceRect.bottom + Margin;

	if (!InX || !InY)
		return;

	const int HoverGroup = Workspace.GroupAt(Point, IgnoreGroup);
	const bool OverTabHeader = Drag.Kind == 1 && HoverGroup >= 0 && HoverGroup < static_cast<int>(Workspace.Groups.size()) &&
		Point.y < Workspace.Groups[HoverGroup].Header.bottom;

	if (!OverTabHeader)
	{
		const bool Left = Point.x >= WorkspaceRect.left - Margin && Point.x <= WorkspaceRect.left + Margin;
		const bool Right = Point.x >= WorkspaceRect.right - Margin && Point.x <= WorkspaceRect.right + Margin;
		const bool Top = Point.y >= WorkspaceRect.top - Margin && Point.y <= WorkspaceRect.top + Margin;
		const bool Bottom = Point.y >= WorkspaceRect.bottom - Margin && Point.y <= WorkspaceRect.bottom + Margin;

		DockZone Edge = DockZone::None;

		if (Left && Top)
			Edge = DockZone::TopLeft;
		else if (Right && Top)
			Edge = DockZone::TopRight;
		else if (Left && Bottom)
			Edge = DockZone::BottomLeft;
		else if (Right && Bottom)
			Edge = DockZone::BottomRight;
		else if (Left)
			Edge = DockZone::Left;
		else if (Right)
			Edge = DockZone::Right;
		else if (Top)
			Edge = DockZone::Top;
		else if (Bottom)
			Edge = DockZone::Bottom;

		if (Edge != DockZone::None)
		{
			Drag.EdgeActive = true;
			Drag.EdgeZone = Edge;
			Drag.EdgeRect = EdgeRegion(Edge);
			return;
		}
	}

	if (!PointInRect(WorkspaceRect, Point))
		return;

	const int Group = HoverGroup;
	if (Group < 0)
	{
		if (Workspace.Root < 0 && PointInRect(RootDropRegion(), Point))
			Drag.RootDrop = true;

		return;
	}

	const rect GroupBounds = Workspace.Groups[Group].Bounds;
	const DockZone Guide = GuideZoneAt(Point, GroupBounds);

	if (Drag.Kind == 2)
	{
		if (Workspace.Groups[Group].Floating)
		{
			if (Point.y >= Workspace.Groups[Group].Header.bottom)
				return;

			Drag.Target = Group;
			Drag.Zone = DockZone::Center;
			Drag.TabIndex = Workspace.InsertIndexAt(Group, Point.x);
			return;
		}

		rect Cells[5];
		ComputeGuideCells(GroupBounds, Cells);

		rect Plate = Cells[0];
		for (int Index = 1; Index < 5; Index++)
		{
			Plate.left = std::min(Plate.left, Cells[Index].left);
			Plate.top = std::min(Plate.top, Cells[Index].top);
			Plate.right = std::max(Plate.right, Cells[Index].right);
			Plate.bottom = std::max(Plate.bottom, Cells[Index].bottom);
		}

		const float HintPad = 23.f * Scale;
		const rect Hint(Plate.left - HintPad, Plate.top - HintPad, Plate.right + HintPad, Plate.bottom + HintPad);

		if (!PointInRect(Hint, Point))
			return;

		Drag.Target = Group;
		Drag.Zone = Guide;
		Drag.TabIndex = Workspace.InsertIndexAt(Group, Point.x);
		return;
	}

	Drag.Target = Group;
	Drag.Zone = Workspace.ZoneAt(Group, Point);
	Drag.TabIndex = Workspace.InsertIndexAt(Group, Point.x);

	if (Guide != DockZone::None)
		Drag.Zone = Guide;

	if (Workspace.Groups[Group].Floating)
		Drag.Zone = DockZone::Center;
}

void UiShell::UpdateGroupDrag(vec2 Point)
{
	if (Drag.Group < 0 || Drag.Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Drag.Group].InUse)
		return;

	GroupItem& Item = Workspace.Groups[Drag.Group];

	if (!Item.Floating)
	{
		const rect Previous = Drag.StartBounds;
		Workspace.DetachGroup(Drag.Group);

		rect Restored = Previous;

		if (Previous.width() >= WorkspaceRect.width() - 2.f * Scale && Previous.height() >= WorkspaceRect.height() - 2.f * Scale)
		{
			const float Width = std::min(std::max(Previous.width() * 0.7f, 420.f * Scale), WorkspaceRect.width());
			const float Height = std::min(std::max(Previous.height() * 0.7f, 300.f * Scale), WorkspaceRect.height());
			const float Left = std::clamp(Drag.Origin.x - Width * 0.3f, WorkspaceRect.left, WorkspaceRect.right - Width);
			const float Top = std::clamp(Drag.Origin.y - 16.f * Scale, WorkspaceRect.top, WorkspaceRect.bottom - Height);
			Restored = rect(Left, Top, Left + Width, Top + Height);
		}

		Item.Bounds = Restored;
		Drag.StartBounds = Restored;
	}

	const vec2 Offset = Drag.Origin - Drag.StartBounds.point();
	Item.Bounds = rect(Point - Offset, Drag.StartBounds.size());
	Item.TargetBounds = Item.Bounds;
	Item.AnimRemaining = 0.f;

	UpdateDropTarget(Point, Drag.Group);
}

void UiShell::ApplyDividerDrag(vec2 Point)
{
	if (Drag.Divider < 0 || Drag.Divider >= static_cast<int>(Workspace.Nodes.size()))
		return;

	LayoutNode& Node = Workspace.Nodes[Drag.Divider];
	if (!Node.InUse || Node.Leaf)
		return;

	const float Span = Node.Horizontal ? Node.Bounds.width() : Node.Bounds.height();
	if (Span <= 1.f)
		return;

	const float Ratio = Node.Horizontal ? (Point.x - Node.Bounds.left) / Span : (Point.y - Node.Bounds.top) / Span;
	const float MinRatio = std::clamp(120.f * Scale / Span, 0.05f, 0.5f);
	Node.Ratio = std::clamp(Ratio, MinRatio, 1.f - MinRatio);
}

void UiShell::ApplyFloatResize(vec2 Point)
{
	if (Drag.Group < 0 || Drag.Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Drag.Group].InUse)
		return;

	const float MinWidth = 280.f * Scale;
	const float MinHeight = 180.f * Scale;

	float Left = Drag.StartBounds.left;
	float Top = Drag.StartBounds.top;
	float Right = Drag.StartBounds.right;
	float Bottom = Drag.StartBounds.bottom;

	if (Drag.Edge & 1)
		Left = std::min(Point.x, Right - MinWidth);
	if (Drag.Edge & 2)
		Right = std::max(Point.x, Left + MinWidth);
	if (Drag.Edge & 4)
		Top = std::min(Point.y, Bottom - MinHeight);
	if (Drag.Edge & 8)
		Bottom = std::max(Point.y, Top + MinHeight);

	Workspace.Groups[Drag.Group].Bounds = rect(Left, Top, Right, Bottom);
	Workspace.Groups[Drag.Group].TargetBounds = Workspace.Groups[Drag.Group].Bounds;
	Workspace.Groups[Drag.Group].AnimRemaining = 0.f;
}

void UiShell::TearOffPanel(int Panel, vec2 Point, rect Target)
{
	if (Panel < 0 || Panel >= static_cast<int>(Workspace.Panels.size()) || !Workspace.Panels[Panel].Open)
		return;

	const bool Snap = Target.width() > 1.f && Target.height() > 1.f;
	const float DefaultWidth = 340.f * Scale;
	const float DefaultHeight = 240.f * Scale;
	rect Bounds(Point.x - 40.f * Scale, Point.y - 16.f * Scale,
		Point.x - 40.f * Scale + DefaultWidth, Point.y - 16.f * Scale + DefaultHeight);

	if (Snap)
	{
		const float Width = std::min(280.f * Scale, Target.width());
		const float Height = std::min(190.f * Scale, Target.height());
		const float Left = std::clamp(Point.x - Width * 0.5f, Target.left, Target.right - Width);
		const float Top = std::clamp(Point.y - Height * 0.5f, Target.top, Target.bottom - Height);
		Bounds = rect(Left, Top, Left + Width, Top + Height);
	}

	const int Group = Workspace.CreateFloatingGroup(Bounds);
	Workspace.DockPanel(Panel, Group, DockZone::Center, 0);

	if (Snap)
		Workspace.AnimateGroupTo(Group, Target);

	if (Workspace.Groups[Group].InUse)
		ActiveGroup = Group;
}

int UiShell::ResizeEdgeAt(rect Bounds, vec2 Point) const
{
	if (!PointInRect(Bounds, Point))
		return 0;

	const float ZoneSize = 6.f * Scale;
	int Edge = 0;

	if (Point.x - Bounds.left <= ZoneSize)
		Edge |= 1;
	if (Bounds.right - Point.x <= ZoneSize)
		Edge |= 2;
	if (Point.y - Bounds.top <= ZoneSize)
		Edge |= 4;
	if (Bounds.bottom - Point.y <= ZoneSize)
		Edge |= 8;

	return Edge;
}

int UiShell::EdgeCursor(int Edge) const
{
	switch (Edge)
	{
	case 1:
	case 2:
		return 1;
	case 4:
	case 8:
		return 2;
	case 5:
	case 10:
		return 3;
	case 6:
	case 9:
		return 4;
	default:
		return 0;
	}
}

void UiShell::UpdateHoverCursor(vec2 Point)
{
	HoverCursor = 0;

	if (!MouseInside || SettingsOpen)
		return;

	for (size_t Index = Workspace.FloatingOrder.size(); Index > 0; Index--)
	{
		const int Group = Workspace.FloatingOrder[Index - 1];
		if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
			continue;

		if (!PointInRect(Workspace.Groups[Group].Bounds, Point))
			continue;

		const int Edge = ResizeEdgeAt(Workspace.Groups[Group].Bounds, Point);
		if (Edge != 0)
			HoverCursor = EdgeCursor(Edge);

		return;
	}

	const int Divider = Workspace.DividerAt(Point);
	if (Divider >= 0 && Divider < static_cast<int>(Workspace.Nodes.size()))
		HoverCursor = Workspace.Nodes[Divider].Horizontal ? 1 : 2;
}

void UiShell::OnMouseMove(vec2 Point)
{
	Mouse = Point;
	MouseInside = true;

	if (!Ready)
		return;

	if (ContentDragPanel >= 0 && ContentDragKind != 0)
	{
		if (PanelContent* Content = ContentForPanel(ContentDragPanel))
			Content->OnMouseMove(Point, ContentDragNode, ContentDragKind);

		return;
	}

	if (PickerOpen && DraggingPickerSv)
	{
		const float Width = std::max(1.f, PickerSv.width());
		const float Height = std::max(1.f, PickerSv.height());
		PickerSat = std::clamp((Point.x - PickerSv.left) / Width, 0.f, 1.f);
		PickerVal = 1.f - std::clamp((Point.y - PickerSv.top) / Height, 0.f, 1.f);
		UpdatePickerColor();
		return;
	}

	if (PickerOpen && DraggingPickerHue)
	{
		const float Width = std::max(1.f, PickerHueBar.width());
		PickerHue = std::clamp((Point.x - PickerHueBar.left) / Width, 0.f, 1.f);
		UpdatePickerColor();
		return;
	}

	if (DraggingSurface && SettingsOpen)
	{
		const float Width = std::max(1.f, SurfaceSlider.width());
		const float Fraction = std::clamp((Point.x - SurfaceSlider.left) / Width, 0.f, 1.f);
		SurfaceOpacity = 0.20f + Fraction * 0.75f;
		ApplyGlass();
		return;
	}

	if (DraggingGlass && SettingsOpen)
	{
		const float Width = std::max(1.f, GlassSlider.width());
		const float Fraction = std::clamp((Point.x - GlassSlider.left) / Width, 0.f, 1.f);
		GlassTint = 0.12f + Fraction * 0.83f;
		ApplyGlass();
		return;
	}

	if (MenuOpen)
	{
		MenuHover = TabMenuHit(Point);
		return;
	}

	if (Drag.Kind != 0)
	{
		if ((Drag.Kind == 1 || Drag.Kind == 2) && !Drag.Started)
		{
			if ((Point - Drag.Origin).length() > 4.f * Scale)
				Drag.Started = true;
		}

		if (Drag.Started)
		{

			if (Drag.Kind == 1)
			{
				UpdateDropTarget(Point, -1);
				HoverCursor = 5;
			}
			else if (Drag.Kind == 2)
			{
				UpdateGroupDrag(Point);
				HoverCursor = 5;
			}
			else if (Drag.Kind == 3)
			{
				ApplyDividerDrag(Point);
			}
			else if (Drag.Kind == 4)
			{
				ApplyFloatResize(Point);
			}
		}

		return;
	}

	UpdateHoverCursor(Point);
}

void UiShell::OnMouseDown(vec2 Point)
{
	Mouse = Point;
	MouseInside = true;

	if (!Ready)
		return;

	ClearContentFocus();
	TooltipDismissed = true;

	if (PointInRect(WindowClose, Point))
	{
		DestroyWindow(Window);
		return;
	}

	if (PointInRect(WindowMin, Point))
	{
		ShowWindow(Window, SW_MINIMIZE);
		return;
	}

	if (PointInRect(WindowMax, Point))
	{
		ShowWindow(Window, IsZoomed(Window) ? SW_RESTORE : SW_MAXIMIZE);
		return;
	}

	if (MenuOpen)
	{
		const int Action = TabMenuHit(Point);
		MenuOpen = false;
		MenuHover = -1;

		if (Action >= 0)
			ExecuteTabMenu(Action);

		return;
	}

	if (PickerOpen)
	{
		if (PointInRect(PickerSv, Point))
		{
			DraggingPickerSv = true;
			DraggingPickerHue = false;
			const float Width = std::max(1.f, PickerSv.width());
			const float Height = std::max(1.f, PickerSv.height());
			PickerSat = std::clamp((Point.x - PickerSv.left) / Width, 0.f, 1.f);
			PickerVal = 1.f - std::clamp((Point.y - PickerSv.top) / Height, 0.f, 1.f);
			UpdatePickerColor();
			return;
		}

		if (PointInRect(PickerHueBar, Point))
		{
			DraggingPickerHue = true;
			DraggingPickerSv = false;
			const float Width = std::max(1.f, PickerHueBar.width());
			PickerHue = std::clamp((Point.x - PickerHueBar.left) / Width, 0.f, 1.f);
			UpdatePickerColor();
			return;
		}

		// Clicks on the picker body keep it open; only the close glyph or an outside click dismisses it.
		if (PointInRect(PickerRect, Point) && !PointInRect(PickerCloseRect(), Point))
			return;

		PickerOpen = false;
		DraggingPickerSv = false;
		DraggingPickerHue = false;

		if (PointInRect(PickerRect, Point))
			return;

		// An outside click inside the settings card falls through so another swatch or row reacts immediately.
		if (!PointInRect(SettingsCard, Point))
		{
			SettingsOpen = false;
			return;
		}
	}

	if (SettingsOpen)
	{
		if (PointInRect(SettingsClose, Point))
		{
			SettingsOpen = false;
			return;
		}

		if (PointInRect(GlassToggle, Point))
		{
			GlassMode = !GlassMode;
			ApplyGlass();
			return;
		}

		const rect SliderHit(GlassSlider.left - 8.f * Scale, GlassSlider.top - 8.f * Scale,
			GlassSlider.right + 8.f * Scale, GlassSlider.bottom + 8.f * Scale);

		if (PointInRect(SliderHit, Point))
		{
			DraggingGlass = true;
			DraggingSurface = false;
			const float Width = std::max(1.f, GlassSlider.width());
			const float Fraction = std::clamp((Point.x - GlassSlider.left) / Width, 0.f, 1.f);
			GlassTint = 0.12f + Fraction * 0.83f;
			ApplyGlass();
			return;
		}

		const rect SurfaceHit(SurfaceSlider.left - 8.f * Scale, SurfaceSlider.top - 8.f * Scale,
			SurfaceSlider.right + 8.f * Scale, SurfaceSlider.bottom + 8.f * Scale);

		if (PointInRect(SurfaceHit, Point))
		{
			DraggingSurface = true;
			DraggingGlass = false;
			const float Width = std::max(1.f, SurfaceSlider.width());
			const float Fraction = std::clamp((Point.x - SurfaceSlider.left) / Width, 0.f, 1.f);
			SurfaceOpacity = 0.20f + Fraction * 0.75f;
			ApplyGlass();
			return;
		}

		for (size_t Index = 0; Index < ThemeRows.size(); Index++)
		{
			if (!PointInRect(ThemeRows[Index], Point))
				continue;

			ThemeIndex = static_cast<int>(Index);
			ThemeBase = UiThemes()[static_cast<size_t>(ThemeIndex)];
			ApplyGlass();
			return;
		}

		for (size_t Index = 0; Index < ColorRows.size(); Index++)
		{
			if (!PointInRect(ColorRows[Index], Point))
				continue;

			OpenPicker(static_cast<int>(Index));
			return;
		}

		if (!PointInRect(SettingsCard, Point))
			SettingsOpen = false;

		return;
	}

	if (HandleContentOverlay(Point))
		return;

	if (RenamingPanel >= 0)
		CommitRename();

	if (PointInRect(SettingsButton, Point))
	{
		SettingsOpen = true;
		Drag = DragInfo{};
		return;
	}

	if (PointInRect(DebugButton, Point))
	{
		DebugRects = !DebugRects;
		Drag = DragInfo{};
		return;
	}

	if (PointInRect(NewPanelButton, Point))
	{
		CreatePanelInTarget();
		return;
	}

	for (size_t Index = Workspace.FloatingOrder.size(); Index > 0; Index--)
	{
		const int Group = Workspace.FloatingOrder[Index - 1];
		if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
			continue;

		if (!PointInRect(Workspace.Groups[Group].Bounds, Point))
			continue;

		Workspace.MoveGroupToFront(Group);
		HandleGroupPress(Group, Point, true);
		return;
	}

	const int Divider = Workspace.DividerAt(Point);
	if (Divider >= 0 && Divider < static_cast<int>(Workspace.Nodes.size()))
	{
		Drag = DragInfo{};
		Drag.Kind = 3;
		Drag.Divider = Divider;
		Drag.Started = true;
		Drag.Origin = Point;
		HoverCursor = Workspace.Nodes[Divider].Horizontal ? 1 : 2;
		return;
	}

	const int Group = Workspace.GroupAt(Point, -1);
	if (Group >= 0)
	{
		HandleGroupPress(Group, Point, false);
		return;
	}

	ActiveGroup = -1;
}

void UiShell::OnMouseUp(vec2 Point)
{
	Mouse = Point;
	DraggingGlass = false;
	DraggingSurface = false;
	DraggingPickerSv = false;
	DraggingPickerHue = false;

	if (!Ready)
		return;

	if (ContentDragPanel >= 0)
	{
		if (PanelContent* Content = ContentForPanel(ContentDragPanel))
			Content->OnMouseUp(Point);

		ContentDragPanel = -1;
		ContentDragNode = -1;
		ContentDragKind = 0;
	}

	if (Drag.Kind == 1 && Drag.Started)
	{
		if (Drag.EdgeActive && Drag.EdgeZone != DockZone::None)
		{
			if (Drag.EdgeZone == DockZone::Left || Drag.EdgeZone == DockZone::Right ||
				Drag.EdgeZone == DockZone::Top || Drag.EdgeZone == DockZone::Bottom)
				Workspace.DockPanelToEdge(Drag.Panel, Drag.EdgeZone, WorkspaceRect);
			else
				TearOffPanel(Drag.Panel, Point, Drag.EdgeRect);
		}
		else if (Drag.Target >= 0 && Drag.Zone != DockZone::None)
			Workspace.DockPanel(Drag.Panel, Drag.Target, Drag.Zone, Drag.TabIndex);
		else if (Drag.RootDrop && Drag.Panel >= 0 && Drag.Panel < static_cast<int>(Workspace.Panels.size()) &&
			Workspace.Panels[Drag.Panel].Group >= 0)
			Workspace.RestoreRoot(Workspace.Panels[Drag.Panel].Group, WorkspaceRect);
		else
			TearOffPanel(Drag.Panel, Point, rect());
	}
	else if (Drag.Kind == 2 && Drag.Started)
	{
		if (Drag.EdgeActive && Drag.EdgeZone != DockZone::None)
		{
			if (Drag.EdgeZone == DockZone::Left || Drag.EdgeZone == DockZone::Right ||
				Drag.EdgeZone == DockZone::Top || Drag.EdgeZone == DockZone::Bottom)
			{
				Workspace.DockGroupToEdge(Drag.Group, Drag.EdgeZone, WorkspaceRect);
			}
			else if (Drag.Group >= 0 && Drag.Group < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[Drag.Group].InUse)
			{
				if (!Workspace.Groups[Drag.Group].Floating)
					Workspace.DetachGroup(Drag.Group);

				Workspace.AnimateGroupTo(Drag.Group, Drag.EdgeRect);
			}
		}
		else if (Drag.Target >= 0 && Drag.Zone != DockZone::None)
		{
			Workspace.DockGroup(Drag.Group, Drag.Target, Drag.Zone);
		}
		else if (Drag.RootDrop)
		{
			Workspace.RestoreRoot(Drag.Group, WorkspaceRect);
		}
	}

	Drag = DragInfo{};
	UpdateHoverCursor(Point);
}

void UiShell::OnMouseWheel(float Delta, vec2 Point)
{
	if (!Ready || ModalOpen() || Drag.Kind != 0)
		return;

	const rect Client(0.f, 0.f, ClientSize.x, ClientSize.y);

	if (OpenDropPanel >= 0)
	{
		PanelContent* Popup = ContentForPanel(OpenDropPanel);

		if (Popup && IsPanelActive(OpenDropPanel) && PointInRect(Popup->DropdownPopup(OpenDropNode, Client), Point))
		{
			Popup->ScrollDropdown(OpenDropNode, Delta, Client);
			return;
		}

		// Scrolling content elsewhere would slide the dropdown out from under its open popup.
		if (Popup)
			Popup->CloseDropdown();

		OpenDropPanel = -1;
		OpenDropNode = -1;
	}

	const int Group = Workspace.GroupAt(Point);
	if (Group < 0)
		return;

	const GroupItem& Item = Workspace.Groups[Group];

	if (PointInRect(Item.Header, Point) && Item.Panels.size() > 1)
	{
		const auto Current = std::find(Item.Panels.begin(), Item.Panels.end(), Item.Active);
		const int Index = Current == Item.Panels.end() ? 0 : static_cast<int>(Current - Item.Panels.begin());
		const int Next = std::clamp(Index + (Delta < 0.f ? 1 : -1), 0, static_cast<int>(Item.Panels.size()) - 1);
		Workspace.ActivatePanel(Group, Item.Panels[Next]);
		ActiveGroup = Group;
		return;
	}

	PanelContent* Content = ActiveContent(Group);
	if (!Content || !PointInRect(GroupContentBounds(Group), Point))
		return;

	Content->OnWheel(Point, Delta);
}

void UiShell::OnMouseLeave()
{
	MouseInside = false;
	MenuHover = -1;
	UpdateHoverCursor(Mouse);
}

void UiShell::OnKeyDown(int Key)
{
	if (FocusedPanel >= 0)
	{
		PanelContent* Content = ContentForPanel(FocusedPanel);

		if (Content && IsPanelActive(FocusedPanel) && Content->OnKey(Key))
		{
			InputCaretTimer = 0.f;
			InputCaretVisible = true;

			if (!Content->HasFocus())
				FocusedPanel = -1;

			return;
		}
	}

	if (RenamingPanel >= 0)
	{
		if (Key == VK_RETURN)
		{
			CommitRename();
			return;
		}

		if (Key == VK_ESCAPE)
		{
			RenamingPanel = -1;
			RenameText.clear();
			return;
		}
	}

	if (Key == VK_F1)
	{
		DebugRects = !DebugRects;
		return;
	}

	if (Key == VK_ESCAPE)
	{
		if (Drag.Kind != 0 || ContentDragPanel >= 0)
		{
			OnCaptureLost();
			return;
		}

		if (OpenDropPanel >= 0)
		{
			if (PanelContent* Content = ContentForPanel(OpenDropPanel))
				Content->CloseDropdown();

			OpenDropPanel = -1;
			OpenDropNode = -1;
			return;
		}

		PickerOpen = false;
		MenuOpen = false;
		SettingsOpen = false;
	}
}

void UiShell::OnChar(wchar_t Char)
{
	if (FocusedPanel >= 0)
	{
		PanelContent* Content = ContentForPanel(FocusedPanel);

		if (Content && IsPanelActive(FocusedPanel))
		{
			Content->OnChar(Char);
			InputCaretTimer = 0.f;
			InputCaretVisible = true;

			if (!Content->HasFocus())
				FocusedPanel = -1;

			return;
		}
	}

	if (RenamingPanel < 0)
		return;

	CaretTimer = 0.f;
	CaretVisible = true;

	if (Char == L'\b')
	{
		if (RenameReplace)
		{
			RenameText.clear();
			RenameReplace = false;
			return;
		}

		if (!RenameText.empty())
			RenameText.pop_back();

		return;
	}

	if (Char == L'\r')
	{
		CommitRename();
		return;
	}

	if (Char == 27)
	{
		RenamingPanel = -1;
		RenameText.clear();
		return;
	}

	if (Char < 32)
		return;

	if (RenameReplace)
	{
		RenameText.clear();
		RenameReplace = false;
	}

	if (RenameText.size() < 48)
		RenameText.push_back(Char);
}

void UiShell::OnRightMouseDown(vec2 Point)
{
	Mouse = Point;
	MouseInside = true;

	if (!Ready)
		return;

	if (SettingsOpen)
	{
		SettingsOpen = false;
		PickerOpen = false;
		return;
	}

	if (RenamingPanel >= 0)
		CommitRename();

	MenuOpen = false;
	MenuHover = -1;
	TooltipDismissed = true;

	if (OpenDropPanel >= 0)
	{
		if (PanelContent* Content = ContentForPanel(OpenDropPanel))
			Content->CloseDropdown();

		OpenDropPanel = -1;
		OpenDropNode = -1;
	}

	const int Group = Workspace.GroupAt(Point);
	if (Group < 0)
		return;

	const GroupItem& Item = Workspace.Groups[Group];

	for (const TabRect& Tab : Item.Tabs)
	{
		if (PointInRect(Tab.Bounds, Point))
		{
			OpenTabMenu(Group, Tab.Panel, Point);
			return;
		}
	}
}

rect UiShell::TabMenuItemRect(int Index) const
{
	const float ItemHeight = 30.f * Scale;
	const float Pad = 6.f * Scale;
	const float Separator = 7.f * Scale;
	float Top = MenuRect.top + Pad + static_cast<float>(Index) * ItemHeight;

	if (Index >= 3)
		Top += Separator;

	return rect(MenuRect.left + Pad, Top, MenuRect.right - Pad, Top + ItemHeight);
}

int UiShell::TabMenuHit(vec2 Point) const
{
	if (!MenuOpen)
		return -1;

	for (int Index = 0; Index < 5; Index++)
	{
		if (PointInRect(TabMenuItemRect(Index), Point))
			return Index;
	}

	return -1;
}

void UiShell::OpenTabMenu(int Group, int Panel, vec2 Point)
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	const float ItemHeight = 30.f * Scale;
	const float Pad = 6.f * Scale;
	const float Separator = 7.f * Scale;
	const float Width = 190.f * Scale;
	const float Height = Pad * 2.f + ItemHeight * 5.f + Separator;

	const float Left = std::clamp(Point.x, 4.f * Scale, std::max(4.f * Scale, ClientSize.x - Width - 4.f * Scale));
	const float Top = std::clamp(Point.y, 4.f * Scale, std::max(4.f * Scale, ClientSize.y - Height - 4.f * Scale));

	MenuRect = rect(Left, Top, Left + Width, Top + Height);
	MenuGroup = Group;
	MenuPanel = Panel;
	MenuOpen = true;
	MenuHover = -1;

	if (Workspace.Groups[Group].Floating)
		Workspace.MoveGroupToFront(Group);

	Workspace.ActivatePanel(Group, Panel);
	ActiveGroup = Group;
}

void UiShell::ExecuteTabMenu(int Action)
{
	const int Panel = MenuPanel;
	const int Group = MenuGroup;
	MenuPanel = -1;
	MenuGroup = -1;

	if (Panel < 0 || Panel >= static_cast<int>(Workspace.Panels.size()) || !Workspace.Panels[Panel].Open)
		return;

	switch (Action)
	{
	case 0:
		BeginRename(Panel);
		break;
	case 1:
	{
		if (Group >= 0 && Group < static_cast<int>(Workspace.Groups.size()) && Workspace.Groups[Group].InUse)
		{
			const rect Bounds = Workspace.Groups[Group].Bounds;
			TearOffPanel(Panel, vec2(Bounds.left + 70.f * Scale, Bounds.top + 90.f * Scale), rect());
		}
		break;
	}
	case 2:
		if (Group >= 0)
			AddPanelToGroup(Group);
		break;
	case 3:
		Workspace.ClosePanel(Panel);
		break;
	case 4:
		CloseOtherPanels(Group, Panel);
		break;
	default:
		break;
	}
}

void UiShell::BeginRename(int Panel)
{
	if (Panel < 0 || Panel >= static_cast<int>(Workspace.Panels.size()) || !Workspace.Panels[Panel].Open)
		return;

	RenamingPanel = Panel;
	RenameText = Workspace.Panels[Panel].Title;
	RenameReplace = true;
	CaretTimer = 0.f;
	CaretVisible = true;

	const int Group = Workspace.Panels[Panel].Group;
	if (Group >= 0)
	{
		Workspace.ActivatePanel(Group, Panel);
		ActiveGroup = Group;
	}
}

void UiShell::CommitRename()
{
	if (RenamingPanel >= 0 && RenamingPanel < static_cast<int>(Workspace.Panels.size()) &&
		Workspace.Panels[RenamingPanel].Open && !RenameText.empty())
	{
		Workspace.Panels[RenamingPanel].Title = RenameText;
	}

	RenamingPanel = -1;
	RenameText.clear();
}

void UiShell::CloseOtherPanels(int Group, int KeepPanel)
{
	if (Group < 0 || Group >= static_cast<int>(Workspace.Groups.size()) || !Workspace.Groups[Group].InUse)
		return;

	std::vector<int> Others;
	for (int Panel : Workspace.Groups[Group].Panels)
	{
		if (Panel != KeepPanel)
			Others.push_back(Panel);
	}

	for (int Panel : Others)
		Workspace.ClosePanel(Panel);

	if (KeepPanel >= 0 && KeepPanel < static_cast<int>(Workspace.Panels.size()) && Workspace.Panels[KeepPanel].Open)
	{
		const int Current = Workspace.Panels[KeepPanel].Group;
		if (Current >= 0)
		{
			Workspace.ActivatePanel(Current, KeepPanel);
			ActiveGroup = Current;
		}
	}
}

LPCWSTR UiShell::CursorName() const
{
	switch (HoverCursor)
	{
	case 1:
		return IDC_SIZEWE;
	case 2:
		return IDC_SIZENS;
	case 3:
		return IDC_SIZENWSE;
	case 4:
		return IDC_SIZENESW;
	case 5:
		return IDC_SIZEALL;
	default:
		return IDC_ARROW;
	}
}

LRESULT UiShell::HitTest(vec2 Point) const
{
	if (!Window || ClientSize.x < 1.f || ClientSize.y < 1.f)
		return HTCLIENT;

	if (!IsZoomed(Window))
	{
		const float Border = 6.f * Scale;
		const bool Left = Point.x < Border;
		const bool Right = Point.x >= ClientSize.x - Border;
		const bool Top = Point.y < Border;
		const bool Bottom = Point.y >= ClientSize.y - Border;

		if (Top && Left)
			return HTTOPLEFT;
		if (Top && Right)
			return HTTOPRIGHT;
		if (Bottom && Left)
			return HTBOTTOMLEFT;
		if (Bottom && Right)
			return HTBOTTOMRIGHT;
		if (Left)
			return HTLEFT;
		if (Right)
			return HTRIGHT;
		if (Top)
			return HTTOP;
		if (Bottom)
			return HTBOTTOM;
	}

	if (Point.y < AppBarRect.bottom)
	{
		if (PointInRect(WindowMin, Point) || PointInRect(WindowMax, Point) || PointInRect(WindowClose, Point) ||
			PointInRect(SettingsButton, Point) || PointInRect(NewPanelButton, Point) || PointInRect(DebugButton, Point))
			return HTCLIENT;

		return HTCAPTION;
	}

	return HTCLIENT;
}
