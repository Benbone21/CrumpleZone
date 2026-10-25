// The update card (updater.h): a box in the middle of the screen over whatever is showing, with UPDATE NOW / LATER, a download
// progress bar and CANCEL, or a message with OK (and COPY LINK). While it is up it takes the keyboard, the mouse and the pad.
#pragma once
#include "render.h"

namespace updatecard {

bool shown();  // the card is up (update::status().phase is not Idle)
void draw(Renderer& r, int W, int H, bool padLast);
// Events while shown(): true when the card used it (main.cpp then skips the event). Scancodes as SDL's.
bool key(int scancode);
bool mouseDown(float x, float y);  // drawable pixels
void mouseMove(float x, float y);

}  // namespace updatecard
