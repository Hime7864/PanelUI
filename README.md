# PanelUI

Dockable panel shell + immediate-mode widget framework for Win32/D3D11, shipped as a **single static
library** that already contains PowerlayGraphics.

```
include/panelui.h   umbrella header (only include a consumer needs)
include/*.h         theme, framework (widgets), workspace (dock model), ui (shell)
src/*.cpp           implementation, compiled into PanelUI.lib
third_party/PowerlayGraphics/lib/<Config>/PowerlayGraphics.lib   vendored renderer archive
```

`PanelUI.lib` is built by merging the project's own objects with the vendored PowerlayGraphics archive,
so a consumer links exactly one file.

---

## Requirements

- Windows 10+, MSVC / ClangCL, C++20, x64.
- `PowerlayGraphics.lib` present in `third_party/PowerlayGraphics/lib/<Configuration>/`.
- Fonts are **optional**: the renderer's embedded fallback is Segoe UI. Ship `segoeui.ttf` /
  `seguisb.ttf` next to the consumer executable only if you want the named regular/semibold slots;
  without them `UiShell::EnsureStartup` falls back to the embedded face.

## Build

```bat
msbuild PanelUI.sln -p:Configuration=Release -p:Platform=x64
msbuild PanelUI.sln -p:Configuration=Debug   -p:Platform=x64
```

Outputs: `x64/<Configuration>/PanelUI.lib` (merged) and `x64/<Configuration>/PanelUI.core.lib`
(own objects only, intermediate). Only `x64` exists because no x86 graphics archive is vendored.

## Consume

```xml
<AdditionalIncludeDirectories>$(PanelUIRoot)\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
<AdditionalLibraryDirectories>$(PanelUIRoot)\x64\$(Configuration);%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
<AdditionalDependencies>PanelUI.lib;%(AdditionalDependencies)</AdditionalDependencies>
```

Do **not** also link `PowerlayGraphics.lib`; it is already inside `PanelUI.lib`.

```cpp
#include "panelui.h"

static UiShell Ui;
static HWND MainWindow = nullptr;

static bool RenderFrame()
{
    if (!IsWindow(MainWindow))
        return false;

    Ui.Frame();
    DwmFlush();
    return true;
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW WindowClass{};
    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.lpfnWndProc = WindowProc;              // forwarding below
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = L"MyApp";
    RegisterClassExW(&WindowClass);

    HWND Window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"MyApp", L"App",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        100, 100, 1280, 800, nullptr, nullptr, Instance, nullptr);
    MainWindow = Window;

    std::vector<UiPanelInit> Layout(1);
    Layout[0].Title = L"Main";
    Layout[0].Zone = DockZone::Center;
    Layout[0].Build = [](PanelContent& Content)
    {
        const int Root = Content.Init(UiFlow::Vertical, 12.f, 8.f);
        Content.Node(Root).Style.Scroll = true;
        Content.Text(Root, L"Hello", 18.f);
        Content.Button(Root, L"Click me", [] { /* work */ });
    };

    Ui.SetTitle(L"My App");            // optional, defaults to L"Workspace"
    Ui.SetStartupLayout(Layout);       // before Initialize
    Ui.Initialize(Window);
    ShowWindow(Window, SW_SHOW);
    return app::run(Window, RenderFrame);
}
```

### Host window contract

`UiShell` is not a subclassable window; it expects these messages forwarded from the consumer's
`WindowProc`:

| Message | Call |
|---|---|
| `WM_NCHITTEST` | `return Ui.HitTest(vec2(clientX, clientY));` |
| `WM_SETCURSOR` (`HTCLIENT`) | `SetCursor(LoadCursorW(nullptr, Ui.CursorName())); return TRUE;` |
| `WM_MOUSEMOVE` | `Ui.OnMouseMove(point)` (+ `TrackMouseEvent(TME_LEAVE)` once) |
| `WM_MOUSELEAVE` | `Ui.OnMouseLeave()` |
| `WM_LBUTTONDOWN` / `WM_LBUTTONUP` | `SetCapture(hwnd)` + `Ui.OnMouseDown(point)` / `Ui.OnMouseUp(point)` + `ReleaseCapture()` |
| `WM_RBUTTONDOWN` | `Ui.OnRightMouseDown(point)` |
| `WM_MOUSEWHEEL` | `Ui.OnMouseWheel(delta / 120.f, clientPoint)` |
| `WM_CHAR` / `WM_KEYDOWN` | `Ui.OnChar(wchar)` / `Ui.OnKeyDown(vk)` |
| `WM_CAPTURECHANGED` | `if (hwnd != Window) Ui.OnCaptureLost();` |
| `WM_NCCALCSIZE`, `WM_DPICHANGED`, `WM_GETMINMAXINFO` | DPI-correct frame handling (see any prior sample) |
| `WM_ERASEBKGND` | `return 1;` |

Render loop: call `Ui.Frame()` per frame (`app::run` from PowerlayGraphics does the pump, or drive it
from a timer during `WM_ENTERSIZEMOVE`).

---

## Architecture

```
UiShell (ui.h/ui.cpp)            window chrome, dock model owner, tabs, drag/dock, settings, picker
 └─ WorkspaceModel (workspace.h) pure layout state: groups, layout tree, floating order, animations
     └─ GroupItem               tabbed container; Tabs are rebuilt every frame in BuildTabs()
 └─ PanelContent (framework.h)   one content tree per panel (widgets)
     └─ UiNode                 style + rects + state + callbacks
```

