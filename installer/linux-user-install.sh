#!/bin/sh
# Crumple Zone for this user only, with no sudo, in a place the game can update itself (src/updater.h):
#   ~/.local/share/crumplezone/app/                      the game (a copy of this folder)
#   ~/.local/share/crumplezone/app.previous/             the version before, after an update (rollback)
#   ~/.local/bin/crumplezone                             a two-line wrapper that starts it
#   ~/.local/share/applications/crumplezone.desktop      the games-menu entry (over the .deb's, if both are installed)
# Run it from the unzipped CrumpleZone-<version>-linux.zip:  ./install.sh      Remove it again:  ./install.sh --uninstall
# Settings and the garage stay in ~/.config/crumplezone either way. The .deb (sudo apt install ./...deb) still works for a
# system-wide install; that one doesn't update itself.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
root=$data/crumplezone
app=$root/app
bin=$HOME/.local/bin
desktop=$data/applications/crumplezone.desktop

if [ "${1:-}" = "--uninstall" ]; then
    rm -rf "$root"
    rm -f "$bin/crumplezone" "$desktop"
    echo "Crumple Zone removed from $root (settings in ~/.config/crumplezone kept)."
    exit 0
fi

if [ ! -x "$here/crumplezone" ] || ! grep -q '^layout linux' "$here/cz-install.txt" 2>/dev/null; then
    echo "install.sh: run this from the unzipped Crumple Zone folder (crumplezone and cz-install.txt beside it)" >&2
    exit 1
fi
if [ "$here" = "$app" ]; then
    echo "Already installed here: $app"
    exit 0
fi

mkdir -p "$root" "$bin" "$(dirname "$desktop")"
rm -rf "$app.new"
cp -a "$here" "$app.new"
if [ -d "$app" ]; then
    rm -rf "$app.previous"
    mv "$app" "$app.previous"
fi
mv "$app.new" "$app"

printf '#!/bin/sh\n# Crumple Zone (installed by install.sh; the game updates itself in %s)\nexec "%s/crumplezone" "$@"\n' "$app" "$app" > "$bin/crumplezone"
chmod 755 "$bin/crumplezone"
printf '[Desktop Entry]\nType=Application\nName=Crumple Zone\nComment=Soft-body driving: crash it, crawl it, race it\nExec=%s\nTerminal=false\nCategories=Game;Simulation;\n' "$bin/crumplezone" > "$desktop"

echo "Crumple Zone installed in $app"
echo "Start it from the games menu, or run: crumplezone"
case ":$PATH:" in
    *":$bin:"*) ;;
    *) echo "(Note: $bin is not on your PATH yet; log out and in again, or run $bin/crumplezone)" ;;
esac
if [ -e /usr/games/crumplezone ]; then
    echo "(The .deb version is installed too. The menu now starts this one; remove the .deb with: sudo apt remove crumplezone)"
fi
