// A vehicle's condition as one line of text (career saves: profile.txt's cond lines, a world's
// left vehicles): which vehicle and mods, where it stands, its fuel, battery, engine, wheels, hood and
// gate, and its damage. The dents are the node shape the multiplayer states carry
// (net::carDents), and with them the bent and broken beams, the body's bent rest shape, the
// pins let go and the glass broken, so a wreck comes back the wreck it was.
//
// "car1 key=value key=value ... end": unknown keys are skipped (later fields add keys),
// and a line without its closing "end" (cut short) is refused.
#pragma once
#include <string>

#include "mods.h"

class Sim;
class Car;

namespace carsave {

std::string capture(const Sim& sim, const Car& c);
// The car must already be built for the text's vehicle and mods (vehicleOf, modsOf). It's
// put at the saved place, at rest, with everything the text says. False when the text is
// for another vehicle or build (another catalog index or mods, or the model changed since:
// not the same nodes or beams) or isn't a condition at all: the car is then left as it was
// built, undamaged, at the saved place if the text has one.
bool restore(Sim& sim, Car& c, const std::string& text);
// The catalog vehicle the text is for (-1: none), and its mods.
int vehicleOf(const std::string& text);
ModSet modsOf(const std::string& text);

}  // namespace carsave
