// Paints the garage shop (garageshop.h) over the live world, in the main menu's look (mainmenudraw.cpp): the
// categories and the parts down the left, the focused part's card on the right with its numbers against
// what's fitted, the hint line at the bottom. Fills s.hits and s.turntableX for the mouse.
#pragma once
#include <string>

#include "garageshop.h"
#include "render.h"

namespace shop {

// `where`: the station you're at ("" none). `a`: fade-in 0..1. `pending`: the truck isn't wearing what's shown yet.
// `toast`: what the last press said (`toastA` its fade).
void draw(Renderer& r, Shop& s, const Career& c, const std::string& where, int W, int H, float a, bool padLast, bool pending,
          const std::string& toast = "", float toastA = 0);

}  // namespace shop
