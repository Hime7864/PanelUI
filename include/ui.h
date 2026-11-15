#pragma once

#include "graphics.hpp"
#include "theme.h"
#include "workspace.h"
#include "framework.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct UiPanelInit
{
	std::wstring Title;
	DockZone Zone = DockZone::Center;
	int RelativeTo = -1;
	bool Floating = false;
	rect FloatBounds;
	std::function<void(PanelContent&)> Build;
};

class UiShell
{
public:
	void Initialize(HWND Window);
	void Frame();
	void SetStartupLayout(std::vector<UiPanelInit> Panels);
	void SetTitle(std::wstring Value);
	// Content factory for tabs created by the shell ("+", New Panel, tab menu New panel).
	// Unset means those tabs open empty.
	void SetDefaultPanelBuild(std::function<void(PanelContent&)> Build);
	void OnMouseMove(vec2 Point);
	void OnMouseDown(vec2 Point);
	void OnRightMouseDown(vec2 Point);
	void OnMouseUp(vec2 Point);
	void OnMouseWheel(float Delta, vec2 Point);
	void OnMouseLeave();
	void OnKeyDown(int Key);
	void OnChar(wchar_t Char);
	void OnCaptureLost();
	LPCWSTR CursorName() const;
	LRESULT HitTest(vec2 Point) const;

private:
	struct DragInfo
	{
		int Kind = 0;
		int Panel = -1;
		int Group = -1;
		int Divider = -1;
		int Edge = 0;
		bool Started = false;
		vec2 Origin;
		rect StartBounds;
		int Target = -1;
		DockZone Zone = DockZone::None;
		int TabIndex = 0;
		bool EdgeActive = false;
		bool RootDrop = false;
		DockZone EdgeZone = DockZone::None;
		rect EdgeRect;
	};

	void UpdateMetrics();
	void EnsureStartup();
	int CreatePanelWithContent(const std::wstring& Title, int Group, bool Activate, const std::function<void(PanelContent&)>& Build);
	void EnsurePanelContent(int Panel, const std::function<void(PanelContent&)>& Build);
	PanelContent* ContentForPanel(int Panel);
	PanelContent* ActiveContent(int Group);
	int ActivePanel(int Group) const;
	rect GroupContentBounds(int Group) const;
	void LayoutContents(float Delta);
	void DrawGroupContent(int Group, const UiTheme& Theme);
	void DrawGroupContentText(int Group, const UiTheme& Theme, const std::vector<rect>& Clips);
	bool IsPanelActive(int Panel) const;
	void ClearContentFocus();
	void DispatchContentMouseDown(int Group, vec2 Point);
	void DrawContentOverlay();
	bool HandleContentOverlay(vec2 Point);
	bool ModalOpen() const;
	void UpdateHover(float Delta);
	void BuildOccluders();
	bool OccludedByOverlay(rect Area) const;
	void DrawTooltip();
	rect PickerCloseRect() const;
	const UiTheme& CurrentTheme() const;
	void DrawAppBar();
	void CollectGroups(int Node);
	void BuildGroupClips();
	void DrawGroupText(int Group);
	void DrawGroup(int Group);
	void DrawDividers();
	void DrawDragOverlay();
	void DrawDebugOverlay();
	void DrawDockGuide(int Group, float Alpha);
	void DrawGuideIcon(rect Cell, DockZone Zone, color Tint);
	void DrawEdgePreview(float Alpha);
	void DrawSettings();
	void DrawContextMenu();
	void DrawRenameField(const TabRect& Tab, const UiTheme& Theme);
	void DrawGear(vec2 Center, float Radius, color Tint);
	void DrawCloseGlyph(rect Bounds, float GlyphSize, color Tint);
	void DrawPlusGlyph(rect Bounds, float GlyphSize, color Tint);
	void DrawGripGlyph(rect Bounds, color Tint);
	void DrawTabStrip(int Group);
	void DrawTabDropMarker(int Group);
	void ApplyGlass();
	void ApplyBackdrop();
	void OpenPicker(int Role);
	void BuildPickerTextures();
	void DrawColorPicker();
	void UpdatePickerColor();
	void OpenTabMenu(int Group, int Panel, vec2 Point);
	void ExecuteTabMenu(int Action);
	int TabMenuHit(vec2 Point) const;
	rect TabMenuItemRect(int Index) const;
	void BeginRename(int Panel);
	void CommitRename();
	void CloseOtherPanels(int Group, int KeepPanel);
	void UpdateMotion(float Delta);
	void ComputeGuideCells(rect Bounds, rect Cells[5]) const;
	DockZone GuideZoneAt(vec2 Point, rect Bounds) const;
	rect RootDropRegion() const;
	rect EdgeRegion(DockZone Zone) const;
	int TargetGroup() const;
	void CreatePanelInTarget();
	void AddPanelToGroup(int Group);
	void HandleGroupPress(int Group, vec2 Point, bool Floating);
	void StartTabDrag(int Group, int Panel, vec2 Point);
	void StartGroupDrag(int Group, vec2 Point);
	void UpdateDropTarget(vec2 Point, int IgnoreGroup);
	void UpdateGroupDrag(vec2 Point);
	void ApplyDividerDrag(vec2 Point);
	void ApplyFloatResize(vec2 Point);
	void TearOffPanel(int Panel, vec2 Point, rect Target = rect());
	int ResizeEdgeAt(rect Bounds, vec2 Point) const;
	int EdgeCursor(int Edge) const;
	void UpdateHoverCursor(vec2 Point);

