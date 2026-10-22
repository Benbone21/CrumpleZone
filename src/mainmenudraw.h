// Paints the new main menu (mainmenu.h) over the camp scene (menuscene.h): the front page, the map and vehicle pages and the DEV page.
#pragma once
#include "mainmenu.h"
#include "render.h"

namespace mainmenu {

// `a`: fade-in 0..1. `padLast`: the controller was used last (the hints name its buttons). Fills nav.hits for the mouse.
void draw(Renderer& r, Nav& nav, int W, int H, float a, double clock, bool padLast);

// A 112x63 RGB thumbnail of a map (mappreview.cpp), or null when there is none.
constexpr int kPreviewW = 112, kPreviewH = 63;
const unsigned char* mapPreview(int map);

}  // namespace mainmenu