- **Panels** are ids into `WorkspaceModel::Panels`; **groups** own tabs; **layout nodes** are the
  binary split tree (`Horizontal`, `Ratio`, `Divider`).
- `PanelContents[panel]` holds one `PanelContent`; it is rebuilt from `UiPanelInit::Build` on creation
  (`EnsurePanelContent`) so **node ids are not stable across rebuilds** — capture nodes by index in
  lambdas only for the lifetime of that content.
- **An absent `Build` callback produces an empty panel.** The library never inserts placeholder
  widgets. Register `UiShell::SetDefaultPanelBuild(...)` to give shell-created tabs (`+`, New Panel,
  tab-menu *New panel*) their content; without it those tabs stay empty.
- Layout every frame: `Workspace.Layout(WorkspaceRect, Scale)` → `StepAnimations` →
  `LayoutContents` (widget layout) → `BuildGroupClips` → `UpdateHover` → `BuildOccluders` → draw.

## Invariants an agent must keep

1. **Rects are `left, top, right, bottom`** (`rect` in graphics.hpp); `size()`, `center()`, `width()`
   are helpers. `PointInRect` is half-open.
2. **Every style length is in 96-DPI units multiplied by `Scale`.** Never bake pixels.
3. **Callbacks may reallocate the node vector** (`Add`, `Init`, `SetVisible`…). Always copy the
   `std::function` before invoking it — use the `Invoke()` helper in framework.cpp; never call
   `Item.OnClick()` through a reference into `Nodes`.
4. **Text is rendered in a separate pass after all geometry.** The renderer's scissor does not survive
   nested clipping for glyphs, so overlap is solved in the framework:
   - `PanelContent::SetOccluders(cuts)` — opaque surfaces (popup, menu, tooltip, settings card,
     drag ghost, every group stacked above) that text must not paint through.
   - `DrawTextLine()` emits per-character runs when a string intersects a cut. Route **all** text
     through it; do not call `renderer::draw_text` directly in a widget branch.
   - `DrawTextNode(..., rect Visible)` culls nodes wholly outside the group viewport.
   If you add an overlay surface, add it to `UiShell::BuildOccluders()` **and** keep its shape opaque
   (`Opaque(Theme.X)`), otherwise content text shows through it.
5. **Layout order is fixed**: flow children first, then `Absolute` children (`DeferredAbsolute`), so
   `AnchorTo` resolves against this frame's bounds. Hidden nodes get zeroed bounds via `HideSubtree`.
6. **Measurement rules**: a fixed `Width`/`Height` always wins; `Fill`/`Percent` return 0 in
   `Measure*` (the parent supplies the space); cross-axis margins are subtracted before measuring.
   Any new widget must implement both `MeasureHeight` and `MeasureWidth`.
7. **Hit testing and hover are gated**: only the topmost group under the cursor gets hover
   (`UiShell::HoverGroup`); `PanelContent::HitTestNode` skips `!Visible`, `!Enabled` and
   clip-violating subtrees. Interactive kinds list lives in `IsInteractive()` / `IsPressable()`.
8. **Scrolling is explicit**: `LayoutFlow` computes `ScrollMax`; `FindScrollNode` never returns a node
   with `ScrollMax == 0`; list rows are culled by row rect in `DrawTextNode`.
9. **Ids**: `UiNode::Id` is only for `AnchorTo` lookups (`PanelContent::Find`). `RadioGroup` ids are
   negative for generated groups.

## Adding a widget (checklist)

1. `UiNodeKind` — add the enum value.
2. `PanelContent` — add a constructor method that fills `UiStyle` and the default state.
3. `MeasureHeight` / `MeasureWidth` — size rules (fixed wins, flexible returns 0).
4. `LayoutNode` — only if the node owns children or computes `ScrollMax`.
5. `HitTestNode` — add to `IsInteractive()`; `OnMouseDown`/`OnMouseUp`/`Activate` for input.
6. `DrawShapesNode` — background/indicator geometry (respect `Off` tinting and `Hovered`).
7. `DrawTextNode` — label/value via `DrawTextLine`.
8. `DrawDebugNode` — kind name for the Rects overlay.
9. No new state in `UiShell` unless it must outlive a panel rebuild.

## Debugging

`Rects` button / `F1` toggles `UiShell::DrawDebugOverlay`:
- panel-system layer: workspace rect, split nodes, dividers, group bounds, tabs, grips, float edges;
- widget layer: every `PanelContent` node with `index kind` chip (amber = control, green = layout/text,
  blue = inner padding box, violet = absolute, pink = scroll track/thumb, `off` = disabled).
- status bar shows mouse, hit group/zone/divider, drag state, and the colour legend.

`UiShell::DebugRects`, `UiShell::SettingsOpen`, `OpenDropPanel/OpenDropNode`, `Drag` are the state to
inspect when reproducing layout/overlap bugs.

## Known limitations

- Overlay text clipping is per character run (kerning across a cut is approximate).
- No horizontal scrolling, no multi-line text input, no wrapping inside buttons.
- Single content tree per panel; tabs keep separate contents, switching does not re-run `Build`.
- `Win32` configurations exist in the project files but require an x86 `PowerlayGraphics.lib`.