	HWND Window = nullptr;
	WorkspaceModel Workspace;
	bool Ready = false;
	bool MouseInside = false;
	bool SettingsOpen = false;
	bool DebugRects = false;
	bool GlassMode = true;
	bool DraggingGlass = false;
	bool DraggingSurface = false;
	bool MenuOpen = false;
	bool PickerOpen = false;
	bool DraggingPickerSv = false;
	bool DraggingPickerHue = false;
	bool RenameReplace = true;
	int PickerRole = -1;
	int MenuGroup = -1;
	int MenuPanel = -1;
	int MenuHover = -1;
	int RenamingPanel = -1;
	int ThemeIndex = 0;
	int ActiveGroup = -1;
	int PanelCounter = 0;
	int HoverCursor = 0;
	float GuideAlpha = 0.f;
	float EdgeAlpha = 0.f;
	rect ZoneAnimRect;
	rect ZoneTargetRect;
	bool ZoneAnimValid = false;
	int ZoneMotionGroup = -1;
	rect GuideAnimCell;
	rect GuideTargetCell;
	bool GuideCellValid = false;
	rect EdgeAnimRect;
	bool EdgeAnimValid = false;
	float GlassTint = 0.55f;
	float SurfaceOpacity = 0.55f;
	float PickerHue = 0.f;
	float PickerSat = 1.f;
	float PickerVal = 1.f;
	float PickerAlpha = 1.f;
	float PickerTextureHue = -1.f;
	texture_id PickerSvTexture = 0;
	texture_id PickerHueTexture = 0;
	float CaretTimer = 0.f;
	bool CaretVisible = true;
	int ContentDragPanel = -1;
	int ContentDragNode = -1;
	int ContentDragKind = 0;
	int FocusedPanel = -1;
	int HoverGroup = -1;
	int TooltipPanel = -1;
	int TooltipNode = -1;
	float TooltipTimer = 0.f;
	bool TooltipDismissed = false;
	rect TooltipBox;
	std::vector<rect> OverlayOccluders;
	int OpenDropPanel = -1;
	int OpenDropNode = -1;
	float InputCaretTimer = 0.f;
	bool InputCaretVisible = true;
	UiTheme ThemeBase;
	UiTheme Theme;
	std::wstring RenameText;
	std::wstring Title = L"Workspace";
	std::function<void(PanelContent&)> DefaultBuild;
	std::vector<std::unique_ptr<PanelContent>> PanelContents;
	std::vector<UiPanelInit> StartupPanels;
	vec2 Mouse;
	float Scale = 1.f;
	vec2 ClientSize;
	rect LastLayoutArea;
	bool LayoutSeen = false;
	rect AppBarRect;
	rect WorkspaceRect;
	rect SettingsButton;
	rect NewPanelButton;
	rect DebugButton;
	rect WindowMin;
	rect WindowMax;
	rect WindowClose;
	rect SettingsCard;
	rect SettingsClose;
	rect GlassToggle;
	rect GlassSlider;
	rect SurfaceSlider;
	rect PickerRect;
	rect PickerSv;
	rect PickerHueBar;
	rect MenuRect;
	std::vector<rect> ThemeRows;
	std::vector<rect> ColorRows;
	std::vector<int> DockedOrder;
	std::vector<int> DrawOrder;
	std::vector<std::vector<rect>> GroupClips;
	std::vector<bool> GroupOpaque;
	DragInfo Drag;
};
