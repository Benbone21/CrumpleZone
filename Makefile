# Crumple Zone — soft-body driving sandbox for Linux.
# Needs only g++ and the SDL2 runtime library (libsdl2-2.0-0); no -dev packages.

CXX      ?= g++
CXXFLAGS ?= -O2 -march=native
CXXFLAGS += -std=c++17 -Wall -Wextra
LDLIBS   := -l:libSDL2-2.0.so.0 -lpthread -lm

PHYS     := src/jobs.cpp src/walker.cpp src/softbody.cpp src/recovery.cpp src/net.cpp src/level.cpp src/levels_builtin.cpp src/levelrun.cpp src/editcore.cpp src/autopilot.cpp src/pilotsense.cpp src/carbuild.cpp src/world.cpp src/mountain.cpp src/freeway.cpp src/frontier.cpp src/backcountry.cpp src/vehicles.cpp src/mods.cpp
SCEN     := tests/jobs.cpp tests/walker.cpp tests/editor.cpp tests/levels.cpp tests/circuit.cpp tests/frontier.cpp tests/backcountry.cpp tests/freeway.cpp tests/surfaces.cpp tests/handling.cpp tests/harness.cpp tests/scenarios.cpp tests/components.cpp tests/offroad.cpp tests/recovery.cpp tests/net.cpp tests/autopilot.cpp
GAME_SRC := src/main.cpp src/editor.cpp src/coop.cpp src/gl.cpp src/render.cpp src/carvisual.cpp src/bodyshell.cpp src/scenery.cpp src/menu.cpp src/fx.cpp src/audio.cpp src/music.cpp src/daylight.cpp src/lights.cpp src/groundtex.cpp src/groundshader.cpp $(PHYS) $(SCEN)
GAME_OBJ := $(patsubst %.cpp,build/%.o,$(GAME_SRC))
TEST_OBJ := $(patsubst %.cpp,build/%.o,tests/run_tests.cpp $(PHYS) $(SCEN))
HEADERS  := $(wildcard src/*.h tests/*.h)

all: crumplezone

crumplezone: $(GAME_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# The same game linked to a side path, for testing while ./crumplezone is in use.
build/crumplezone-dev: $(GAME_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/run_tests: $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# Render-mesh integration checks, with no SDL window or OpenGL context.
build/test_jesko_visual: build/tests/jesko_visual.o build/src/carvisual.o build/src/bodyshell.o $(patsubst %.cpp,build/%.o,$(PHYS)) build/tests/harness.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

build/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ---- Windows build: cross-compiled with mingw-w64 (posix threads) ------------------
#   make windows   ->  build/win/CrumpleZone/ (crumplezone.exe + SDL2.dll) and build/win/CrumpleZone-windows.zip
# Needs: the g++-mingw-w64-x86-64-posix package, and SDL2.dll (the official SDL2 x64
# Windows runtime) in $(WINSDL).
WINCXX   ?= x86_64-w64-mingw32-g++-posix
WINSDL   ?= third_party/sdl2-win64
WINFLAGS := -O2 -std=gnu++17 -Wall -D_WIN32_WINNT=0x0601
WIN_OBJ  := $(patsubst %.cpp,build/win/obj/%.o,$(GAME_SRC))

build/win/obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(WINCXX) $(WINFLAGS) -c -o $@ $<

build/win/CrumpleZone/crumplezone.exe: $(WIN_OBJ)
	@mkdir -p $(dir $@)
	$(WINCXX) $(WINFLAGS) -mwindows -o $@ $^ $(WINSDL)/SDL2.dll -lws2_32 -static -static-libgcc -static-libstdc++

windows: build/win/CrumpleZone/crumplezone.exe
	cp $(WINSDL)/SDL2.dll build/win/CrumpleZone/
	cp README.md build/win/CrumpleZone/README.txt
	cd build/win && rm -f CrumpleZone-windows.zip && zip -qr CrumpleZone-windows.zip CrumpleZone

# Runs every scenario test in parallel and writes build/test_report.html.
# Pass a filter with: make test T=braking
test: build/run_tests
	./build/run_tests $(T)

run: crumplezone
	./crumplezone

clean:
	rm -rf build crumplezone

.PHONY: windows all test run clean
