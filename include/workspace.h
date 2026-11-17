#pragma once

#include "graphics.hpp"

#include <string>
#include <vector>

enum class DockZone
{
	None,
	Center,
	Left,
	Right,
	Top,
	Bottom,
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

struct PanelItem
{
	bool Open = false;
	int Group = -1;
	std::wstring Title;
	bool Closable = true;
};

struct TabRect
{
	int Panel = -1;
	rect Bounds;
	rect Close;
};

struct GroupItem
{
	bool InUse = false;
	bool Floating = false;
	std::vector<int> Panels;
	int Active = -1;
	rect Bounds;
	rect TargetBounds;
	float AnimRemaining = 0.f;
	rect Header;
	rect Grip;
	rect NewTab;
	std::vector<TabRect> Tabs;
};

struct LayoutNode
{
	bool InUse = false;
	bool Leaf = true;
	bool Horizontal = true;
	float Ratio = 0.5f;
	int First = -1;
	int Second = -1;
	int Parent = -1;
	int Group = -1;
	rect Bounds;
	rect Divider;
};

inline bool PointInRect(rect Bounds, vec2 Point)
{
	return Point.x >= Bounds.left && Point.x < Bounds.right && Point.y >= Bounds.top && Point.y < Bounds.bottom;
}

class WorkspaceModel
{
public:
	int CreatePanel(const std::wstring& Title, int Group, bool Activate);
	void EnsureGroup(rect Area);
	int CreateFloatingGroup(rect Bounds);
	void ClosePanel(int Panel);
	void ActivatePanel(int Group, int Panel);
	void DockPanel(int Panel, int TargetGroup, DockZone Zone, int TabIndex);
	void DockGroup(int Group, int TargetGroup, DockZone Zone);
	void DetachGroup(int Group);
	void MoveGroupToFront(int Group);
	void ClampFloatingGroups(rect Area);
	void Layout(rect Area, float Scale);
	void StepAnimations(float Delta);
	void BeginAnimation(int Group, float Duration = 0.2f);
	void BeginAnimationAll(float Duration = 0.2f);
	void AnimateGroupTo(int Group, rect Bounds);
	void RestoreRoot(int Group, rect Area);
	void DockGroupToEdge(int Group, DockZone Zone, rect Area);
	void DockPanelToEdge(int Panel, DockZone Zone, rect Area);
	// Sets how much of its parent split this group's leaf occupies (0.05..0.95).
	bool SetGroupShare(int Group, float Share);
	int GroupAt(vec2 Point) const;
	int GroupAt(vec2 Point, int IgnoreGroup) const;
	int DividerAt(vec2 Point) const;
	int InsertIndexAt(int Group, float X) const;
	DockZone ZoneAt(int Group, vec2 Point) const;
	int DefaultGroup() const;

	std::vector<PanelItem> Panels;
	std::vector<GroupItem> Groups;
	std::vector<LayoutNode> Nodes;
	std::vector<int> FloatingOrder;
	int Root = -1;
	float LayoutScale = 1.f;
	// Cleared by the shell when it suppresses panel creation: no "+" button in group headers.
	bool AllowNewTabs = true;

private:
	int AllocPanel();
	int AllocGroup();
	int AllocNode();
	void FreeNode(int Node);
	void ReorderPanel(int Panel, int Group, int Index);
	void CloseGroup(int Group);
	void RemoveGroupFromTree(int Group);
	void WrapRootWithGroup(int Group, DockZone Zone, rect Area);
	void SplitLeaf(int Leaf, DockZone Zone, int Group);
	void CollapseNode(int Node);
	void ComputeNode(int Node, rect Area);
	void BuildTabs(int Group);
	void RemovePanelFromGroup(int Panel, int Group);
	int LeafOfGroup(int Group) const;
};
