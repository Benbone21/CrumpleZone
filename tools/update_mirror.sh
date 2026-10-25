#!/bin/bash
# Copies the newest Crumple Zone release that carries a signed update manifest from GitHub to the update server's folder, checks
# everything with cz-update (the public key compiled in), and only then makes it the version the games are offered.
# docs/server-updates-setup.md explains the setup (a systemd timer runs this). No secrets: the server only ever has public data.
#
#   tools/update_mirror.sh             fetch the latest release if it is new (safe to run often; does nothing when up to date)
#   tools/update_mirror.sh --use V     offer version V again (rollback: it must still be in the folder), and HOLD there: later runs
#                                      don't move on to newer releases until --unhold
#   tools/update_mirror.sh --unhold    end the hold (and fetch the latest now)
#   tools/update_mirror.sh --status    what is offered and what is kept
#
# Settings (environment):
#   CZ_UPDATES_ROOT   the served folder (default /srv/crumplezone-updates): manifest.txt and v<version>/ beside it
#   CZ_UPDATE_TOOL    cz-update built from this repository (default ~/crumplezone-src/build/cz-update)
#   CZ_UPDATE_PUBKEY  optional: check against this public key (64 hex) instead of the one compiled into the tool
#   CZ_REPO           GitHub owner/name (default Benbone21/crumplezone)
#   CZ_KEEP           versions kept on disk (default 3; the offered one is always kept)
#   CZ_API, CZ_DOWNLOAD_BASE   (tests: where "latest release" and its files come from)
set -euo pipefail

ROOT=${CZ_UPDATES_ROOT:-/srv/crumplezone-updates}
TOOL=${CZ_UPDATE_TOOL:-$HOME/crumplezone-src/build/cz-update}
REPO=${CZ_REPO:-Benbone21/crumplezone}
API=${CZ_API:-https://api.github.com/repos/$REPO/releases/latest}
KEEP=${CZ_KEEP:-3}

log() { echo "update_mirror: $*"; }
die() { echo "update_mirror: $*" >&2; exit 1; }
verify() {  # verify MANIFEST [--files DIR]
    if [ -n "${CZ_UPDATE_PUBKEY:-}" ]; then "$TOOL" --verify "$1" --key "$CZ_UPDATE_PUBKEY" "${@:2}"; else "$TOOL" --verify "$@"; fi
}
offered() { [ -f "$ROOT/manifest.txt" ] && awk '$1=="version"{print $2; exit}' "$ROOT/manifest.txt" || true; }
publish() {  # publish VERSION: manifest.txt becomes v<VERSION>/update-manifest.txt (atomically)
    cp "$ROOT/v$1/update-manifest.txt" "$ROOT/.manifest.tmp"
    chmod 644 "$ROOT/.manifest.tmp"
    mv -f "$ROOT/.manifest.tmp" "$ROOT/manifest.txt"
    log "now offering $1"
}

[ -x "$TOOL" ] || die "no cz-update at $TOOL (build it: make build/cz-update)"
mkdir -p "$ROOT"

case "${1:-}" in
    --status)
        echo "offered: $(offered)"
        [ -f "$ROOT/.hold" ] && echo "held:    yes (at $(cat "$ROOT/.hold"); --unhold to end it)"
        for d in "$ROOT"/v*/; do [ -d "$d" ] && echo "kept:    $(basename "$d") ($(du -sh "$d" | cut -f1))"; done
        exit 0 ;;
    --use)
        v=${2:?usage: --use VERSION}
        [ -f "$ROOT/v$v/update-manifest.txt" ] || die "v$v is not in $ROOT"
        verify "$ROOT/v$v/update-manifest.txt" --files "$ROOT/v$v" >/dev/null || die "v$v doesn't verify any more: not offering it"
        publish "$v"
        echo "$v" > "$ROOT/.hold"
        log "held at $v: newer releases are not offered until --unhold"
        exit 0 ;;
    --unhold)
        rm -f "$ROOT/.hold"
        log "hold ended" ;;
    "")
        if [ -f "$ROOT/.hold" ]; then exit 0; fi ;;  # (held by --use: quiet)
    *) die "unknown option $1" ;;
esac

tag=$(curl -fsSL --proto =https,http --max-time 60 "$API" | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])') ||
    die "can't read the latest release from $API"
v=${tag#[vV]}
case "$v" in *[!0-9A-Za-z.-]*|"") die "odd release tag '$tag'";; esac
DL=${CZ_DOWNLOAD_BASE:-https://github.com/$REPO/releases/download}/$tag
current=$(offered)
if [ "$current" = "$v" ]; then exit 0; fi  # (up to date: quiet)
if [ -n "$current" ] && [ "$(printf '%s\n%s\n' "$current" "$v" | sort -V | tail -1)" != "$v" ]; then
    log "the latest release ($v) is older than the one offered ($current): leaving it (use --use to go back on purpose)"
    exit 0
fi

work=$(mktemp -d "$ROOT/.incoming.XXXXXX")
trap 'rm -rf "$work"' EXIT
if ! curl -fsSL --max-time 120 -o "$work/update-manifest.txt" "$DL/update-manifest.txt"; then
    log "release $tag has no update-manifest.txt (not signed): nothing to do"
    exit 0
fi
verify "$work/update-manifest.txt" > "$work/.verify" || { cat "$work/.verify" >&2 || true; die "the manifest of $tag does NOT verify: not using it"; }
mv=$(awk '$1=="version"{print $2; exit}' "$work/update-manifest.txt")
[ "$mv" = "$v" ] || die "the manifest says $mv but the tag is $tag: not using it"
awk '$1=="file"{print $3}' "$work/update-manifest.txt" | while read -r url; do
    case "$url" in "v$v/"*) ;; *) die "file URL '$url' is not under v$v/ (CI set absolute URLs?): not using it";; esac
    name=${url#"v$v/"}
    case "$name" in */*|.*|"") die "odd file name '$name'";; esac
    log "downloading $name"
    curl -fsSL --retry 3 --max-time 1800 -o "$work/$name" "$DL/$name" || die "can't download $name"
done
verify "$work/update-manifest.txt" --files "$work" > "$work/.verify" || { cat "$work/.verify" >&2 || true; die "the files of $tag don't match the manifest: not using them"; }
rm -f "$work/.verify"
chmod 755 "$work"
chmod 644 "$work"/*
rm -rf "$ROOT/v$v"
mv "$work" "$ROOT/v$v"
trap - EXIT
publish "$v"

# Old versions: keep the newest $KEEP (and always the offered one).
ls -d "$ROOT"/v*/ 2>/dev/null | xargs -n1 basename | sed 's/^v//' | sort -V | head -n -"$KEEP" | while read -r old; do
    [ "$old" = "$v" ] && continue
    log "removing old v$old"
    rm -rf "${ROOT:?}/v$old"
done
