// The terrain texture's window (render.cpp streamTerrain): w x w cells of the map round the camera, stored toroidally (cell
// (i, j) at texel (i mod w, j mod w)). It follows the camera a step at a time: each step brings in one strip along its leading
// edge, which overwrites the strip it leaves behind, so every texel inside the window is right before and after the step.
// The strip's texels are made on a background thread; the frame only uploads it. Pure, so the tests can walk it.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace streamwin {

// The window's first cell for a camera at grid cell c, on a map n cells a side.
inline int start(int c, int n, int w) { return std::min(std::max(c - w / 2, 0), std::max(0, n - w)); }

// A rectangle of cells that doesn't wrap in the texture.
struct Piece {
    int a0, b0, cw, ch;  // first cell (i along x, j along z) and size
};

struct Step {
    int toI = 0, toJ = 0;       // the window's first cell after the step
    std::vector<Piece> pieces;  // the cells it brings in (empty: no step)
};

// Splits cells [i0, i1] x [j0, j1] where they wrap in a texture w texels a side.
inline void split(int i0, int i1, int j0, int j1, int w, std::vector<Piece>& out) {
    for (int a0 = i0; a0 <= i1;) {
        const int a1 = std::min(i1, a0 + (w - 1 - a0 % w));
        for (int b0 = j0; b0 <= j1;) {
            const int b1 = std::min(j1, b0 + (w - 1 - b0 % w));
            out.push_back({a0, b0, a1 - a0 + 1, b1 - b0 + 1});
            b0 = b1 + 1;
        }
        a0 = a1 + 1;
    }
}

// The next step from the window at (curI, curJ) toward (wantI, wantJ) (where it would sit centred on the camera). `moving`
// carries over between calls: a move starts once the camera is more than `slack` cells off and goes on, `step` cells at a
// time along whichever axis is further off, until within step / 2. Where the wanted window is held at the map's edge (n
// cells a side) it goes all the way there: else the camera could drive between the map's edge and a window left short of it.
inline Step next(int curI, int curJ, int wantI, int wantJ, int w, int n, int slack, int step, bool& moving) {
    Step s;
    s.toI = curI, s.toJ = curJ;
    const int di = wantI - curI, dj = wantJ - curJ;
    const bool edge = (di != 0 && (wantI == 0 || wantI == n - w)) || (dj != 0 && (wantJ == 0 || wantJ == n - w));
    if (std::abs(di) > slack || std::abs(dj) > slack || edge) moving = true;
    if (moving && std::abs(di) < step / 2 && std::abs(dj) < step / 2 && !edge) moving = false;
    if (di == 0 && dj == 0) moving = false;
    if (!moving) return s;
    int i0, i1, j0, j1;
    if (std::abs(di) >= std::abs(dj)) {
        const int k = std::clamp(di, -step, step);
        s.toI += k;
        i0 = k > 0 ? curI + w : s.toI, i1 = k > 0 ? s.toI + w - 1 : curI - 1, j0 = curJ, j1 = curJ + w - 1;
    } else {
        const int k = std::clamp(dj, -step, step);
        s.toJ += k;
        j0 = k > 0 ? curJ + w : s.toJ, j1 = k > 0 ? s.toJ + w - 1 : curJ - 1, i0 = curI, i1 = curI + w - 1;
    }
    split(i0, i1, j0, j1, w, s.pieces);
    return s;
}

}  // namespace streamwin
