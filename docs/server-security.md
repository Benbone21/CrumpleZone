# Multiplayer server security

Review of the multiplayer host (`src/net.cpp`, the game's built-in host and the dedicated
`crumplezone-server`) for the case where it sits on a public IP and anybody can reach it.
Written 2026-10-16. `file:line` pointers are into commit `81940ed` (the code as reviewed);
each finding says what now guards it. Protocol version 4 -> **5**: older builds refuse to
join a new host (and the reverse) with "different game version".

## What is exposed

| What | Port | Notes |
|---|---|---|
| Reliable messages (hello, spawns, lines, chatter, level) | **TCP 27015** (or the `--port` you chose) | on every interface, or one with `--bind IP` |
| Vehicle states | **UDP 27015** (same) | same port number, same interfaces |
| LAN discovery (answers "who's hosting?") and announcements | UDP 27016 / 27017 | **off** on the dedicated server unless `--lan`; on in the game's own host, answered only to the local network |

IPv4 only. Nothing is encrypted. A probe that connects and says nothing gets nothing back
(the host speaks only after a valid hello), and is dropped after 8 s. A probe that speaks the
protocol learns its version and whether the game is full, nothing else (no map, no names,
no player count) unless it joins.

## Threat model

The attacker can send any bytes to the TCP and UDP port from any address (scanners, bots,
other players' clients modified to misbehave). They cannot read other players' traffic
(otherwise see "what remains"). Goals considered: crash or stall the host or the other
players' games, take over or end the session, impersonate or kick other players, cheat the
shared state (cash, jobs, races), exhaust CPU, memory, file descriptors or bandwidth, use the
host to attack a third party (reflection), learn about the machine, write to its disk.

**Files:** nothing from the network is ever written to disk or used as a file name. Levels
sent by a host live in memory only (`levelText`); the dedicated server has no career,
settings or discovered-places files and never writes. Names and chat reach only the HUD (and
the server's log, after `net::sanitize`). One indirect write in the game (not the server): a
buddy's "cache MAP:ID" chatter for one of the map's own supply caches opens it for you and
saves your career file, at its fixed path (L5). **Format strings:** no network string is ever
a format string (`printf("%s", ...)`, `std::string` building); names are cleaned before they
reach the server's log, so a name can't carry terminal escape sequences.

## Findings

Severity is for a host exposed to the internet with no firewall other than the port forward.
"Fixed" means fixed in this branch and covered by a test in `tests/net_security.cpp` (`net/sec-*`).

### Critical

| # | Finding | Attack | Where (81940ed) | Fixed by |
|---|---|---|---|---|
| C1 | Any connection can send the host's own messages. `MSG_REJECT` sets `role = OFF`; `MSG_WELCOME` rewrites the host's `self` and `map`. | One 5-byte message from any scanner that opens the port ends the session, or renumbers the host so car ids stop matching. No hello, no login. | `net.cpp:552-562` (no role or state check in `handle`) | `handle` is split into the host's view of a guest and a guest's view of the host. A guest may send only hello/auth, ping/pong, chatter and its own vehicles and lines; any other type drops it. The guest side takes welcome/reject only while joining. |
| C2 | Nothing needs a hello. Every world message from any connection is applied and **relayed unvalidated** to every player. | Unauthenticated: despawn or kick other players (`LEFT`, `DESPAWN`), spawn or replace vehicles in their names, hook lines, flood. | `net.cpp:521-620`, relay at `617` | The host ignores everything but hello/auth until a guest is let in. Relayed messages are first applied by the host itself (`apply(..., from)`), which refuses anything not a valid vehicle or line owned by that guest; only accepted messages are passed on. |
| C3 | Rope message: end kind, hook index and car are used unchecked. A hook of 2..255 indexes `hooks[2]` out of bounds; an end `a` that is not a vehicle indexes `cars[-1]`. Relayed, so it crashes **every** machine. NaN or 0 in `stallN`, `k`, ... wrecks the physics. | One message from any connection crashes the host and every client. | `net.cpp:764-788` using `recovery.cpp:11` and `recovery.cpp:701-707` | `apply` MSG_ROPE: kinds and hooks in range, `a` must be a non-prop vehicle of the sender, the other end a vehicle or a finite point in the world, every number finite and in a sane range, one line per id, at most 6 per player and 64 in all. |

### High

| # | Finding | Attack | Where | Fixed by |
|---|---|---|---|---|
| H1 | No authentication at all: no password, key or allowlist. | Anyone who finds the port plays, griefs, or fills the 7 slots. | `net.cpp:526-551` | `--password` (server) / `--password` or the PASSWORD field (game): the host sends a random challenge, the guest answers HMAC-SHA-256(password, challenge); the password never crosses the wire; compared in constant time. Five wrong answers from an address lock it out for a minute. Optional (LAN play unchanged), strongly recommended. |
| H2 | Unbounded connections, no timeouts: no cap overall or per address, a connection may stay silent forever, input buffers and the queue to a slow reader grow without limit. | Open a few hundred sockets (file descriptors run out, real players can't connect), or hold slots with half-open sessions (slowloris). | `net.cpp:816-829`, `495-519`, `476-479` | 24 connections in all, 8 per address (a full host drops its oldest silent unjoined connection to make room); no hello (and password) within 8 s, or silence for 30 s, drops; message length capped (256 B before hello, 1 KB after), messages and bytes per second limited per guest; output queue capped at 16 MB; at most 128 KB read per guest per frame. |
| H3 | The dedicated server never adopts its sim and never drains what it collects: every world message from any client is appended to `pending` (never freed), `gameIn` and `events` grow forever. Also functionally broken: nothing a guest spawns is kept, so players who join later don't see earlier players. | Memory exhaustion by a stream of messages, a few KB/s for days or a burst in seconds. | `server_main.cpp:88-117`, `net.cpp:619`, `net.h` (`gameIn`) | The server adopts its sim (so state is kept and replayed to later joiners), clears `gameIn`/`events`/`log` every loop; `pending`, `gameIn` and `events` are capped in `net.cpp`. |
| H4 | Vehicle states are decoded with only some floats checked. Steering, damage, wheel angle/omega are unchecked; `gear` (a signed byte) indexes the gear table; positions of any size are accepted. | NaN or 1e30 poisons the physics of everyone near the car (or all of them: the host relays it), a huge position breaks the spatial grid, a gear of 100 reads out of bounds. | `net.cpp:1051-1117`, `softbody.cpp:147` | `takeSnapshot` rejects the whole state unless every number is finite and plausible (position within 50 km, speed under 1000 m/s, ...), normalizes the rotation, clamps what is clamped, and limits `gear` to the spec's table. Fragments per state limited to 32. |
| H5 | Datagrams are trusted: any packet with the magic and a player id is accepted and **relayed**; a hello re-points a guest's stream to the sender's address; the hello is echoed back whole. | Forge another player's car; hijack their incoming stream by sending a hello with their id; use the host to bounce traffic at a spoofed address; amplify the host's bandwidth by one datagram to N guests. | `net.cpp:985-1020` | Every datagram carries the 64-bit secret the host told that guest in its (TCP) welcome, and the host stamps each receiver's own on the copy it relays. Wrong or missing secret: dropped, never answered. The reply to a hello is the 22-byte header only. Per-guest datagram and byte limits. Snapshots must name a vehicle the host knows to be that guest's. |
| H6 | No ownership: any vehicle id, line id and player id may be claimed by anyone, and a state's rope lengths set **any** line. | Replace or delete another player's truck for everyone (`SPAWN`/`DESPAWN`/`LAMPS` with their id); pay out someone else's winch; spoof the host's id so clients ignore the real host. | `net.cpp:709-763`, `1193-1195` | The owner (top 16 bits of a vehicle or line id) must be the sending guest, and never the receiver itself. Rope lengths in a state only touch the winch lines that vehicle works, clamped. At most 6 vehicles per guest. |
| H7 | Host-only race chatter is relayed from any guest: `jobdone` pays **every** player cash (`career.jobDone()`), `job\|...` replaces the job, `restart` resets everyone's race. | One chat packet: free money for the lobby, or no one can finish a race. | `main.cpp:3218-3271`, `3240-3248` | Chatter beginning `job` or `restart` is dropped by the host when it comes from a guest, and by a client when it claims to be from anyone but player 0. |
| H8 | `Job::parse` trusts the count field: `n*3` overflows `int`, then `f[11+k*3]` runs far outside the vector; the kind indexes tables. Fed by chatter relayed to all clients. | One crafted `job\|...` message (before H7) crashes every client. | `jobs.cpp:48-70` | Kind and count checked (0..32), strings and positions bounded and finite. Also covered by the fuzzer. |
| H9 | LAN discovery on a public server: the responder on UDP 27016 answers any "CZFIND" from anywhere with a longer message (about 10x) and the host announces to broadcast addresses. | Reflection and amplification against a spoofed victim; fingerprinting of the server (map, player count, host name). | `net.cpp:325-339`, `1361-1390` | The dedicated server does not open it unless `--lan`. In the game it answers only sources on one of this machine's own networks (and loopback), at most 20 a second. |
| H10 | The relay of reliable messages is itself broken: a message from one guest is passed to the others **without its length prefix**, which corrupts their stream. Never noticed with a single guest. | (Not an attack: with two or more guests, the first spawn or line corrupts the others' connections; an attacker triggers it at will.) | `net.cpp:617` | `broadcast` is given the framed message. Test `net/sec-late-join`. |

### Medium

| # | Finding | Attack | Where | Fixed by |
|---|---|---|---|---|
| M1 | `SpecId` mods are not checked against the garage options, and `tunedSpec` keeps a copy of a spec for every distinct combination ever asked. | 256^5 different spawns: unbounded memory and CPU on every machine, from one guest. | `net.cpp:177-184`, `mods.cpp:708-730` | `specFrom` requires each pick to be a real option of that model; spawns limited to 3 a second (burst 12) per guest. |
| M2 | Unbounded vehicles and lines per player: one guest can fill the world's 48 body slots. | Nobody else can spawn; CPU cost for all. | `net.cpp:723` | 6 vehicles and 6 lines per guest, 64 lines in all, rate limits on spawns and lines. |
| M3 | Level text from the host accumulates without limit and without checking order; a client trusts `self` in the welcome as an array index (`players[self]`). | A hostile host fills a client's memory, or crashes it (index 255). | `net.cpp:567-580`, `552-556`, `467` | Level: at most 8 MB in at most 200 parts, in order, whole or not at all (a host also refuses to send more). Welcome: id 1..7, a known map, only once. |
| M4 | Names, session names, chat and discovery announcements are not cleaned; duplicates allowed. | Control characters and DEL (drawn as a solid block) in the HUD; terminal escape sequences in a server's log; two players called the same thing (impersonation). | `net.cpp:529`, `549`, `1455-1489` | `net::sanitize`: printable ASCII only, 16 characters for names, 255 for chatter; a second "BOB" is "BOB2". |
| M5 | Unchecked pong time and ping values are shown and relayed. | NaN pings in everyone's player list. | `net.cpp:600-611` | Clamped to 0..10 s, non-finite becomes 0. |
| M6 | `fuel` chatter from any player adds any amount (negative, NaN) to your tank. | Drain or break a player's fuel from a distance. | `main.cpp:3249-3258` | 0..60 L only. |
| M7 | Unbounded `gameIn`, `events`, `pending`, discovery results. | Memory growth from one flooding peer. | `net.h`, `net.cpp:619`, `1481-1489` | Capped (256, 64, 8192 messages / 32 MB, 64 rows). |
| M8 | Dedicated server runs as whatever user starts it, `root` included. | A bug anywhere above is a root compromise. | `server_main.cpp` | Refuses to start as root unless `--allow-root`; docs give a systemd unit with `DynamicUser`. |

### Low / accepted

| # | Finding | Notes |
|---|---|---|
| L1 | **No encryption.** Everything after the password check is plaintext and unauthenticated: a person on the path can read it, and hijack or inject into a session. | Design limit. For anything that matters, put the server behind a VPN (WireGuard, Tailscale) and listen only on the VPN address (`--bind`). |
| L2 | A short or guessable password can be brute-forced online (limited: 5 tries per minute per address) and offline by someone who recorded a handshake. | Use a long random password (`--password-file`). |
| L3 | The lock-out and the per-address limits are per IPv4 address; people behind one NAT share them (8 connections). | Raise `kMaxConnsPerIp` in `net.h` for a LAN party behind one address. |
| L4 | A hostile **host** can send a level with very many objects, or lie in any way a host may (race results, weather). | You choose whom to join. Levels are bounded in size, not in work. |
| L5 | Race results (`prog`, `fin`, `out`) and `cache` chatter are taken at their word. | Cooperative game. A guest can claim a finish; it can't change anyone else's state. |
| L6 | A peer on the path can read the 64-bit datagram secret (it is sent over unencrypted TCP) and forge states for that guest. | See L1. |
| L7 | Volumetric floods (bandwidth, SYN floods beyond the kernel's defaults) are not something the process can fix. | Use the host's firewall rate limits (docs/multiplayer.md has an `nft`/`iptables` example) or your provider's protection. |
| L8 | IPv6 is not supported (the sockets are IPv4); `--bind` takes an IPv4 address. | |

## What the code now does with a message

* TCP: at most 128 KB read per guest per frame; framing checked (length 1..256 before the
  hello, 1..1024 after); 150 messages and 64 KB per second per guest (burst 300 / 256 KB);
  anything unknown, out of order or too big drops the guest (it keeps its socket half a
  second to read why). Dropped guests are logged (`[net] ...` lines, rate limited).
* UDP: header size and magic, secret, sender known and in the session, vehicle theirs and
  known, fragment counts (at most 32 pieces, full-size except the last), 300 datagrams and
  600 KB a second per guest.
* Everything decoded goes through `okNum`/`okPos` (finite, within 50 km) and an index check
  against the receiver's own tables (`vehicleCatalog()`, `modOptions`, `sim.cars`, the spec's
  gear table) before it touches the sim.

## Tests

`tests/net_security.cpp` (all `net/sec-*`):

* `net/sec-hmac`: RFC 4231 vectors; text cleaning.
* `net/sec-password`: wrong password refused, right one let in, none given refused with a
  reason, a host with no password lets anyone in, five wrong passwords lock the address out.
* `net/sec-flood`: 100 connections from one address (4 kept), many addresses (total capped),
  silent connections dropped by the handshake timeout, a real player still joins, an idle
  player is dropped.
* `net/sec-host-only`: eleven host-only or out-of-order messages each drop the guest and
  leave the session intact; garbage before the hello; huge frames; other versions; names
  and chat cleaned; host-only chatter not relayed; forged vehicle ids refused.
* `net/sec-udp`: scans, forged hellos and forged states with no or a wrong secret get no
  answer and move nothing.
* `net/sec-level`: an oversized level is refused by the sender and the receiver; parts
  must arrive whole and in order.
* `net/sec-late-join`: a server with no truck of its own keeps what guests spawn and tells
  later joiners; two guests see each other (the relay-framing bug).
* `net/sec-fuzz`: 6000 mangled messages at a host, 6000 at a client, 4000 states and
  fragments, 6000 datagrams, 3000 level texts, 4000 job messages (random bytes, truncation,
  NaN/inf, extreme integers, bad indices, repeated chunks). Afterwards the physics is stepped
  and every body must still be finite, and the sim's vehicle and line counts bounded.

Under `-fsanitize=address,undefined` (a scratch copy of the tree, so `./build` isn't touched):

```bash
mkdir /tmp/czasan && cp -r src tests tools Makefile /tmp/czasan/ && ln -s $PWD/assets $PWD/third_party /tmp/czasan/
cd /tmp/czasan && CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined' make -j4 build/run_tests
ASAN_OPTIONS=detect_leaks=0 ./build/run_tests --jobs 2 net/sec
```

All `net/sec-*` scenarios, the fuzzer included, run with no ASan or UBSan report. To check the
fuzzer can see what it is for, the rope hook-index check was removed in that scratch copy:
UBSan stopped the run in `Car::hookPoint` (`hooks[e]`, from `Sim::ropeForces`) within the first
structured iterations. (Byte-flipping alone did not find it; the field-by-field "structured"
part of the fuzzer, which builds whole valid messages with one odd field and then steps the
physics, did.)

## What remains

* **No encryption or authentication of the game traffic** (L1/L6): the password proves who
  may join, not who is on the wire. Over the internet and for anything private, run the
  server behind WireGuard or Tailscale.
* **Volumetric DoS** (L7) belongs to the firewall.
* The server trusts what a *client* machine computes about its own car (it is the physics
  authority for it): a modified client can drive impossibly, ignore damage or claim race
  results. The host only guarantees that such a car cannot crash, poison or impersonate
  anyone else.
* The physics itself is not hardened against extreme but finite, plausible values
  (a state 49 km out at 900 m/s is accepted). The fuzzer steps the physics after such
  inputs; if that ever finds a hang, tighten `kPosLimit` and the speed limit in `net.cpp`.
