// The inventory menu's picture (invmenu.h has what's in it and where): a small dark panel over the scene, a square grid
// with a slot per kind (its model as the icon), the chosen one's description, and the keys.
#pragma once
#include "invmenu.h"
#include "render.h"

// `lay`: menu.layout(W, H, u, pad). `pad`: the pad was the last thing used (the hints say its buttons).
void drawInvMenu(Renderer& r, const InvMenu& menu, const InvLayout& lay, int W, int H, bool pad);
