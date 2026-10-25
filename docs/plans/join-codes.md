# Join codes: open your world to friends (task #30)

Written 2026-10-25 for branch `wip/joincodes`. Status at the bottom.

## In plain words

You are playing. You press ESC, pick **FRIENDS**, then **OPEN TO FRIENDS**. The game shows a
code such as `KX7-M4Q`, with a **COPY** button. You send the code to a friend (chat, voice, text).
Your friend picks **MULTIPLAYER → JOIN WITH CODE** on the main menu, types or pastes the code, and
drives into your world: the same map, the level you are playing, your edited sections, your parked
trucks. Neither of you needs to forward ports or know an IP address.

The world stays in **your** game. The owner's server box doesn't run your world. It runs a small,
always-on **backend** (`crumplezone-rendezvous`) that does three things:

1. **Codes.** Your game registers and gets a code. A friend's game asks "where is `KX7-M4Q`?"
2. **Introductions (hole punching).** The backend tells each game the other's public address.
   Both games send packets to each other at the same moment. Most home routers then let the
   traffic through. After that, the traffic goes **directly** between you and your friend. The box
   carries nothing more.
3. **Relay (fallback).** Some networks block that (mobile carriers, some campus or office
   networks, "symmetric" NATs). Then the traffic goes **through the box**, only between the two
   games that were introduced, and with a speed limit.

Private servers keep working as they do today: **JOIN BY ADDRESS** (`IP:PORT`), the game's own
**HOST A PRIVATE GAME**, the dedicated `crumplezone-server` with its password, and LAN discovery.

## How it works today (0.4.0, protocol 7)

* `NetSession` (`src/net.cpp`) has HOST and CLIENT roles. The host is the session authority: it
  admits guests (hello, an optional password via an HMAC challenge, a per-guest 64-bit datagram
  secret), relays everything, owns the shared clock, the level and the time of day.
* Reliable messages go over **TCP** on the game port (27015 by default). Vehicle states go over
  **UDP** on the same port number. The guest learns its UDP path by sending `DG_HELLO` datagrams.
  The host learns the guest's NAT-mapped UDP address from them.
* Reaching a friend over the internet needs a port forward of 27015 TCP+UDP on the host's
  router. TCP can't be hole-punched reliably. This is the part that's missing.
* Hosting from the menu reloads the proving ground without your saved section edits, because a
  guest would build the generator's sections and the two worlds would differ.

## The new flow

### Host: OPEN TO FRIENDS (pause menu, while playing)

1. ESC → **FRIENDS** → **OPEN TO FRIENDS**. If you aren't hosting yet, the game starts hosting
   *on the world you are in*, with no reload. It uses the game port from settings. If that port
   is taken, it tries the next nine. Then it registers with the backend from the same UDP socket,
   so the address the backend sees is the one friends will reach.
2. The panel shows the code, big, grouped `KX7-M4Q`. **COPY** puts it on the clipboard. The
   panel also shows who's connected (ping, DIRECT or RELAYED), **KICK** per player, **PASSWORD**
   on/off, and **MAX PLAYERS** 2 to 8.
3. What joiners get: the map, the level being played (`levelText`, edits and all), **the
   section edits of the proving-ground park** (new: `MSG_SECTIONS`, the `.czs` texts of the
   sections you edited, so your world is no longer reloaded without them), the time of day, every
   vehicle and line. These go in the welcome, before the guest builds its world.
4. **CLOSE TO FRIENDS** retires the code. Players already in stay until you **STOP HOSTING**.

### Codes

* Six symbols from `ABCDEFGHJKMNPQRSTVWXYZ23456789`. That is 30 symbols, with no `0 O 1 I L U`.
  Shown as `ABC-DEF`. Typing ignores case, spaces and dashes. The code box accepts no other
  character.
* 30^6 ≈ 729 million codes. With a few hundred live codes and 10 guesses a minute per address,
  guessing a live code takes years. A guessed code still meets the host's password if there is one.
* A code lives while the host's game keeps it alive (a keepalive every 10 s). It dies 45 s after
  the last one, at once on CLOSE, and after 12 hours in any case (the game then gets a new
  code). A new OPEN gets a new code, so an old shared code stops working.

### Guest: JOIN WITH CODE

Main menu → **MULTIPLAYER** → **JOIN WITH CODE** → type or paste (Ctrl+V; on a pad, up and down
change the letter in the focused box) → **JOIN**. The joining card says what is happening, in
plain words: `FINDING THE GAME`, then `CONNECTING`, then `CONNECTING THROUGH THE RELAY` if
needed, then `LOADING THE WORLD`. Errors:

