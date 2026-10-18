# Updating the dedicated server (instructions for an agent on the server machine)

The server is built from source on the server machine (the releases ship the game, not the
server). Every player and the server must run the **same release**: a mismatch is refused at join
("version mismatch").

Assumed setup (from `docs/multiplayer.md`, "systemd"): the binary at
`/opt/crumplezone/crumplezone-server`, the unit `crumplezone.service`, the password in
`/etc/crumplezone/password`. If this machine differs, find the real paths first
(`systemctl cat crumplezone`) and use those. Never print or copy the password file.

## Rules for the agent
- Steps marked **(sudo)** need the user: show them the exact command and let them run it.
  Don't enter passwords yourself.
- Don't change the unit's hardening, the firewall, the port or the password unless asked.
- Keep the old binary until the new one is confirmed working (step 5).

## 1. Get the release's source
```bash
# first time only (no sudo; any directory the user owns):
git clone https://github.com/Benbone21/CrumpleZone.git ~/crumplezone-src
# every update:
cd ~/crumplezone-src && git fetch --tags && git checkout vX.Y.Z   # the release tag, e.g. v0.3.0
```
Build tools, if missing **(sudo)**: `sudo apt install build-essential git` (Debian/Ubuntu).
The server needs no SDL, GPU or other libraries.

## 2. Build and check it
```bash
make -j"$(nproc)" build/crumplezone-server
./build/crumplezone-server --help | head -3      # runs at all
```
`-march=native` is the default, so build on the server itself (or pass `CXXFLAGS=-O2` when
building elsewhere for a different CPU).

Smoke test on a spare port, no root, no password file needed:
```bash
CZ_PASSWORD=test timeout 5 ./build/crumplezone-server --port 28999 --map 1 --name test; echo exit=$?
```
It should print its banner (port, map, session) and exit 124 (stopped by `timeout`), not an error.

## 3. Install **(sudo)**
```bash
sudo cp /opt/crumplezone/crumplezone-server /opt/crumplezone/crumplezone-server.old
sudo install -m 755 build/crumplezone-server /opt/crumplezone/crumplezone-server
sudo systemctl restart crumplezone
```
(Restarting drops anyone connected: warn the players first.)

## 4. Confirm
```bash
systemctl status crumplezone --no-pager | head -5     # active (running)
journalctl -u crumplezone -n 20 --no-pager            # no errors; the banner (Port, Map, Session)
```
Then have a player join with the same release. If the flags in the unit (`--map`, `--time`, ...)
need changing, edit the unit **(sudo)**, `sudo systemctl daemon-reload`, restart.

## 5. Roll back if it fails **(sudo)**
```bash
sudo install -m 755 /opt/crumplezone/crumplezone-server.old /opt/crumplezone/crumplezone-server
sudo systemctl restart crumplezone
```

## Report back
The tag built, `systemctl status` line, the journal's banner lines, and whether a player joined.
