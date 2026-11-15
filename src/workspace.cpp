#include "workspace.h"
#include "theme.h"

#include <algorithm>
#include <cmath>

namespace
{
	rect CollapsedBounds(DockZone Zone, rect Area)
	{
		switch (Zone)
		{
		case DockZone::Left:
			return rect(Area.left, Area.top, Area.left, Area.bottom);
		case DockZone::Right:
			return rect(Area.right, Area.top, Area.right, Area.bottom);
		case DockZone::Top:
			return rect(Area.left, Area.top, Area.right, Area.top);
		case DockZone::Bottom:
			return rect(Area.left, Area.bottom, Area.right, Area.bottom);
		default:
			return Area;
		}
	}

	bool IsHalfZone(DockZone Zone)
	{
		return Zone == DockZone::Left || Zone == DockZone::Right || Zone == DockZone::Top || Zone == DockZone::Bottom;
	}
}

int WorkspaceModel::AllocPanel()
{
	for (size_t Index = 0; Index < Panels.size(); Index++)
	{
		if (!Panels[Index].Open)
			return static_cast<int>(Index);
	}

	Panels.push_back(PanelItem{});
	return static_cast<int>(Panels.size()) - 1;
}

int WorkspaceModel::AllocGroup()
{
	for (size_t Index = 0; Index < Groups.size(); Index++)
	{
		if (!Groups[Index].InUse)
			return static_cast<int>(Index);
	}

	Groups.push_back(GroupItem{});
	return static_cast<int>(Groups.size()) - 1;
}

int WorkspaceModel::AllocNode()
{
	for (size_t Index = 0; Index < Nodes.size(); Index++)
	{
		if (!Nodes[Index].InUse)
		{
			Nodes[Index] = LayoutNode{};
			Nodes[Index].InUse = true;
			return static_cast<int>(Index);
		}
	}

	Nodes.push_back(LayoutNode{});
	Nodes.back().InUse = true;
	return static_cast<int>(Nodes.size()) - 1;
}

void WorkspaceModel::FreeNode(int Node)
{
	if (Node < 0 || Node >= static_cast<int>(Nodes.size()))
		return;

	Nodes[Node] = LayoutNode{};
}

int WorkspaceModel::CreatePanel(const std::wstring& Title, int Group, bool Activate)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return -1;

	const int Panel = AllocPanel();
	Panels[Panel].Open = true;
	Panels[Panel].Group = Group;
	Panels[Panel].Title = Title;

	Groups[Group].Panels.push_back(Panel);
	if (Activate || Groups[Group].Active < 0)
		Groups[Group].Active = Panel;

	return Panel;
}

void WorkspaceModel::EnsureGroup(rect Area)
{
	if (Root >= 0)
		return;

	const int Group = AllocGroup();
	Groups[Group].InUse = true;
	Groups[Group].Floating = false;
	Groups[Group].Bounds = Area;
	Groups[Group].TargetBounds = Area;

	const int Node = AllocNode();
	Nodes[Node].Leaf = true;
	Nodes[Node].Group = Group;
	Nodes[Node].Parent = -1;

	Root = Node;
}

int WorkspaceModel::CreateFloatingGroup(rect Bounds)
{
	const int Group = AllocGroup();
	Groups[Group].InUse = true;
	Groups[Group].Floating = true;
	Groups[Group].Bounds = Bounds;
	Groups[Group].TargetBounds = Bounds;
	FloatingOrder.push_back(Group);
	return Group;
}

void WorkspaceModel::RemovePanelFromGroup(int Panel, int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()))
		return;

	std::vector<int>& List = Groups[Group].Panels;
	for (size_t Index = 0; Index < List.size(); Index++)
	{
		if (List[Index] != Panel)
			continue;

		List.erase(List.begin() + Index);

		if (Groups[Group].Active == Panel)
			Groups[Group].Active = List.empty() ? -1 : List[std::min(Index, List.size() - 1)];

		return;
	}
}

void WorkspaceModel::ClosePanel(int Panel)
{
	if (Panel < 0 || Panel >= static_cast<int>(Panels.size()) || !Panels[Panel].Open)
		return;

	const int Group = Panels[Panel].Group;
	if (Group >= 0)
	{
		RemovePanelFromGroup(Panel, Group);

		if (Groups[Group].Panels.empty())
			CloseGroup(Group);
	}

	Panels[Panel] = PanelItem{};
}

