// Which build this is, for the F3 screen: the git commit it was made from (Makefile, build/buildid.txt), when
// buildinfo.cpp was last compiled (so, when that commit changed), and whether it is the dev binary or a release one.
#pragma once
#include <string>

std::string buildDescription();  // "A1B2C3D  DEV BUILD  BUILT OCT 17 2026 12:00:00"
