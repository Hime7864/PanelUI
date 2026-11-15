#pragma once

// PanelUI: dockable panel shell + widget framework on top of PowerlayGraphics.
//
// A consumer only needs:
//   #include "panelui.h"          // this file
//   PanelUI.lib                    // this library; it already contains PowerlayGraphics
//
// UiShell owns the window chrome, dock layout, tab strips, drag/dock, settings and color picker.
// PanelContent builds the widgets inside a panel.

#include "graphics.hpp"
#include "theme.h"
#include "framework.h"
#include "workspace.h"
#include "ui.h"