| Situation | Card says |
|---|---|
| no such code, or expired | `NO GAME HAS THAT CODE. CODES STOP WORKING WHEN THE HOST CLOSES THE GAME: ASK FOR A NEW ONE` |
| host full | `THAT GAME IS FULL (8 OF 8)` |
| guest older than the host | `YOUR GAME IS OLDER THAN YOUR FRIEND'S: UPDATE TO 0.4.1 OR NEWER` |
| guest newer | `YOUR FRIEND'S GAME IS OLDER: THEY NEED TO UPDATE` |
| password | `THIS GAME NEEDS A PASSWORD` / `WRONG PASSWORD` (a password box, RETRY) |
| backend unreachable | `CAN'T REACH THE JOIN-CODE SERVER. CHECK YOUR INTERNET, OR JOIN BY ADDRESS` |
| too many tries | `TOO MANY TRIES: WAIT A MINUTE` |
| punch and relay both fail | `COULDN'T CONNECT TO YOUR FRIEND'S GAME` |
| kicked | `THE HOST REMOVED YOU FROM THE GAME` |

### When the host leaves

The host's game sends `CLOSE` on every tunnel and to the backend. Guests see
`THE HOST CLOSED THE GAME`. They keep driving alone in their copy of the world, and the other
players' vehicles disappear (as today on a lost connection). There is no host migration (owner
choice 4). A host whose game crashes stops answering: guests time out after 15 s with
`LOST THE CONNECTION TO THE HOST`, and the code dies 45 s later.

## Protocol

Everything new is **UDP**. The game's TCP path stays for direct joins (JOIN BY ADDRESS, the
dedicated server). A guest that joins by code has no TCP at all. Its reliable messages travel
over a small **reliable stream over UDP** (the "tunnel"), on the same socket and path as its
vehicle states. Above the tunnel nothing changes. `NetSession` sees a `Conn` whose bytes come
from the tunnel instead of a TCP socket. The hello, the password challenge, the datagram secret,
every check in `docs/server-security.md` and the host's authority over the session are the same
code. Anything the host sends a guest, including future host-authoritative contact messages
(`wip/netsync`), goes through the same `sendTo(Conn&)`, so it is carried on direct and relayed
paths alike.

### Backend messages (`src/rendezvous.h`, UDP port **27020**)

Header: `u16 0xC27C, u8 proto (1), u8 type`. All integers little-endian, as in `net.cpp`.

