#pragma once

// The overlay draws with Dear ImGui, which is vendored and built only on Windows.
// The portable mock build (Makefile) compiles the same translation units with
// HAVE_IMGUI undefined, and `make check-ui` defines it on the command line with a
// forced -include of imgui.h, so the drawing code is syntax-checked on the Mac
// too. Headers that need ImGui types include this so the detection lives in one place.

#ifdef _WIN32
#if __has_include("imgui.h")
#include "imgui.h"
#ifndef HAVE_IMGUI
#define HAVE_IMGUI 1
#endif
#endif
#endif
