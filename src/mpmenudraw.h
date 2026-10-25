// Paints the multiplayer pages (mpmenu.h) in the main menu's style.
#pragma once
#include "mpmenu.h"
#include "render.h"

namespace mpmenu {

// `a`: fade 0..1. `pad`: the controller was used last. `overGame`: over the running game (a darker
// backdrop) rather than the main menu's camp. Fills nav.hits for the mouse.
void draw(Renderer& r, Nav& nav, const View& v, int W, int H, float a, double clock, bool pad, bool overGame);

}  // namespace mpmenu