| Type | From → to | Body |
|---|---|---|
| `REGISTER` | host → rv | u16 game version, u8 players, u8 max, u8 flags (locked), u64 host nonce, local addresses (≤ 4) |
| `REGISTERED` | rv → host | u64 nonce, 6-byte code, u64 host key, the host's public ip:port, u16 keepalive s |
| `KEEP` / `KEPT` | host ↔ rv | code, host key, players, max, flags (KEPT also tells the host of pending intros) |
| `CLOSE` | host → rv | code, host key |
| `LOOKUP` | guest → rv | u16 version, code, u64 guest nonce, u64 cookie (0 the first time), locals; padded to 128 B |
| `COOKIE` | rv → guest | u64 nonce, u64 cookie (proves the guest's address is real before anyone sends to it) |
| `INTRO` | rv → guest and host | u32 pair id, u64 pair secret, u64 relay key for this side, the other side's public ip:port, its locals (only when both share a public IP) |
| `INTRO_ACK` | host → rv | code, host key, pair id (rv repeats the host's INTRO until acked, 4 tries) |
| `FAIL` | rv → guest | u64 nonce, u8 reason (NOT_FOUND, FULL, OLDER, NEWER, BUSY), u16 host version, u8 players/max |
| `RELAY` | peer → rv | u32 pair id, u64 relay key, payload |
| `RELAYED` | rv → peer | u32 pair id, payload |
| `PING` / `STATUS` | anyone → rv | health check. The counts are answered only to loopback |

### Tunnel and punch packets (game ↔ game, magic `0xC27B`)

`u16 magic, u8 type, u32 pair id, u64 pair secret`, then:

* `PUNCH` / `PUNCH_ACK`: nothing more. A packet with the wrong pair or secret is dropped and gets no answer.
* `DATA`: u32 segment number, up to 1100 bytes of the stream.
* `ACK`: u32 next expected, u64 bitmap of the 64 after it (selective ack).
* `CLOSE`: the session ended (host left, guest left, kicked).

Retransmission uses the measured round trip (RTO 3x the smoothed RTT, 0.2 to 3 s, doubling). A
small AIMD window starts at 16 segments, grows by one per round trip and halves on a loss (max
256 ≈ 280 KB in flight). An empty ACK goes out every second as a keepalive, which also keeps the
NAT mappings open. A tunnel that hears nothing for 15 s is dead. The game's vehicle-state
datagrams (`0xC27A`, with their secret) go on the same path.

### Hole punching, step by step

1. Host: REGISTER from its game socket. rv answers REGISTERED with the code and the public
   address it saw.
2. Guest: LOOKUP → COOKIE → LOOKUP with the cookie. rv makes a pair and sends INTRO to both
   (to the host at its registered address, repeated until INTRO_ACK).
3. Both sides send PUNCH every 100 ms to each of the other's candidates (public address, plus
   LAN addresses when they're behind the same public IP), for up to 2.5 s.
4. The first PUNCH or PUNCH_ACK that arrives with the right pair and secret fixes the direct path.
5. If nothing arrives in 2.5 s: the guest switches to the relay (`RELAY` to rv, which forwards
   to the host's registered address as `RELAYED`). The host answers on whichever path the guest's
   last valid packet came from.
6. The guest sends its hello through the tunnel. From here on it is the normal join.
7. If a direct path goes quiet for 4 s mid-session (a NAT forgot it), the guest moves to the
   relay, and the host follows.

### Bandwidth per session

From `tests/net_bandwidth.cpp`: a guest uploads about 15 to 20 KB/s and downloads about 15 KB/s
per other player near it. A **direct** session costs the box only keepalives: about 10 bytes a
second per open code. A **relayed** guest passes its traffic through the box both ways:

| Session | Through the box, each way |
|---|---|
| 1 host + 1 relayed guest | ≈ 20 KB/s (0.16 Mbit/s) |
| 1 host + 3 relayed guests | ≈ 3 × 50 KB/s ≈ 150 KB/s (1.2 Mbit/s) |
| 1 host + 7 relayed guests (worst) | ≈ 7 × 110 KB/s ≈ 0.8 MB/s (6 Mbit/s) |

A level transfer (up to 8 MB, once per join) goes at the relay's per-pair limit. The default is
**512 KB/s per pair**, a burst of 2 MB, and a total cap (default **40 Mbit/s**,
`--relay-mbit`). Over the cap, packets are dropped and the tunnel retransmits.

### Capacity of the box

* Codes and introductions: about 100 bytes of memory per code and 200 per pair, and
  microseconds of CPU per packet. 10 000 open codes are about 2 MB and 1 000 keepalives a second.
* Relay: one CPU core forwards well over 100 000 small packets a second. Bandwidth is the limit:
  a 100 Mbit/s uplink carries about 15 worst-case 8-player relayed sessions, or about 300 relayed
  1-to-1 pairs. Most pairs punch through (typically 80 to 90 % of home connections), so the real
  load is a fraction of that.
* It's one small process with no disk writes. It can run beside `crumplezone-server` on the
  same box.

### Security

* **Codes expire** (above). **Rate limits** per source address: REGISTER 6 a minute and at most
  4 live codes; LOOKUP 10 a minute (wrong codes included); everything else 50 packets a second.
  Global caps: 20 000 codes, 50 000 pairs, 32 pairs per code (the oldest goes).
* **No open relay.** rv forwards a `RELAY` packet only when its pair id exists, its key is the
  sender's side's key, and its source address is the address that side used with rv. It only
  ever forwards to the other side's recorded address. Nobody can name a destination. Relay
  traffic is capped per pair, per source address and in total. Pairs die with their code, or
  after 2 minutes without traffic.
* **No amplification.** Every reply is at most as big as its request: LOOKUP and REGISTER are
  padded, and PING is answered with a reply of the same size. The COOKIE round trip proves a
  guest's address before the host is told to punch towards it. A spoofed lookup makes rv send
  one small packet to the victim and nothing else.
* **Secrets.** The host key (64-bit) authorizes KEEP, CLOSE and INTRO_ACK. Each pair has a
  64-bit pair secret on every punch and tunnel packet, and per-side relay keys. All are random
  from the system's generator. The cookie is an HMAC-SHA-256 of the guest's address and the
  minute, under a key rv draws at start (nothing to configure, nothing on disk).
* **The game's own checks still apply.** Password (HMAC challenge), the per-guest datagram
  secret, every limit and validation in `docs/server-security.md`. The host's game accepts a
  tunnel only for a pair it was introduced to. Its per-address limits and wrong-password lockout
  use the guest's public address as rv saw it.
* **What rv learns.** The codes, both sides' public addresses (and LAN addresses only between
  games behind the same public IP), and the encrypted-nothing game traffic of relayed pairs.
  Traffic is not encrypted (as today, L1 in the security review). rv logs counts, never codes
  or addresses at the default log level.
* **DoS.** A flood at rv can't make it send more than it receives. It doesn't grow memory past
  the caps, and it logs at most 2 lines a second. Volumetric floods belong to the firewall
  (`docs/server-rendezvous-setup.md` has a rate-limit rule).

### Compatibility

`net::kVersion` 7 → **8** (the tunnel, `MSG_SECTIONS`, a link byte in `MSG_RTT`, REJECT after
the join for kicks), `kRelease` "0.4.1". A 0.4.0 game joining a 0.4.1 host by address is told to
update. Codes check the version at LOOKUP, before any connection. The backend speaks its own
`proto 1` and refuses other protos with FAIL(OLDER or NEWER), so a newer game gets a clear
message from an older backend. `wip/netsync` (the collision desync work) has not bumped yet. If
it lands a bump first, this branch takes the next number. Both must reach 0.4.1 with one
consistent number.

## Multiplayer UI (main-menu style, keyboard + mouse + pad)

Pure navigation state in `src/mpmenu.{h,cpp}` (tested, no GL), painted by `src/mpmenudraw.cpp`
with the main menu's look: the dark shade on the left, amber focus bar, big labels, a panel on
the right, the hint line at the bottom. The pad drives it through the same key events (A = Enter,
B = Esc, D-pad = arrows).

```
MAIN MENU → MULTIPLAYER                         (P_HOME)
+---------------------------------------------+------------------------------------+
| MULTIPLAYER                                 |                                    |
|                                             |  JOIN WITH CODE                    |
| > JOIN WITH CODE        A FRIEND'S 6-LETTER |  YOUR FRIEND OPENS THEIR GAME TO   |
|   JOIN BY ADDRESS       IP:PORT OF A SERVER |  FRIENDS (ESC > FRIENDS) AND READS |
|   ON YOUR NETWORK       2 GAMES FOUND       |  YOU THE CODE.                     |
|   HOST A PRIVATE GAME   PORT, PASSWORD      |                                    |
|   YOUR NAME  [BREAD_____]                   |  RECENT: KX7-M4Q (BOB, 2 H AGO)    |
|   RECENT    KX7-M4Q  /  10.0.0.5:27015      |                                    |
|   BACK                                      |                                    |
+---------------------------------------------+------------------------------------+
 W S MOVE   ENTER OK   ESC BACK

JOIN WITH CODE                                  (P_CODE)
   [ K ][ X ][ 7 ] - [ M ][ 4 ][ Q ]       typing fills the boxes; Ctrl+V pastes
   > JOIN                                  pad: left/right box, up/down letter
     PASTE
     BACK
   "CODES HAVE LETTERS AND DIGITS, NO 0 O 1 I L U"

JOIN BY ADDRESS                                 (P_ADDRESS)
   ADDRESS   [203.0.113.5:27015_]
   PASSWORD  [********]   (IF THE GAME HAS ONE)
   > JOIN
     BACK

ON YOUR NETWORK                                 (P_LAN)
   > BOB'S GAME       BOB  PROVING GROUND  2/8
     SERVER           MOUNTAIN  LOCKED  1/8
     BACK

HOST A PRIVATE GAME                             (P_HOST)  (people join by your address, or code)
   MAP          < PROVING GROUND >
   GAME NAME    [BREAD'S GAME]
   PASSWORD     [****]
   MAX PLAYERS  < 8 >
   PORT         [27015]
   ALSO GIVE A JOIN CODE  < ON >
   > START HOSTING
     BACK

JOINING                                         (P_JOINING)
   JOINING KX7-M4Q
   (spinner) CONNECTING THROUGH THE RELAY...
   > CANCEL
 -- on failure --
   COULDN'T JOIN
   THAT GAME IS FULL (8 OF 8)
   PASSWORD [_____]        (only for password errors)
   > RETRY
     BACK

IN GAME: ESC → FRIENDS                          (P_FRIENDS, over the game)
+---------------------------------------------+------------------------------------+
| FRIENDS                                     |  CODE                              |
| > OPEN TO FRIENDS     GET A CODE            |   KX7-M4Q          [COPY]          |
|   PASSWORD     < OFF >                      |  SEND IT TO A FRIEND: THEY PICK    |
|   MAX PLAYERS  < 8 >                        |  MULTIPLAYER > JOIN WITH CODE      |
|   PLAYERS                                   |                                    |
|     BREAD (YOU)  HOST                       |  ON YOUR NETWORK, OR BY ADDRESS:   |
|     BOB     42 MS  DIRECT     [KICK]        |  192.168.8.112:27015               |
|     ANN    120 MS  RELAYED    [KICK]        |                                    |
|   CLOSE TO FRIENDS / STOP HOSTING           |                                    |
|   BACK                                      |                                    |
+---------------------------------------------+------------------------------------+
  (a guest sees PLAYERS with pings and LEAVE THE GAME)
```

Name entry: the YOUR NAME field on P_HOME. The first time you open MULTIPLAYER with no name set
(the default is your login), the cursor starts in it. Names are `te::Kind::Name`, 16 characters.

The classic PauseMenu page 2 (HOST/JOIN cards) stays reachable from the DEV page's CLASSIC MENU.
The main menu's MULTIPLAYER button and the pause menu's MULTIPLAYER row open the new pages.

## Building it: phases (each its own commit on `wip/joincodes`, suite green)

1. **Backend.** `src/rendezvous.{h,cpp}` (protocol, the service as a poll-driven class, code
   alphabet) and `tools/rendezvous_main.cpp` → `build/crumplezone-rendezvous`. Tests
   `net/rv-*`: register, lookup, cookie, expiry, rate limits, relay forwarding, no forwarding
   for wrong key, source or pair, reply sizes ≤ request sizes.
   *Why a separate binary and not `crumplezone-server --rendezvous`:* it needs none of the game.
   It's about 1/50 of the code (no physics, worlds or vehicle catalog), so the attack surface is
   that much smaller. It can be restarted or updated without kicking players off the dedicated
   server, gets its own systemd unit with tighter hardening (no files at all), and builds on the
   box in seconds. The two share `net.cpp`'s HMAC and socket helpers through `src/netsock.h`.
2. **Client in NetSession.** Socket shim (`src/netsock.h`, plus a test hook for UDP), `Path`,
   the tunnel, `openToFriends()`, `beginJoinCode()`, punch, relay, kick, link type, version 8.
   Tests `net/code-*` over loopback: direct punch, a simulated NAT (address-restricted cone →
   punch works) and a symmetric NAT (→ relay), a forced relay with two guests, host leaves,
   expired code, full, wrong version, password over a code, kick.
3. **World edits to joiners.** `MSG_SECTIONS`, `World::sessionSections`, no more reload on
   hosting. Test: an edited section on the host arrives and is built on the guest.
4. **UI.** `mpmenu` + painter, main.cpp wiring (main menu MULTIPLAYER, pause FRIENDS), copy and
   paste, settings (`rendezvous`, recent). Tests `ui/mpmenu-*` on the navigation.
5. **Box docs.** `docs/server-rendezvous-setup.md`; `crumplezone-server --code` (a dedicated
   server can also take a code); README/multiplayer docs.

## Owner choices (defaults used until told otherwise)

1. **The backend's address**: the game needs the box's DNS name or IP. The build default is
   `CZ_RENDEZVOUS` at compile time, else the `rendezvous HOST:PORT` line in settings.txt, else
   `--rendezvous` on the command line. With none of them set, the code pages say that join codes
   aren't set up in this build. Pick a hostname (e.g. `play.<your-domain>`) and it goes into
   `net::kDefaultRendezvous`.
2. **Port 27020/UDP** for the backend.
3. **Relay limits**: 512 KB/s per pair, 40 Mbit/s in total. Raise or lower these to suit the
   box's uplink and data cap.
4. **No host migration** when the host leaves: guests keep driving alone. Migration would need
   the world state handed over, which is a later project.
5. **Code length 6** (`ABC-DEF`). 5 is shorter but leaves much less margin against guessing.
6. **Codes for the dedicated server** (`crumplezone-server --code`): included, off by default.
7. **LAN addresses shared** only between games behind the same public IP.

## Status

See the commits on `wip/joincodes` and the report at the end of the work.