void WorkspaceModel::CloseGroup(int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	for (int Panel : Groups[Group].Panels)
	{
		Panels[Panel].Open = false;
		Panels[Panel].Group = -1;
	}

	Groups[Group].Panels.clear();
	Groups[Group].Active = -1;

	if (Groups[Group].Floating)
	{
		for (size_t Index = 0; Index < FloatingOrder.size(); Index++)
		{
			if (FloatingOrder[Index] == Group)
			{
				FloatingOrder.erase(FloatingOrder.begin() + Index);
				break;
			}
		}
	}
	else
	{
		RemoveGroupFromTree(Group);
	}

	Groups[Group] = GroupItem{};
	BeginAnimationAll();
}

void WorkspaceModel::ActivatePanel(int Group, int Panel)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	for (int Existing : Groups[Group].Panels)
	{
		if (Existing == Panel)
		{
			Groups[Group].Active = Panel;
			return;
		}
	}
}

void WorkspaceModel::ReorderPanel(int Panel, int Group, int Index)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()))
		return;

	std::vector<int>& List = Groups[Group].Panels;

	size_t Current = List.size();
	for (size_t Position = 0; Position < List.size(); Position++)
	{
		if (List[Position] == Panel)
		{
			Current = Position;
			break;
		}
	}

	if (Current >= List.size())
		return;

	List.erase(List.begin() + Current);

	if (Current < static_cast<size_t>(Index))
		Index--;

	Index = std::clamp(Index, 0, static_cast<int>(List.size()));
	List.insert(List.begin() + Index, Panel);
	Groups[Group].Active = Panel;
}

void WorkspaceModel::DockPanel(int Panel, int TargetGroup, DockZone Zone, int TabIndex)
{
	if (Panel < 0 || Panel >= static_cast<int>(Panels.size()) || !Panels[Panel].Open)
		return;

	if (TargetGroup < 0 || TargetGroup >= static_cast<int>(Groups.size()) || !Groups[TargetGroup].InUse)
		return;

	const int SourceGroup = Panels[Panel].Group;
	if (SourceGroup < 0)
		return;

	if (Groups[TargetGroup].Floating && Zone != DockZone::Center)
		Zone = DockZone::Center;

	if (SourceGroup == TargetGroup)
	{
		if (Zone == DockZone::Center)
		{
			ReorderPanel(Panel, TargetGroup, TabIndex);
			return;
		}

		if (Groups[TargetGroup].Panels.size() <= 1)
			return;
	}

	if (Zone == DockZone::Center)
	{
		RemovePanelFromGroup(Panel, SourceGroup);
		const bool SourceEmpty = Groups[SourceGroup].Panels.empty();
		const int Index = std::clamp(TabIndex, 0, static_cast<int>(Groups[TargetGroup].Panels.size()));

		Groups[TargetGroup].Panels.insert(Groups[TargetGroup].Panels.begin() + Index, Panel);
		Groups[TargetGroup].Active = Panel;
		Panels[Panel].Group = TargetGroup;

		if (SourceEmpty)
			CloseGroup(SourceGroup);

		BeginAnimationAll();
		return;
	}

	RemovePanelFromGroup(Panel, SourceGroup);
	const bool SourceEmpty = Groups[SourceGroup].Panels.empty();

	if (SourceEmpty)
		CloseGroup(SourceGroup);

	const int NewGroup = AllocGroup();
	Groups[NewGroup].InUse = true;
	Groups[NewGroup].Floating = false;
	Groups[NewGroup].Panels.push_back(Panel);
	Groups[NewGroup].Active = Panel;
	Panels[Panel].Group = NewGroup;

	const rect Start = CollapsedBounds(Zone, Groups[TargetGroup].Bounds);
	Groups[NewGroup].Bounds = Start;
	Groups[NewGroup].TargetBounds = Start;

	const int Leaf = LeafOfGroup(TargetGroup);
	if (Leaf >= 0)
		SplitLeaf(Leaf, Zone, NewGroup);

	BeginAnimationAll();
}

