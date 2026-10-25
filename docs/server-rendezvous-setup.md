# Setting up the join-code backend (instructions for an agent on the server machine)

Players host their worlds in their own games and give friends a **join code**. Codes only work
if this machine runs the small backend that hands them out: **`crumplezone-rendezvous`**. It
does three things (`docs/plans/join-codes.md`):

1. Codes: a host's game registers and gets one.
2. Introductions: it tells the two games each other's address, so they can punch through their
   home routers and talk directly.
3. Relay: when that fails, it passes their packets along, only between those two games, with a
   speed limit.

It holds no world and writes nothing to disk. It needs no secrets: its keys are random and drawn
when it starts, in memory. It is a separate program from the dedicated server
(`crumplezone-server`, `docs/server-update.md`). That server keeps working as it does now for
private games (join by address, with its password). The two run side by side.

Assumed setup (change it if this machine differs; check with `systemctl cat crumplezone`):

| What | Where |
|---|---|
| Source checkout | `~/crumplezone-src` (the same one `docs/server-update.md` uses) |
| Binary | `/opt/crumplezone/crumplezone-rendezvous` |
| Unit | `crumplezone-rendezvous.service` |
| Port | **UDP 28015** (`--port`) |
| The dedicated server (unchanged) | `/opt/crumplezone/crumplezone-server`, `crumplezone.service`, TCP+UDP 27015 |

## Rules for the agent

- Steps marked **(sudo)** need the user: show them the exact command and let them run it.
  Don't enter passwords yourself.
- Don't change the dedicated server's unit, its password, the SSH rule or other firewall rules
  unless asked. Only add what's below.
- Never print or copy `/etc/crumplezone/password`. This backend doesn't use it.
- Keep the old binary until the new one is confirmed working (step 7).

## 1. Get the source

```bash
# first time only (no sudo; any directory the user owns):
git clone https://github.com/Benbone21/CrumpleZone.git ~/crumplezone-src
# every install or update:
cd ~/crumplezone-src && git fetch --tags && git checkout vX.Y.Z   # the release tag, e.g. v0.4.1
```

The backend first ships in 0.4.1. Before that release exists, check out the branch or commit the
owner names (`git checkout wip/joincodes`). Players' games must be the release the backend
speaks: the backend checks each game's version and tells the player to update if it doesn't
match.

Build tools, if missing **(sudo)**: `sudo apt install build-essential git` (Debian/Ubuntu).

## 2. Build it and try it, no root

```bash
make -j"$(nproc)" build/crumplezone-rendezvous build/run_tests
./build/crumplezone-rendezvous --help | head -3
# a spare port, for a moment:
./build/crumplezone-rendezvous --port 28777 & RV=$!
sleep 0.5
./build/crumplezone-rendezvous --selftest 127.0.0.1:28777   # registers a code, looks it up, relays both ways, closes it
CZ_TEST_RENDEZVOUS=127.0.0.1:28777 timeout 120 ./build/run_tests --jobs 1 net/code-live -v
kill $RV
```

- `--selftest` must end with `selftest OK: ...`.
- `net/code-live` must PASS. It is a real join over loopback, through the backend: a host
  opens to friends, one guest punches straight through, and another is made to use the relay.
- `build/crumplezone-rendezvous` needs no SDL, GPU or game assets. It builds in seconds.
  `build/run_tests` builds the whole game's tests and takes a few minutes. It's only needed for
  the join test.
- `-march=native` is the default, so build on this machine (or pass `CXXFLAGS=-O2`).

## 3. Install **(sudo)**

```bash
sudo install -d /opt/crumplezone
[ -f /opt/crumplezone/crumplezone-rendezvous ] && sudo cp /opt/crumplezone/crumplezone-rendezvous /opt/crumplezone/crumplezone-rendezvous.old
sudo install -m 755 build/crumplezone-rendezvous /opt/crumplezone/crumplezone-rendezvous
```

`/etc/systemd/system/crumplezone-rendezvous.service`:

```ini
[Unit]
Description=Crumple Zone join-code backend (codes, hole punching, relay)
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/opt/crumplezone/crumplezone-rendezvous --port 28015 --relay-mbit 40 --pair-kbps 512
Restart=on-failure
RestartSec=3

# A throwaway user, no privileges, and no files at all
DynamicUser=yes
NoNewPrivileges=yes
CapabilityBoundingSet=
AmbientCapabilities=
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
PrivateUsers=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
ProtectProc=invisible
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
RestrictAddressFamilies=AF_INET
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
MemoryMax=256M
TasksMax=8

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now crumplezone-rendezvous
```

## 4. Firewall **(sudo)**

Only **UDP 28015** comes in for the backend. Players' own games need nothing on this machine.
Keep the existing rules: SSH, and the dedicated server's 27015 TCP+UDP.

```bash
# ufw
sudo ufw allow 28015/udp
```

