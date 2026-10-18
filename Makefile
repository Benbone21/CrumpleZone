# Crumple Zone — soft-body driving sandbox for Linux.
# Needs only g++ and the SDL2 runtime library (libsdl2-2.0-0); no -dev packages.

CXX      ?= g++
CXXFLAGS ?= -O2 -march=native
# -ffp-contract=off (every build, 'override' so a command-line CXXFLAGS keeps it): no FMA fusing, so the sim is bit-identical between -march=native and x86-64-v2/Windows builds.
FPFLAGS  := -ffp-contract=off -fno-fast-math
override CXXFLAGS += $(FPFLAGS) -std=c++17 -Wall -Wextra
LDLIBS   := -l:libSDL2-2.0.so.0 -lpthread -lm

MODELS   := $(sort $(wildcard src/models/*.cpp))
PHYS     := src/modelkit.cpp $(MODELS) src/perf.cpp src/physics_workers.cpp src/collision.cpp src/props.cpp src/jobs.cpp src/walker.cpp src/softbody.cpp src/recovery.cpp src/net.cpp src/level.cpp src/levels_builtin.cpp src/levelrun.cpp src/editcore.cpp src/saveflow.cpp src/textedit.cpp src/autopilot.cpp src/pilotsense.cpp src/carbuild.cpp src/world.cpp src/spawnpad.cpp src/sections.cpp src/mountain.cpp src/freeway.cpp src/frontier.cpp src/backcountry.cpp src/vehicles.cpp src/mods.cpp src/gamepad.cpp src/platform.cpp src/tires.cpp src/interact.cpp src/fixes.cpp src/carparts.cpp src/carframe.cpp src/vtools.cpp src/keybinds.cpp src/devmenu.cpp src/career.cpp src/anchors.cpp src/biome.cpp src/plants.cpp src/handwork.cpp src/sectionfile.cpp src/trunk.cpp src/invmenu.cpp src/objshape.cpp src/bodyshell.cpp src/throttle.cpp src/debuginfo.cpp src/forest.cpp src/meshopt.cpp src/frameprof.cpp src/watermesh.cpp src/texenc.cpp
SCEN     := tests/perf.cpp tests/drivetrain.cpp tests/net_state.cpp tests/collision.cpp tests/props.cpp tests/jobs.cpp tests/walker.cpp tests/editor.cpp tests/levels.cpp tests/circuit.cpp tests/frontier.cpp tests/backcountry.cpp tests/freeway.cpp tests/surfaces.cpp tests/handling.cpp tests/harness.cpp tests/scenarios.cpp tests/components.cpp tests/offroad.cpp tests/recovery.cpp tests/net.cpp tests/autopilot.cpp tests/nearby.cpp tests/gamepad.cpp tests/tires.cpp tests/vtools.cpp tests/simulation.cpp tests/vehicle_physics.cpp tests/dynamics.cpp tests/extreme.cpp tests/environment.cpp tests/towing.cpp tests/keybinds.cpp tests/devmenu.cpp tests/treads.cpp tests/wobble.cpp tests/career.cpp tests/models.cpp tests/carparts.cpp tests/sections.cpp tests/anchors.cpp tests/music.cpp tests/trunk.cpp tests/fieldrepair.cpp tests/net_security.cpp tests/carframe.cpp tests/spawnpads.cpp tests/invmenu.cpp tests/rumble.cpp tests/editbrush.cpp tests/objshape.cpp tests/editobjects.cpp tests/throttle.cpp tests/f3info.cpp tests/plants.cpp tests/forest.cpp tests/grass.cpp tests/textedit.cpp tests/winch_payout.cpp tests/meshopt.cpp tests/frameprof.cpp tests/crashshare.cpp tests/gfxdetect.cpp tests/net_bandwidth.cpp tests/net_time.cpp tests/texenc.cpp tests/carshell.cpp
GAME_SRC := src/main.cpp src/lab.cpp src/gamepad_sdl.cpp src/editor.cpp src/coop.cpp src/gl.cpp src/gpu.cpp src/render.cpp src/carvisual.cpp src/scenery.cpp src/menu.cpp src/fx.cpp src/engine_samples.cpp src/engineaudio.cpp src/audio.cpp src/music.cpp src/ambience.cpp src/daylight.cpp src/lights.cpp src/groundtex.cpp src/groundshader.cpp src/propdraw.cpp src/invdraw.cpp src/debugscreen.cpp src/buildinfo.cpp $(PHYS) $(SCEN) src/treedraw.cpp src/texcomp.cpp
GAME_OBJ := $(patsubst %.cpp,build/%.o,$(GAME_SRC))
TEST_OBJ := $(patsubst %.cpp,build/%.o,tests/run_tests.cpp $(PHYS) $(SCEN) src/music.cpp)
HEADERS  := $(wildcard src/*.h tests/*.h)

all: crumplezone

# Geometry-only culling and crack-free terrain LOD checks (no SDL/OpenGL).
build/test_visibility: tests/visibility.cpp src/visibility.h src/mathx.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $<

build/win/test_visibility.exe: tests/visibility.cpp src/visibility.h src/mathx.h
	@mkdir -p build/win
	$(WINCXX) $(WINFLAGS) -o $@ $< -static -static-libgcc -static-libstdc++

build/mac/test_visibility: tests/visibility.cpp src/visibility.h src/mathx.h
	@mkdir -p build/mac
	$(MACCXX) $(MACFLAGS) -o $@ $<

crumplezone: $(GAME_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# The same game linked to a side path, for testing while ./crumplezone is in use.
build/crumplezone-dev: $(GAME_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/run_tests: $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# Dedicated multiplayer server (headless, no SDL/OpenGL).
build/crumplezone-server: build/src/server_main.o $(PHYS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# CPU-only workload; args: car count, separated|pileup, 2 kHz steps.
build/bench_sim: build/tools/bench_sim.o $(patsubst %.cpp,build/%.o,$(PHYS))
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# Prototype performance targets are measured separately from functional scenario checks.
build/bench_autopilot: build/tests/run_tests.o build/tests/lidar.o build/tests/harness.o $(patsubst %.cpp,build/%.o,$(PHYS))
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# Run with SDL_VIDEODRIVER=offscreen; exercises shaders, quality presets and MSAA fallback.
build/test_render: build/tests/render.o $(filter-out build/src/main.o,$(GAME_OBJ))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

build/test_audio: build/tests/audio.o build/tests/engine_samples.o build/tests/soundscape.o build/src/audio.o build/src/engineaudio.o build/src/engine_samples.o build/src/music.o build/src/ambience.o $(patsubst %.cpp,build/%.o,$(PHYS))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# Every mesh the game builds, what's wasted in it (meshopt.h): SDL_VIDEODRIVER=offscreen build/meshaudit [--fast].
build/meshaudit: build/tools/meshaudit.o $(filter-out build/src/main.o,$(GAME_OBJ))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

meshaudit: build/meshaudit
	SDL_VIDEODRIVER=offscreen ./build/meshaudit

# Contact sheets of the code-made models: build/modelsheet [--out DIR] [id ...].
build/modelsheet: build/tools/modelsheet.o build/src/modelkit.o build/src/meshopt.o build/src/platform.o $(patsubst %.cpp,build/%.o,$(MODELS))
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

# Silent audition files: build/engine_preview [output directory].
build/soundscape_preview: build/tools/soundscape_preview.o build/src/music.o build/src/ambience.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

# One WAV per biome's music, plus the maps' own pieces to compare levels with: build/biome_preview [directory [seconds [seed]]].
build/biome_preview: build/tools/biome_preview.o build/src/music.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lm

build/engine_preview: build/tools/engine_preview.o build/src/engineaudio.o build/src/engine_samples.o $(patsubst %.cpp,build/%.o,$(PHYS))
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

# Same CPU benchmark compiled with the portable Windows release flags.
build/win/bench_sim.exe: build/win/obj/tools/bench_sim.o $(patsubst %.cpp,build/win/obj/%.o,$(PHYS))
	$(WINCXX) $(WINFLAGS) -o $@ $^ -lws2_32 -liphlpapi -static -static-libgcc -static-libstdc++

build/win/run_tests.exe: build/win/obj/tests/run_tests.o $(patsubst %.cpp,build/win/obj/%.o,$(PHYS) $(SCEN) src/music.cpp)
	$(WINCXX) $(WINFLAGS) -o $@ $^ -lws2_32 -liphlpapi -static -static-libgcc -static-libstdc++

build/win/test_audio.exe: build/win/obj/tests/audio.o build/win/obj/tests/engine_samples.o build/win/obj/tests/soundscape.o build/win/obj/src/audio.o build/win/obj/src/engineaudio.o build/win/obj/src/engine_samples.o build/win/obj/src/music.o build/win/obj/src/ambience.o $(patsubst %.cpp,build/win/obj/%.o,$(PHYS))
	$(WINCXX) $(WINFLAGS) -o $@ $^ $(WINSDL)/SDL2.dll -lws2_32 -liphlpapi -static -static-libgcc -static-libstdc++

# Render-mesh integration checks, with no SDL window or OpenGL context.
build/test_jesko_visual: build/tests/jesko_visual.o build/src/carvisual.o $(patsubst %.cpp,build/%.o,$(PHYS)) build/tests/harness.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lpthread -lm

build/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# The commit the F3 screen reports: build/buildid.txt is rewritten only when it changes, so buildinfo.o (and the relink)
# only follow a new commit.
GITID := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
build/buildid.txt: FORCE
	@mkdir -p build
	@echo '$(GITID)' | cmp -s - $@ || echo '$(GITID)' > $@
build/src/buildinfo.o: build/buildid.txt
build/src/buildinfo.o: CXXFLAGS += -DCZ_GIT='"$(GITID)"'
FORCE:
.PHONY: FORCE

# ---- Release builds: LTO, optional PGO (dev builds above stay plain and fast) ----------
#   make build/rel/crumplezone       ->  generic x86-64-v2, -flto, -fno-plt, stripped (also build/rel/{bench_sim,run_tests,...})
#   tools/pgo.sh linux && make PGO=use build/rel/crumplezone   ->  the same, profile-guided (see tools/pgo.sh)
# No -ffast-math: the sim and the net sync must stay bit-identical across builds and platforms.
LTOJ     ?= auto
RELARCH  ?= -march=x86-64-v2 -mtune=generic
PGODIR   ?= $(CURDIR)/build/pgo
PGO      ?=
ifeq ($(PGO),gen)
PGOFLAGS := -fprofile-generate=$(PGODIR) -fprofile-update=prefer-atomic
else ifeq ($(PGO),use)
PGOFLAGS := -fprofile-use=$(PGODIR) -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch
else
PGOFLAGS :=
endif
RELFLAGS := -O2 $(RELARCH) -flto=$(LTOJ) -fno-plt $(FPFLAGS) $(PGOFLAGS) -std=c++17 -Wall -Wextra
RELLD    := -s
REL_GAME := $(patsubst %.cpp,build/rel/%.o,$(GAME_SRC))
REL_PHYS := $(patsubst %.cpp,build/rel/%.o,$(PHYS))
REL_TEST := $(patsubst %.cpp,build/rel/%.o,tests/run_tests.cpp $(PHYS) $(SCEN) src/music.cpp)

build/rel/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(RELFLAGS) -c -o $@ $<
build/rel/src/buildinfo.o: build/buildid.txt
build/rel/src/buildinfo.o: RELFLAGS += -DCZ_GIT='"$(GITID)"'

build/rel/crumplezone: $(REL_GAME)
	$(CXX) $(RELFLAGS) $(RELLD) -o $@ $^ $(LDLIBS)
build/rel/run_tests: $(REL_TEST)
	$(CXX) $(RELFLAGS) $(RELLD) -o $@ $^ -lpthread -lm
build/rel/bench_sim: build/rel/tools/bench_sim.o $(REL_PHYS)
	$(CXX) $(RELFLAGS) $(RELLD) -o $@ $^ -lpthread -lm
build/rel/test_render: build/rel/tests/render.o $(filter-out build/rel/src/main.o,$(REL_GAME))
	$(CXX) $(RELFLAGS) $(RELLD) -o $@ $^ $(LDLIBS)
build/rel/test_audio: build/rel/tests/audio.o build/rel/tests/engine_samples.o build/rel/tests/soundscape.o build/rel/src/audio.o build/rel/src/engineaudio.o build/rel/src/engine_samples.o build/rel/src/music.o build/rel/src/ambience.o $(REL_PHYS)
	$(CXX) $(RELFLAGS) $(RELLD) -o $@ $^ $(LDLIBS)

# ---- Windows build: cross-compiled with mingw-w64 (posix threads) ------------------
#   make windows   ->  build/win/CrumpleZone/ (crumplezone.exe + SDL2.dll) and build/win/CrumpleZone-windows.zip
# Needs: the g++-mingw-w64-x86-64-posix package, and SDL2.dll (the official SDL2 x64
# Windows runtime) in $(WINSDL).
WINCXX   ?= x86_64-w64-mingw32-g++-posix
WINSDL   ?= third_party/sdl2-win64
# LTO always (every Windows build is a release); PGO as for Linux (tools/pgo.sh windows: trained under wine, since gcc profiles are not portable across compiler builds), -s strips the link.
WINPGO   := $(if $(filter use,$(PGO)),-fprofile-use=$(PGODIR)/win -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch,$(if $(filter gen,$(PGO)),-fprofile-generate=$(PGODIR)/win -fprofile-update=prefer-atomic))
WINFLAGS := -O2 -march=x86-64-v2 -mtune=generic -flto=$(LTOJ) $(FPFLAGS) $(WINPGO) -std=gnu++17 -Wall -D_WIN32_WINNT=0x0601
WIN_OBJ  := $(patsubst %.cpp,build/win/obj/%.o,$(GAME_SRC))

build/win/obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(WINCXX) $(WINFLAGS) -c -o $@ $<

build/win/CrumpleZone/crumplezone.exe: $(WIN_OBJ)
	@mkdir -p $(dir $@)
	$(WINCXX) $(WINFLAGS) -mwindows -s -o $@ $^ $(WINSDL)/SDL2.dll -lws2_32 -liphlpapi -static -static-libgcc -static-libstdc++

windows: build/win/CrumpleZone/crumplezone.exe
	cp $(WINSDL)/SDL2.dll build/win/CrumpleZone/
	cp README.md build/win/CrumpleZone/README.txt
	@mkdir -p build/win/CrumpleZone/docs
	cp docs/performance.md build/win/CrumpleZone/docs/
	cp docs/multiplayer.md build/win/CrumpleZone/docs/
	cp docs/recovery-audio.md build/win/CrumpleZone/docs/
	cp docs/audio-soundscape.md docs/engine-recordings.md build/win/CrumpleZone/docs/
	rm -rf build/win/CrumpleZone/assets && mkdir -p build/win/CrumpleZone/assets
	cp -r assets/ground build/win/CrumpleZone/assets/
	if [ -d assets/audio ]; then cp -r assets/audio build/win/CrumpleZone/assets/; fi
	if [ -f assets/props/props.txt ]; then cp -r assets/props build/win/CrumpleZone/assets/; fi
	if [ -d assets/models ]; then cp -r assets/models build/win/CrumpleZone/assets/; fi
	cd build/win && rm -f CrumpleZone-windows.zip && zip -qr CrumpleZone-windows.zip CrumpleZone

# Windows installer (NSIS): build/win/CrumpleZone-setup.exe from the windows build. Needs makensis (apt: nsis).
windows-installer: windows
	makensis -V2 -DVERSION=$(VERSION) -DSRC=$(CURDIR)/build/win/CrumpleZone -DOUT=$(CURDIR)/build/win/CrumpleZone-setup.exe installer/windows.nsi

# ---- macOS build: run on a Mac (Xcode command line tools) ----------------------------
#   make mac [VERSION=x.y.z]  ->  universal app, ZIP and DMG
# A universal (Apple silicon + Intel) app with SDL2.framework inside it. Needs the official
# SDL2 macOS framework (from the SDL2-2.x.dmg) in $(MACSDL). Ad-hoc signed only: the first
# launch needs right-click > Open (or: xattr -dr com.apple.quarantine CrumpleZone.app).
VERSION  ?= 0.2.0
MACCXX   ?= clang++
MACSDL   ?= third_party/sdl2-mac
MACLTO   ?= -flto=thin
MACFLAGS := -O2 $(FPFLAGS) $(MACLTO) -std=c++17 -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 -Wall
MAC_OBJ  := $(patsubst %.cpp,build/mac/obj/%.o,$(GAME_SRC))
MACAPP   := build/mac/CrumpleZone.app

build/mac/obj/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(MACCXX) $(MACFLAGS) -c -o $@ $<

build/mac/crumplezone: $(MAC_OBJ)
	$(MACCXX) $(MACFLAGS) -o $@ $^ -F$(MACSDL) -framework SDL2 -Wl,-rpath,@executable_path/../Frameworks

mac: build/mac/crumplezone
	rm -rf $(MACAPP) && mkdir -p $(MACAPP)/Contents/MacOS $(MACAPP)/Contents/Resources/assets $(MACAPP)/Contents/Frameworks
	cp build/mac/crumplezone $(MACAPP)/Contents/MacOS/crumplezone
	cp -R assets/ground $(MACAPP)/Contents/Resources/assets/
	if [ -d assets/audio ]; then cp -R assets/audio $(MACAPP)/Contents/Resources/assets/; fi
	if [ -f assets/props/props.txt ]; then cp -R assets/props $(MACAPP)/Contents/Resources/assets/; fi
	if [ -d assets/models ]; then cp -R assets/models $(MACAPP)/Contents/Resources/assets/; fi
	cp -R $(MACSDL)/SDL2.framework $(MACAPP)/Contents/Frameworks/
	cp README.md $(MACAPP)/Contents/Resources/README.md
	printf '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict>\n<key>CFBundleName</key><string>Crumple Zone</string>\n<key>CFBundleDisplayName</key><string>Crumple Zone</string>\n<key>CFBundleIdentifier</key><string>io.github.benbone21.crumplezone</string>\n<key>CFBundleExecutable</key><string>crumplezone</string>\n<key>CFBundlePackageType</key><string>APPL</string>\n<key>CFBundleShortVersionString</key><string>$(VERSION)</string>\n<key>CFBundleVersion</key><string>$(VERSION)</string>\n<key>LSMinimumSystemVersion</key><string>11.0</string>\n<key>LSApplicationCategoryType</key><string>public.app-category.simulation-games</string>\n<key>NSHighResolutionCapable</key><true/>\n</dict></plist>\n' > $(MACAPP)/Contents/Info.plist
	codesign --force --deep --sign - $(MACAPP)
	cd build/mac && rm -f CrumpleZone-mac.zip && ditto -c -k --keepParent CrumpleZone.app CrumpleZone-mac.zip

# DMG contains the signed app and an Applications shortcut for drag-and-drop installation.
mac-dmg: mac
	rm -rf build/mac/dmg-root && mkdir -p build/mac/dmg-root
	ditto $(MACAPP) build/mac/dmg-root/CrumpleZone.app
	ln -s /Applications build/mac/dmg-root/Applications
	cp README.md build/mac/dmg-root/README.md
	hdiutil create -ov -volname "Crumple Zone $(VERSION)" -srcfolder build/mac/dmg-root -format UDZO build/mac/CrumpleZone-mac.dmg
	@verified=false; for attempt in 1 2 3 4 5; do \
	  if hdiutil verify build/mac/CrumpleZone-mac.dmg; then verified=true; break; fi; sleep 3; \
	done; test "$$verified" = true

# Native macOS regression executables, using the same universal release flags.
build/mac/bench_sim: build/mac/obj/tools/bench_sim.o $(patsubst %.cpp,build/mac/obj/%.o,$(PHYS))
	$(MACCXX) $(MACFLAGS) -o $@ $^

build/mac/run_tests: build/mac/obj/tests/run_tests.o $(patsubst %.cpp,build/mac/obj/%.o,$(PHYS) $(SCEN) src/music.cpp)
	$(MACCXX) $(MACFLAGS) -o $@ $^

build/mac/test_audio: build/mac/obj/tests/audio.o build/mac/obj/tests/engine_samples.o build/mac/obj/tests/soundscape.o build/mac/obj/src/audio.o build/mac/obj/src/engineaudio.o build/mac/obj/src/engine_samples.o build/mac/obj/src/music.o build/mac/obj/src/ambience.o $(patsubst %.cpp,build/mac/obj/%.o,$(PHYS))
	$(MACCXX) $(MACFLAGS) -o $@ $^ -F$(MACSDL) -framework SDL2 -Wl,-rpath,$(abspath $(MACSDL))

build/mac/test_render: build/mac/obj/tests/render.o $(filter-out build/mac/obj/src/main.o,$(MAC_OBJ))
	$(MACCXX) $(MACFLAGS) -o $@ $^ -F$(MACSDL) -framework SDL2 -Wl,-rpath,$(abspath $(MACSDL))

# ---- Debian package ---------------------------------------------------------------
#   make deb [VERSION=x.y.z]  ->  build/deb/crumplezone_VERSION_amd64.deb
# Installs to /opt/crumplezone (the game finds assets/ beside its executable), with
# /usr/games/crumplezone and a desktop menu entry. Settings stay in ~/.config/crumplezone.
DEBROOT := build/deb/root
deb: build/rel/crumplezone
	rm -rf $(DEBROOT) && mkdir -p $(DEBROOT)/DEBIAN $(DEBROOT)/opt/crumplezone $(DEBROOT)/usr/games $(DEBROOT)/usr/share/applications $(DEBROOT)/usr/share/doc/crumplezone
	install -m755 build/rel/crumplezone $(DEBROOT)/opt/crumplezone/crumplezone
	strip $(DEBROOT)/opt/crumplezone/crumplezone
	mkdir -p $(DEBROOT)/opt/crumplezone/assets && cp -r assets/ground $(DEBROOT)/opt/crumplezone/assets/
	if [ -d assets/audio ]; then cp -r assets/audio $(DEBROOT)/opt/crumplezone/assets/; fi
	if [ -f assets/props/props.txt ]; then cp -r assets/props $(DEBROOT)/opt/crumplezone/assets/; fi
	if [ -d assets/models ]; then cp -r assets/models $(DEBROOT)/opt/crumplezone/assets/; fi
	ln -s /opt/crumplezone/crumplezone $(DEBROOT)/usr/games/crumplezone
	cp README.md $(DEBROOT)/usr/share/doc/crumplezone/README.md
	printf '[Desktop Entry]\nType=Application\nName=Crumple Zone\nComment=Soft-body driving: crash it, crawl it, race it\nExec=/usr/games/crumplezone\nTerminal=false\nCategories=Game;Simulation;\n' > $(DEBROOT)/usr/share/applications/crumplezone.desktop
	printf 'Package: crumplezone\nVersion: $(VERSION)\nSection: games\nPriority: optional\nArchitecture: amd64\nDepends: libsdl2-2.0-0, libgl1, libc6, libstdc++6\nMaintainer: Crumple Zone <noreply@users.noreply.github.com>\nInstalled-Size: %s\nDescription: Soft-body driving sim: crash it, crawl it, race it\n Node-and-beam soft-body vehicles (ladder frames, live axles, crumple zones)\n for rock crawling, off-roading, crashes and racing.\n' "$$(du -sk $(DEBROOT) | cut -f1)" > $(DEBROOT)/DEBIAN/control
	dpkg-deb --root-owner-group --build $(DEBROOT) build/deb/crumplezone_$(VERSION)_amd64.deb

# ---- Props: the offline baker for the Poly Haven scans (Linux; needs ImageMagick's `convert`) ----
#   make bake PROPSRC=/path/to/polyhaven/models   ->  assets/props/ (committed; the game reads it)
PROPSRC ?= assets/source/polyhaven/models
build/bakeprops: tools/bakeprops.cpp src/props.cpp src/props.h src/platform.cpp src/third_party/cgltf.h src/third_party/stb_image.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -fopenmp -o $@ tools/bakeprops.cpp src/props.cpp src/platform.cpp -lm  # (platform.cpp: exeDir)

bake: build/bakeprops
	./build/bakeprops --src $(PROPSRC) --out assets/props $(BAKEARGS)

# Runs every scenario test in parallel and writes build/test_report.html.
# Pass a filter with: make test T=braking
test: build/run_tests
	./build/run_tests $(T)

run: crumplezone
	./crumplezone

clean:
	rm -rf build crumplezone

.PHONY: windows windows-installer mac mac-dmg deb all test run clean bake meshaudit