void WorkspaceModel::DockGroup(int Group, int TargetGroup, DockZone Zone)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	if (TargetGroup < 0 || TargetGroup >= static_cast<int>(Groups.size()) || !Groups[TargetGroup].InUse)
		return;

	if (Group == TargetGroup)
		return;

	if (Groups[Group].Panels.empty())
	{
		CloseGroup(Group);
		return;
	}

	if (Groups[TargetGroup].Floating && Zone != DockZone::Center)
		Zone = DockZone::Center;

	if (Groups[Group].Floating)
	{
		for (size_t Index = 0; Index < FloatingOrder.size(); Index++)
		{
			if (FloatingOrder[Index] == Group)
			{
				FloatingOrder.erase(FloatingOrder.begin() + Index);
				break;
			}
		}
	}
	else
	{
		RemoveGroupFromTree(Group);
	}

	Groups[Group].Floating = false;

	if (Zone == DockZone::Center)
	{
		const int Active = Groups[Group].Active >= 0 ? Groups[Group].Active : Groups[Group].Panels.back();

		for (int Panel : Groups[Group].Panels)
		{
			Panels[Panel].Group = TargetGroup;
			Groups[TargetGroup].Panels.push_back(Panel);
		}

		Groups[TargetGroup].Active = Active;
		Groups[Group] = GroupItem{};

		BeginAnimationAll();
		return;
	}

	const int Leaf = LeafOfGroup(TargetGroup);
	if (Leaf >= 0)
		SplitLeaf(Leaf, Zone, Group);

	BeginAnimationAll();
}

void WorkspaceModel::DetachGroup(int Group)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	if (Groups[Group].Floating)
	{
		MoveGroupToFront(Group);
		return;
	}

	RemoveGroupFromTree(Group);
	Groups[Group].Floating = true;
	Groups[Group].TargetBounds = Groups[Group].Bounds;
	Groups[Group].AnimRemaining = 0.f;
	FloatingOrder.push_back(Group);
}

void WorkspaceModel::MoveGroupToFront(int Group)
{
	for (size_t Index = 0; Index < FloatingOrder.size(); Index++)
	{
		if (FloatingOrder[Index] == Group)
		{
			FloatingOrder.erase(FloatingOrder.begin() + Index);
			break;
		}
	}

	FloatingOrder.push_back(Group);
}

void WorkspaceModel::ClampFloatingGroups(rect Area)
{
	const float MinWidth = 280.f * LayoutScale;
	const float MinHeight = 180.f * LayoutScale;

	for (int Group : FloatingOrder)
	{
		if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
			continue;

		if (Groups[Group].AnimRemaining > 0.f)
			continue;

		rect Bounds = Groups[Group].Bounds;
		vec2 Size = Bounds.size();
		Size.x = std::max(Size.x, MinWidth);
		Size.y = std::max(Size.y, MinHeight);
		Size.x = std::min(Size.x, Area.width());
		Size.y = std::min(Size.y, Area.height());

		const float X = std::clamp(Bounds.left, Area.left, Area.right - Size.x);
		const float Y = std::clamp(Bounds.top, Area.top, Area.bottom - Size.y);

		Groups[Group].Bounds = rect(X, Y, X + Size.x, Y + Size.y);
	}
}

void WorkspaceModel::Layout(rect Area, float Scale)
{
	LayoutScale = Scale;

	ClampFloatingGroups(Area);

	if (Root >= 0)
		ComputeNode(Root, Area);

	for (GroupItem& Item : Groups)
	{
		if (Item.InUse && Item.Floating && Item.AnimRemaining <= 0.f)
			Item.TargetBounds = Item.Bounds;
	}
}

void WorkspaceModel::BeginAnimation(int Group, float Duration)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	Groups[Group].AnimRemaining = std::max(Groups[Group].AnimRemaining, Duration);
}

void WorkspaceModel::BeginAnimationAll(float Duration)
{
	for (size_t Index = 0; Index < Groups.size(); Index++)
	{
		if (Groups[Index].InUse)
			Groups[Index].AnimRemaining = std::max(Groups[Index].AnimRemaining, Duration);
	}
}

void WorkspaceModel::AnimateGroupTo(int Group, rect Bounds)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	Groups[Group].TargetBounds = Bounds;
	Groups[Group].AnimRemaining = 0.22f;
	BeginAnimation(Group, 0.22f);
}

