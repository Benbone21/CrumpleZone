# Benchmarking on a Windows or Mac laptop

Copy-paste steps to build Crumple Zone on a laptop, run the benchmark suite in a real window and send the results back. Takes about 10 minutes plus the benchmark (about 12 runs of a few seconds each).

What a fresh clone contains: `assets/ground`, `assets/props` and the code. `assets/source` (raw scans) is not in the repo and is not needed. `assets/models/choices.txt` (the model lab's picks) is optional; without it the game uses its default model designs. Nothing needs downloading or baking.

## Windows (MSYS2)
1. Install MSYS2 from https://www.msys2.org, then open the **MSYS2 UCRT64** shell (not the plain MSYS one).
2. Prerequisites (add `zip` if you want a zip of the build):
   ```
   pacman -S --needed make mingw-w64-ucrt-x86_64-gcc git
   ```
3. Clone and build (SDL2.dll is already in `third_party/sdl2-win64`):
   ```
   git clone https://github.com/Benbone21/CrumpleZone.git MemeNG    # (if it is private, sign in when git asks)
   cd MemeNG
   make -j$(nproc) windows
   ```
   `make windows` detects MSYS2 and uses that shell's `g++`. In another shell, run `make windows WINCXX=g++`. The game ends up in `build/win/CrumpleZone/crumplezone.exe` (a zip is made if `zip` is installed).
4. Run the suite (from the same shell):
   ```
   cd build/win/CrumpleZone && ./crumplezone.exe --bench-suite
   ```
   Do not touch the window until it closes. The exe has no console, so everything goes to files.
5. Results: `%APPDATA%\crumplezone\bench\` (`explorer "$APPDATA/crumplezone/bench"`). Also `%APPDATA%\crumplezone\graphics.log` (the console output).
6. Discrete GPU: Settings > System > Display > Graphics > Add desktop app > pick `crumplezone.exe` > Options > High performance. Plug the laptop in and set the power mode to Best performance. The first lines of the report name the GPU the game really used; check it is not the Intel or AMD integrated one.

## Mac
1. Prerequisites: `xcode-select --install`. Download `SDL2-2.30.x.dmg` (the newest 2.30.x) from https://github.com/libsdl-org/SDL/releases, open it, and copy `SDL2.framework` to `third_party/sdl2-mac/` inside the clone (create that folder; it is git-ignored).
2. Clone and build the quick native build (this machine's architecture, no app bundle):
   ```
   git clone https://github.com/Benbone21/CrumpleZone.git MemeNG
   cd MemeNG
   mkdir -p third_party/sdl2-mac
   cp -R /Volumes/SDL2/SDL2.framework third_party/sdl2-mac/
   make -j$(sysctl -n hw.ncpu) mac-dev
   ```
   (`make mac` builds the universal signed app instead, which is slower.)
3. Run the suite:
   ```
   ./build/macdev/crumplezone --bench-suite
   ```
4. Results: `~/.config/crumplezone/bench/` (`open ~/.config/crumplezone/bench`).
5. Discrete GPU: on a MacBook Pro with two GPUs, System Settings > Battery (or Energy Saver) > turn off "Automatic graphics switching" so it stays on the discrete GPU; otherwise macOS picks it for OpenGL games on its own. Plug in, power mode High Performance if offered.

## Options
- `--bench-suite [FRAMES[,WARMUP]]`: Frontier, Open Country (Wilds) and Boulder Gully, each at POTATO, LOW, MEDIUM and HIGH; default 600 frames after 120. Add `--size 1920x1080` (default: the window's size) or `--bench-out FILE` (the summary file; the screenshots go beside it).
- `--bench FRAMES[,WARMUP] [--map N] [--quality 0-3|potato]`: one run. The report file is `<date-time>-<map>-<quality>-<WxH>.txt` in the same folder, or `--bench-out FILE`.
- Each run is a lap by a drone camera, the same every time, with VSync off and a fixed 60 Hz physics step, so frame times are real GPU and CPU cost.

## What the suite writes
In the bench folder, with `STAMP` the date and time:
- `suite-STAMP-WxH.txt`: the machine (GPU, vendor, GL version and driver, CPU, cores, RAM, OS, display refresh rate), a table of all runs, one `suite-csv:` line per run, then each run's per-stage CPU and GPU table.
- `suite-STAMP-WxH-<map>-<quality>.bmp`: one screenshot per run with the F3 stats showing.

## What to send back
The whole bench folder (`suite-*.txt` and the `.bmp` files), plus: the laptop model, whether it was plugged in, and which GPU it used. When the game opens, press F3 in a normal run if the report's GPU line looks wrong.