```nft
# nftables: inside the existing `chain input` (policy drop), next to the game port's rules.
# The backend rate-limits per address itself; this caps a flood before it reaches the process
# (one relayed game is a few hundred packets a second at most).
udp dport 28015 meter rv_flood { ip saddr limit rate over 3000/second burst 6000 packets } drop
udp dport 28015 accept
```

If the provider has its own firewall (a cloud security group), open UDP 28015 there too.

## 5. Settings (flags in `ExecStart`)

No secret values are involved. Change these only if the owner asks. To apply a change:
edit the unit **(sudo)**, then `sudo systemctl daemon-reload && sudo systemctl restart crumplezone-rendezvous`.

| Flag | Default | What |
|---|---|---|
| `--port N` | 28015 | the UDP port. If you change it, the games need `HOST:N` (step 8) |
| `--bind IP` | all | listen on one IPv4 address only |
| `--relay-mbit N` | 40 | all relayed traffic together, Mbit/s. Size it to the uplink and data cap; `0` turns the relay off (punch-only) |
| `--pair-kbps N` | 512 | one relayed pair (a guest and its host), KB/s |
| `--max-codes N` | 20000 | open codes at once |
| `--codes-per-ip N` | 4 | open codes from one address (a household hosting several games) |
| `--lookups-per-min N` | 10 | code lookups from one address a minute (guessing codes) |
| `--quiet` | off | no once-a-minute counts line in the journal |

What it costs: a relayed 2-player game is about 0.2 Mbit/s each way, and a relayed 8-player
game up to about 6. Most games punch through and cost almost nothing. Codes and pairs take a
few hundred bytes each.

Restarting drops the codes: hosts' games notice within about 35 s and get a new code (their
players already in stay connected if they punched through; relayed ones drop).

## 6. Confirm

```bash
systemctl status crumplezone-rendezvous --no-pager | head -5     # active (running)
journalctl -u crumplezone-rendezvous -n 20 --no-pager             # the banner: "crumplezone-rendezvous: UDP 0.0.0.0:28015, protocol 1, ..."
/opt/crumplezone/crumplezone-rendezvous --check 127.0.0.1         # ok 127.0.0.1:28015: codes N, pairs N, ...
/opt/crumplezone/crumplezone-rendezvous --selftest 127.0.0.1      # selftest OK: ...
cd ~/crumplezone-src && CZ_TEST_RENDEZVOUS=127.0.0.1:28015 timeout 120 ./build/run_tests --jobs 1 net/code-live -v   # PASS
```

`--check` shows the counts only to this machine (loopback). From anywhere else it answers
with zeros, which still proves the port is reachable. If the user has another machine handy,
run `crumplezone-rendezvous --selftest PUBLIC_IP` from there. That proves the firewall lets
UDP 28015 in.

## 7. The dedicated server keeps working

Leave `crumplezone.service` as it is. Private servers still use **JOIN BY ADDRESS** with the
password, as before. If the owner wants the dedicated server reachable by code too (owner's
choice, off by default), add `--code 127.0.0.1:28015` to its `ExecStart` **(sudo)**. Its journal
then shows a line `OPEN TO FRIENDS: CODE ABC-DEF` after each start, and players can type that
code. A new code is issued on every restart.

## 8. Point the games at it

The game has to know this machine's address. The owner chooses one of these:

- **In the release** (normal): in `src/net.h`, change `#define CZ_RENDEZVOUS ""` to the DNS name
  or IP of this machine, e.g. `#define CZ_RENDEZVOUS "play.example.org"` (add `:PORT` if not
  28015), commit, and release.
- **Per player**, for testing: the line `rendezvous play.example.org` in the game's
  `settings.txt`, or `crumplezone --rendezvous play.example.org`.

Until one of these is set, the game's JOIN WITH CODE and OPEN TO FRIENDS say join codes aren't
set up, and everything else works as before.

## 9. Logs

```bash
journalctl -u crumplezone-rendezvous -f
```

- The banner at start: the port, the protocol, the relay limits.
- Once a minute: `codes N, pairs N, relayed N KB (N packets), dropped N, refused N, lookups N, registrations N`.
- Refusals, at most 2 lines a second: `refused a registration from IP: too many codes`,
  `too many lookups from IP`. A few are normal. A steady stream means somebody is guessing
  codes or flooding. The limits already handle it; tighten the firewall rule if it's heavy.
- Codes are never logged.

## 10. Roll back **(sudo)**

```bash
sudo install -m 755 /opt/crumplezone/crumplezone-rendezvous.old /opt/crumplezone/crumplezone-rendezvous
sudo systemctl restart crumplezone-rendezvous
```

To take it away entirely: `sudo systemctl disable --now crumplezone-rendezvous` and remove the
UDP 28015 firewall rule. Games then say join codes can't be reached, and joining by address
still works.

## Report back

The commit or tag built, the `systemctl status` line, the journal's banner, the outputs of
`--check` and `--selftest`, the `net/code-live` result, the firewall rule added, and the address
(DNS name or IP and port) the games should use.