void WorkspaceModel::RestoreRoot(int Group, rect Area)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	if (Groups[Group].Panels.empty())
	{
		CloseGroup(Group);
		return;
	}

	if (Root >= 0)
	{
		const int Target = DefaultGroup();
		if (Target >= 0 && Target != Group)
			DockGroup(Group, Target, DockZone::Center);

		return;
	}

	for (size_t Index = 0; Index < FloatingOrder.size(); Index++)
	{
		if (FloatingOrder[Index] == Group)
		{
			FloatingOrder.erase(FloatingOrder.begin() + Index);
			break;
		}
	}

	Groups[Group].Floating = false;
	Groups[Group].TargetBounds = Area;

	const int Node = AllocNode();
	Nodes[Node].Leaf = true;
	Nodes[Node].Group = Group;
	Nodes[Node].Parent = -1;
	Root = Node;

	BeginAnimationAll(0.24f);
}

void WorkspaceModel::WrapRootWithGroup(int Group, DockZone Zone, rect Area)
{
	if (Root < 0)
	{
		const int Node = AllocNode();
		Nodes[Node].Leaf = true;
		Nodes[Node].Group = Group;
		Nodes[Node].Parent = -1;
		Root = Node;
		Groups[Group].TargetBounds = Area;
		return;
	}

	const int OldRoot = Root;
	const int NewRoot = AllocNode();
	const int NewLeaf = AllocNode();

	Nodes[NewLeaf].Leaf = true;
	Nodes[NewLeaf].Group = Group;
	Nodes[NewLeaf].Parent = NewRoot;

	Nodes[NewRoot].Leaf = false;
	Nodes[NewRoot].Horizontal = Zone == DockZone::Left || Zone == DockZone::Right;
	Nodes[NewRoot].Ratio = 0.5f;
	Nodes[NewRoot].Parent = -1;
	Nodes[NewRoot].Group = -1;

	if (Zone == DockZone::Left || Zone == DockZone::Top)
	{
		Nodes[NewRoot].First = NewLeaf;
		Nodes[NewRoot].Second = OldRoot;
	}
	else
	{
		Nodes[NewRoot].First = OldRoot;
		Nodes[NewRoot].Second = NewLeaf;
	}

	Nodes[OldRoot].Parent = NewRoot;
	Root = NewRoot;
}

void WorkspaceModel::DockGroupToEdge(int Group, DockZone Zone, rect Area)
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return;

	if (!IsHalfZone(Zone))
		return;

	if (Groups[Group].Panels.empty())
	{
		CloseGroup(Group);
		return;
	}

	if (Groups[Group].Floating)
	{
		for (size_t Index = 0; Index < FloatingOrder.size(); Index++)
		{
			if (FloatingOrder[Index] == Group)
			{
				FloatingOrder.erase(FloatingOrder.begin() + Index);
				break;
			}
		}
	}
	else
	{
		RemoveGroupFromTree(Group);
	}

	Groups[Group].Floating = false;
	WrapRootWithGroup(Group, Zone, Area);
	BeginAnimationAll(0.24f);
}

void WorkspaceModel::DockPanelToEdge(int Panel, DockZone Zone, rect Area)
{
	if (Panel < 0 || Panel >= static_cast<int>(Panels.size()) || !Panels[Panel].Open)
		return;

	if (!IsHalfZone(Zone))
		return;

	const int SourceGroup = Panels[Panel].Group;
	if (SourceGroup < 0 || SourceGroup >= static_cast<int>(Groups.size()) || !Groups[SourceGroup].InUse)
		return;

	const int NewGroup = AllocGroup();
	Groups[NewGroup].InUse = true;
	Groups[NewGroup].Floating = false;
	Groups[NewGroup].Panels.push_back(Panel);
	Groups[NewGroup].Active = Panel;
	Panels[Panel].Group = NewGroup;

	RemovePanelFromGroup(Panel, SourceGroup);

	if (Groups[SourceGroup].Panels.empty())
		CloseGroup(SourceGroup);

	const rect Start = CollapsedBounds(Zone, Area);
	Groups[NewGroup].Bounds = Start;
	Groups[NewGroup].TargetBounds = Start;

	WrapRootWithGroup(NewGroup, Zone, Area);
	BeginAnimationAll(0.24f);
}

void WorkspaceModel::StepAnimations(float Delta)
{
	const float Step = std::clamp(Delta, 0.f, 0.05f);
	const float Factor = 1.f - std::exp(-Step / 0.045f);

	for (GroupItem& Item : Groups)
	{
		if (!Item.InUse)
			continue;

		if (Item.AnimRemaining > 0.f)
		{
			Item.Bounds = rect(
				Item.Bounds.left + (Item.TargetBounds.left - Item.Bounds.left) * Factor,
				Item.Bounds.top + (Item.TargetBounds.top - Item.Bounds.top) * Factor,
				Item.Bounds.right + (Item.TargetBounds.right - Item.Bounds.right) * Factor,
				Item.Bounds.bottom + (Item.TargetBounds.bottom - Item.Bounds.bottom) * Factor);

			Item.AnimRemaining -= Step;

			if (Item.AnimRemaining <= 0.f)
			{
				Item.AnimRemaining = 0.f;
				Item.Bounds = Item.TargetBounds;
			}
		}
		else if (!Item.Floating)
		{
			Item.Bounds = Item.TargetBounds;
		}
	}

	for (size_t Index = 0; Index < Groups.size(); Index++)
	{
		if (Groups[Index].InUse)
			BuildTabs(static_cast<int>(Index));
	}
}

void WorkspaceModel::ComputeNode(int Node, rect Area)
{
	if (Node < 0 || Node >= static_cast<int>(Nodes.size()) || !Nodes[Node].InUse)
		return;

	Nodes[Node].Bounds = Area;

	if (Nodes[Node].Leaf)
	{
		const int Group = Nodes[Node].Group;
		if (Group >= 0 && Group < static_cast<int>(Groups.size()) && Groups[Group].InUse)
			Groups[Group].TargetBounds = Area;

		return;
	}

	const float Gap = 6.f * LayoutScale;

	if (Nodes[Node].Horizontal)
	{
		const float Split = Area.left + Area.width() * Nodes[Node].Ratio;
		Nodes[Node].Divider = rect(Split - Gap * 0.5f, Area.top, Split + Gap * 0.5f, Area.bottom);
		ComputeNode(Nodes[Node].First, rect(Area.left, Area.top, Split - Gap * 0.5f, Area.bottom));
		ComputeNode(Nodes[Node].Second, rect(Split + Gap * 0.5f, Area.top, Area.right, Area.bottom));
	}
	else
	{
		const float Split = Area.top + Area.height() * Nodes[Node].Ratio;
		Nodes[Node].Divider = rect(Area.left, Split - Gap * 0.5f, Area.right, Split + Gap * 0.5f);
		ComputeNode(Nodes[Node].First, rect(Area.left, Area.top, Area.right, Split - Gap * 0.5f));
		ComputeNode(Nodes[Node].Second, rect(Area.left, Split + Gap * 0.5f, Area.right, Area.bottom));
	}
}

void WorkspaceModel::BuildTabs(int GroupIndex)
{
	GroupItem& Group = Groups[GroupIndex];
	Group.Tabs.clear();

	const float Pad = 6.f * LayoutScale;
	const float HeaderHeight = UiHeaderHeight * LayoutScale;
	const float PlusWidth = 30.f * LayoutScale;
	const float GripWidth = Group.Floating ? 18.f * LayoutScale : 0.f;
	const float TabPadding = 22.f * LayoutScale;
	const float MinTab = 84.f * LayoutScale;
	const float MaxTab = 210.f * LayoutScale;
	const float CloseWidth = 18.f * LayoutScale;

	const rect Bounds = Group.Bounds;
	if (Bounds.width() <= 1.f || Bounds.height() <= 1.f)
	{
		Group.Header = rect();
		Group.Grip = rect();
		Group.NewTab = rect();
		return;
	}

	const float Header = std::min(HeaderHeight, Bounds.height());
	Group.Header = rect(Bounds.left, Bounds.top, Bounds.right, Bounds.top + Header);
	Group.Grip = rect(Bounds.left + Pad, Group.Header.top, Bounds.left + Pad + GripWidth, Group.Header.bottom);
	Group.NewTab = rect(Bounds.right - Pad - PlusWidth, Group.Header.top + 5.f * LayoutScale, Bounds.right - Pad, Group.Header.bottom - 5.f * LayoutScale);

	const float ContentLeft = Bounds.left + Pad + GripWidth;
	const float ContentRight = Bounds.right - Pad - PlusWidth - 4.f * LayoutScale;
	const float Available = std::max(0.f, ContentRight - ContentLeft);

	const size_t Count = Group.Panels.size();
	std::vector<float> Widths(Count, 0.f);
	float Total = 0.f;

	for (size_t Index = 0; Index < Count; Index++)
	{
		const std::wstring& Title = Panels[Group.Panels[Index]].Title;
		const float TextWidth = renderer::measure_text(Title.c_str(), UiTabTextSize * LayoutScale).width();
		const float Width = std::clamp(TextWidth + TabPadding * 2.f + CloseWidth, MinTab, MaxTab);
		Widths[Index] = Width;
		Total += Width;
	}

	if (Total > Available && Total > 0.f)
	{
		const float Shrink = Available / Total;
		float Shrunk = 0.f;

		for (size_t Index = 0; Index < Count; Index++)
		{
			Widths[Index] = std::max(Widths[Index] * Shrink, 46.f * LayoutScale);
			Shrunk += Widths[Index];
		}

		// The 46px floor can still overflow with many tabs; fall back to equal widths so every
		// panel keeps a reachable tab instead of silently dropping the trailing ones.
		if (Shrunk > Available)
			std::fill(Widths.begin(), Widths.end(), Available / static_cast<float>(Count));
	}

	float X = ContentLeft;

	for (size_t Index = 0; Index < Count; Index++)
	{
		if (X >= ContentRight)
			break;

		float Width = Widths[Index];
		if (X + Width > ContentRight)
			Width = ContentRight - X;

		if (Width < 1.f)
			break;

		TabRect Tab;
		Tab.Panel = Group.Panels[Index];
		Tab.Bounds = rect(X, Group.Header.top + 4.f * LayoutScale, X + Width, Group.Header.bottom);

		const float CenterY = (Tab.Bounds.top + Tab.Bounds.bottom) * 0.5f;
		const float CloseSize = 18.f * LayoutScale;

		// A close button on a tab narrower than this would cover the whole tab and turn every click into a close.
		if (Width >= 44.f * LayoutScale)
		{
			Tab.Close = rect(Tab.Bounds.right - CloseSize - 2.f * LayoutScale, CenterY - CloseSize * 0.5f,
				Tab.Bounds.right - 2.f * LayoutScale, CenterY + CloseSize * 0.5f);
		}

		Group.Tabs.push_back(Tab);
		X += Width;
	}
}

int WorkspaceModel::LeafOfGroup(int Group) const
{
	for (size_t Index = 0; Index < Nodes.size(); Index++)
	{
		if (Nodes[Index].InUse && Nodes[Index].Leaf && Nodes[Index].Group == Group)
			return static_cast<int>(Index);
	}

	return -1;
}

void WorkspaceModel::SplitLeaf(int Leaf, DockZone Zone, int Group)
{
	if (Leaf < 0 || Leaf >= static_cast<int>(Nodes.size()) || !Nodes[Leaf].InUse || !Nodes[Leaf].Leaf)
		return;

	const int OldGroup = Nodes[Leaf].Group;
	const int OldParent = Nodes[Leaf].Parent;

	const int OldLeaf = AllocNode();
	Nodes[OldLeaf].Leaf = true;
	Nodes[OldLeaf].Group = OldGroup;
	Nodes[OldLeaf].Parent = Leaf;

	const int NewLeaf = AllocNode();
	Nodes[NewLeaf].Leaf = true;
	Nodes[NewLeaf].Group = Group;
	Nodes[NewLeaf].Parent = Leaf;

	Nodes[Leaf].Leaf = false;
	Nodes[Leaf].Horizontal = Zone == DockZone::Left || Zone == DockZone::Right;
	Nodes[Leaf].Ratio = 0.5f;
	Nodes[Leaf].Group = -1;
	Nodes[Leaf].Parent = OldParent;

	if (Zone == DockZone::Left || Zone == DockZone::Top)
	{
		Nodes[Leaf].First = NewLeaf;
		Nodes[Leaf].Second = OldLeaf;
	}
	else
	{
		Nodes[Leaf].First = OldLeaf;
		Nodes[Leaf].Second = NewLeaf;
	}
}

void WorkspaceModel::CollapseNode(int Node)
{
	if (Node < 0 || Node >= static_cast<int>(Nodes.size()) || !Nodes[Node].InUse)
		return;

	const int Parent = Nodes[Node].Parent;

	if (Parent < 0)
	{
		if (Root == Node)
			Root = -1;

		FreeNode(Node);
		return;
	}

	const int Sibling = Nodes[Parent].First == Node ? Nodes[Parent].Second : Nodes[Parent].First;
	if (Sibling < 0)
		return;

	const int Grand = Nodes[Parent].Parent;

	Nodes[Parent] = Nodes[Sibling];
	Nodes[Parent].Parent = Grand;

	if (!Nodes[Parent].Leaf)
	{
		Nodes[Nodes[Parent].First].Parent = Parent;
		Nodes[Nodes[Parent].Second].Parent = Parent;
	}

	FreeNode(Sibling);
	FreeNode(Node);
}

void WorkspaceModel::RemoveGroupFromTree(int Group)
{
	const int Leaf = LeafOfGroup(Group);
	if (Leaf >= 0)
		CollapseNode(Leaf);
}

int WorkspaceModel::GroupAt(vec2 Point) const
{
	return GroupAt(Point, -1);
}

int WorkspaceModel::GroupAt(vec2 Point, int IgnoreGroup) const
{
	for (size_t Index = FloatingOrder.size(); Index > 0; Index--)
	{
		const int Group = FloatingOrder[Index - 1];
		if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
			continue;

		if (Group == IgnoreGroup)
			continue;

		if (PointInRect(Groups[Group].Bounds, Point))
			return Group;
	}

	for (size_t Index = 0; Index < Nodes.size(); Index++)
	{
		if (!Nodes[Index].InUse || !Nodes[Index].Leaf)
			continue;

		const int Group = Nodes[Index].Group;
		if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
			continue;

		if (Group == IgnoreGroup)
			continue;

		if (PointInRect(Groups[Group].Bounds, Point))
			return Group;
	}

	return -1;
}

int WorkspaceModel::DividerAt(vec2 Point) const
{
	for (size_t Index = 0; Index < Nodes.size(); Index++)
	{
		if (!Nodes[Index].InUse || Nodes[Index].Leaf)
			continue;

		if (PointInRect(Nodes[Index].Divider, Point))
			return static_cast<int>(Index);
	}

	return -1;
}

int WorkspaceModel::InsertIndexAt(int Group, float X) const
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()))
		return 0;

	const GroupItem& Item = Groups[Group];
	for (size_t Index = 0; Index < Item.Tabs.size(); Index++)
	{
		if (X < Item.Tabs[Index].Bounds.center().x)
			return static_cast<int>(Index);
	}

	return static_cast<int>(Item.Tabs.size());
}

DockZone WorkspaceModel::ZoneAt(int Group, vec2 Point) const
{
	if (Group < 0 || Group >= static_cast<int>(Groups.size()) || !Groups[Group].InUse)
		return DockZone::None;

	const GroupItem& Item = Groups[Group];
	const rect Bounds = Item.Bounds;

	if (!PointInRect(Bounds, Point))
		return DockZone::None;

	if (Point.y < Item.Header.bottom)
		return DockZone::Center;

	const float MarginX = std::clamp(Bounds.width() * 0.26f, 20.f, 120.f);
	const float MarginY = std::clamp(Bounds.height() * 0.26f, 20.f, 120.f);

	if (Point.x < Bounds.left + MarginX)
		return DockZone::Left;

	if (Point.x > Bounds.right - MarginX)
		return DockZone::Right;

	if (Point.y < Bounds.top + MarginY)
		return DockZone::Top;

	if (Point.y > Bounds.bottom - MarginY)
		return DockZone::Bottom;

	return DockZone::Center;
}

int WorkspaceModel::DefaultGroup() const
{
	for (size_t Index = 0; Index < Nodes.size(); Index++)
	{
		if (!Nodes[Index].InUse || !Nodes[Index].Leaf)
			continue;

		const int Group = Nodes[Index].Group;
		if (Group >= 0 && Group < static_cast<int>(Groups.size()) && Groups[Group].InUse)
			return Group;
	}

	for (int Group : FloatingOrder)
	{
		if (Group >= 0 && Group < static_cast<int>(Groups.size()) && Groups[Group].InUse)
			return Group;
	}

	return -1;
}
