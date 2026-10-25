# Multiplayer motion, sound and lights (2026-10-13)

The previous implementation anchored remote playback to the arrival of each newest
snapshot. Variable network delay therefore changed the playback time abruptly, producing
backward jumps and variable apparent speed. Engine RPM/load/boost and reverse gear were
not transmitted. The renderer also forced every remote headlight switch to AUTO, and
the audio mixer only synthesized the selected player's engine.

## Changes

- Remote presentation follows a monotonic clock with slowly corrected sender/local clock
  offset. The existing 100 ms interpolation buffer is retained. Steering, braking, wheel
  spin, RPM/load/boost and damage interpolate between snapshots. Brief packet gaps coast
  for at most 250 ms; prolonged gaps stop the remote body's velocity.
- Three bounded fragment assemblies per car let an older snapshot finish while a newer
  one is only partially received. Fragment order and overlapping packet assembly are
  tested. Snapshot build counters reject queued states from before a car was rebuilt.
- Discontinuous recovery/repair moves clear the interpolation history and jump to the
  new pose instead of sweeping through intervening geometry or generating huge velocities.
  Initial spawns now send the vehicle origin instead of passing its center of mass to
  `Car::build`; this prevents the extra height followed by a drop on the first snapshot.
- Headlight mode is a reliable TCP message, independent of the UDP interpolation buffer
  and sleeping-car update rate. The current mode is included in spawn messages for late
  joiners. OFF/LOW/HIGH/AUTO are used directly by the renderer. Brake state and reverse
  gear arrive in the periodic snapshots.
- Snapshot RPM, effective engine load and boost drive the same engine synthesizer used
  for the local car. Up to eight nearby engines mix with distance attenuation, stereo
  positioning and fades on departure. This includes remote vehicles and nearby local
  AI cars. Voices keep their identity as distance ordering changes.
- Engine synthesis is shared in `src/engineaudio.*`; voice constants and filter coefficients
  are cached. The audio callback consumes a fixed-size triple buffer, with no locks or
  allocations for nearby-engine updates. Existing tires, impacts, wind and music remain.

The wire protocol is now **3**. **Both computers must run this updated build.** Older
protocol-2 releases are rejected at the join handshake and shown as incompatible in
discovery; they cannot decode the new spawn/snapshot fields.

This improves snapshot presentation and missing shared state. Each player still owns
their own vehicle simulation, and cross-player collision forces are not a new centralized
authoritative physics system. A live two-PC test is still needed to assess collision
feel and audio balance on actual speakers.

## Verification

Production snapshot encoding/decoding, reliable light messages, fragment assembly and
remote playback are exercised without sockets by `tests/net_state.cpp`. All three
scenarios passed. With simulated variable delay (10–80 ms) and reordered packets,
a car at 10 m/s had no backward jumps; its largest 120 Hz presentation step was 0.09 m,
and the wheel animation's largest step was 0.17 rad. The tests also check late joins,
reverse gear, RPM/load/boost, light changes, stale states after rebuilds, truncation,
stopping after packet loss and recovery teleports.

Offline audio checks passed audibility, left/right pan, distance attenuation, departure
fades, bounded finite samples, all seven engine voices, concurrent snapshot publication,
and SDL's callback startup/shutdown using the silent dummy driver. No speakers or visible
window were used. The Windows game, scenario runner and audio test cross-compile locally.

```bash
nice -n 19 make -j6 build/run_tests build/test_audio
nice -n 19 ./build/run_tests --jobs 1 -v net/state
SDL_AUDIODRIVER=dummy CZ_AUDIO_DEVICE_TEST=1 nice -n 19 ./build/test_audio
```

The local sandbox denies socket binding (`Operation not permitted`) and Wine execution,
so a real host/client session cannot be verified here. Release CI now includes native
Windows loopback networking, state/playback, worker and audio tests. That workflow was
edited locally and has not been run from this session. Final full-suite results are in
`docs/performance.md`.

# Hosting for the internet, safely (2026-10-16)

Protocol **5**. Both ends must be this build (older builds are refused with "different game
version"). What changed on the wire: a password handshake, a secret in every datagram, and
strict checks on everything received. The full review, with every finding and its fix, is in
[server-security.md](server-security.md).

## Any port

Nothing is tied to 27015; it is only the default. A host (the game's, or the dedicated server)
listens for TCP and UDP on **one game port of your choice, 1024 to 65535**. The wire protocol
did not change (still **5**): the port is not sent anywhere but in the join address and in
what a host announces on the LAN.

| | Pick the port | Default |
|---|---|---|
| Game, from the menu | Multiplayer page, HOST card, **PORT** (digits only; kept in `settings.txt` as `hostport`) | 27015 |
| Game, command line | `--host PORT` (this run only; `--host` alone uses the menu's port) | the menu's |
| Dedicated server | `--port PORT`, or `CZ_PORT=PORT` in the environment (`--port` wins) | 27015 |
| Joining | `IP`, `IP:PORT` or `HOST:PORT` in the ADDRESS field or `--join` (IPv4 only: one `:` at most) | 27015 |

- Hosting below 1024 is refused (it needs root; the server won't run as root). A port that is
  not a plain number from 1024 to 65535 is refused with a message, not quietly replaced.
  Joining takes any port 1 to 65535, since a host may reach you through a forward.
- If the port is taken (another game, another server) hosting fails with
  "PORT N IS IN USE: PICK ANOTHER IN THE MULTIPLAYER MENU"; the server prints the same and exits.
- Games in the JOIN list are joined on the port their host announces; you type a port only for a
  game on another network.
- Open or forward **the chosen port**, TCP and UDP (`PORT` below; 27015 if you left the default).
- **LAN discovery is separate and fixed**: UDP 27016 ("who's hosting?") and 27017
  (announcements), LAN only. Nothing to forward for them; a host's announcement carries its real
  game port. They are off on the dedicated server unless you start it with `--lan`.

## A password

Set one whenever the game is reachable from outside your own network.

- Game: Multiplayer page, **PASSWORD** (used for the game you host and the ones you join), or
  `--password SECRET` on the command line (`--host`, `--join`).
- Dedicated server: `--password-file FILE` (first line), the `CZ_PASSWORD` environment
  variable, or `--password SECRET` (visible to other users in `ps`; avoid).
- A host without a password lets anyone in (LAN play is unchanged). A host with one sends a
  fresh random challenge to each joiner, who answers with HMAC-SHA-256 of it under the
  password; the password itself is never sent, and the answer is compared in constant time.
  Five wrong answers from one address lock that address out for a minute.
- The game remembers the menu's password in `settings.txt` (your config folder), as plain text;
  `--password` is not saved.
- Use a long random one: `head -c 24 /dev/urandom | base64 > cz-password`. The in-game field
  accepts letters, digits and `. - _ :` (32 characters at most); anything can be given with
  `--password`.

## What a server defends against

Silent connections (dropped after 8 s without a hello, 30 s without a word), floods (24
connections, 8 per address), oversized or too-frequent messages, malformed messages (any
number, index, count or float out of range drops the message, or the guest), forged player,
vehicle or line ids, host-only messages sent by guests (welcome, reject, level, clock, who
joined, "job done", "restart"), spawn floods (6 vehicles and 6 lines per guest) and unknown
datagram senders (every datagram carries a secret told in the welcome).

It does **not** encrypt anything. Anyone on the network path can read or alter a session.
For private play use a VPN (WireGuard, Tailscale) and `--bind` the server to its address.

## Running the dedicated server

```bash
make build/crumplezone-server
CZ_PASSWORD='a long random password' ./build/crumplezone-server --port 27015 --map 1 --name "Friends"
# any other port: --port 28123, or CZ_PORT=28123 ./build/crumplezone-server ...
```

- **Not as root.** It refuses to start as root (`--allow-root` overrides: don't). Make a user for it.
- **Firewall: only the game port**, TCP and UDP `PORT` (27015 unless you chose another). The
  LAN-discovery ports (UDP 27016/27017, fixed) are off on the server (`--lan` turns them on, for
  a server on your own network).
- `--port` takes 1024 to 65535 and refuses anything else (`--port 80`, `--port 99999`, `--port abc`)
  with a message and exit code 2; so does a bad `CZ_PORT`.
- It never writes a file and needs nothing from the network but those two sockets.
- **Time of day**: the server owns it, and every player sees the same sky (sent on joining and
  every second; a guest's own TIME OF DAY setting is kept for single player and for games it hosts).
  `--time HOURS` is where it starts (0 to 24, default 10; `13.5` is 1:30 pm), `--day-length MINUTES`
  how many real minutes a whole day takes (1 to 1440, default 24), `--frozen` holds it at `--time`.
  Example: `--time 12 --frozen` (always noon), `--time 6 --day-length 120` (a slow day from dawn).
- **One clock**: the server's. Guests set theirs by it with pings (NTP style) and every state,
  line and chatter line is stamped with it. Chatter, lines hooked and the sky are shown a little
  behind it (twice the send interval plus the measured latency and jitter), so what was said
  then happens then on every screen. Vehicles (0.4.1) are shown *now*: each other player's truck
  is run on from its newest state, along the arc it was on, by its age (about the one-way latency;
  F3 shows how far), so it is where its owner has it, to a few centimetres when driven steadily.
  Behind, a truck at 65 km/h was 2.5 to 5 m back of where it really was and each player crashed
  into the other's past (docs/plans/net-collisions.md).
- **Crashes between players**: each owner's machine runs its own truck's side of the crash; the
  other's copy is taken onto its physics (a whole soft body) for the knock and up to 0.6 s after,
  steered gently back onto its owner's states once they have the crash in them too, then eased
  onto them. Shown now, both machines have the same crash at the same moment (tests
  `net/moving-crash/*`: rear end, side, last-moment braking and swerving, 0 to 250 ms each way).
- **Traffic**: states are quantized and told only by the dents that moved since the newest one the
  receiver said it has; the server re-tells each guest's states to each other guest that way, and
  sends trucks more than 150 m from all of a player's own less often (10, 5, then 2 a second past
  150, 400 and 800 m). Seven guests driving together: about 60 KB/s down per guest, 450 KB/s up
  from the server (0.2.0: 284 and 1990); spread over the map, about 9 and 60.
- **Other players' vehicles**: one with nobody in it changes hands when you get in (MSG_TAKE to the host,
  which asks its owner: the owner is the one that knows nobody's in it, and the host takes the asks in
  order, so two at once get one yes and one no). Handed over, it keeps its body and dents on every machine
  under an id of the new owner's, and the new owner simulates and sends it (MSG_GIVE); getting it back is
  the same. A state's status byte carries a "driver in it" bit: then you ride along instead (chatter
  "ride ID", so the server sends that truck to you at the full rate). Someone who leaves holding another's
  vehicle leaves it behind: the host takes it on, parked, and its owner can take it back.
- **Versions**: protocol 8 (0.4.1). An older game is turned away with "version mismatch: update
  to 0.4.1 or newer"; a newer game joining an older server is told the server is older. Each side
  also says its build (the git commit) on joining: another build is let in, but everyone is told
  "NAME RUNS BUILD x, THIS GAME y: HANDLING MAY DIFFER" (physics changes between builds, tire grip
  say, don't change the protocol, and each player's truck handles as its owner's build has it).

Firewall examples (the server is IPv4 only; keep your SSH rule or you lock yourself out):

```bash
# ufw (PORT = your game port, 27015 by default)
sudo ufw default deny incoming && sudo ufw allow OpenSSH
sudo ufw allow PORT/tcp && sudo ufw allow PORT/udp && sudo ufw enable
```

```nft
# /etc/nftables.conf (the same, with a limit on new connections to the game port; PORT = 27015 by default)
table inet filter {
  chain input {
    type filter hook input priority 0; policy drop;
    ct state established,related accept
    iif lo accept
    tcp dport 22 accept
    tcp dport PORT ct state new limit rate 30/second burst 60 packets accept
    udp dport PORT accept
  }
}
```

### systemd

`/etc/systemd/system/crumplezone.service` (put the binary at `/opt/crumplezone/crumplezone-server`
and the password in `/etc/crumplezone/password`, mode 600, owned by root):

```ini
[Unit]
Description=Crumple Zone dedicated server
After=network-online.target
Wants=network-online.target

[Service]
# The port: --port N below, or drop it and use Environment=CZ_PORT=28123 (27015 if neither)
ExecStart=/opt/crumplezone/crumplezone-server --port 27015 --map 1 --name "Friends" --password-file ${CREDENTIALS_DIRECTORY}/password
LoadCredential=password:/etc/crumplezone/password
Restart=on-failure
RestartSec=3

# A throwaway user, no privileges, no files but its own
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
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
RestrictAddressFamilies=AF_INET
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
# And a ceiling, so a bug can't take the machine with it
MemoryMax=1G
TasksMax=64

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload && sudo systemctl enable --now crumplezone
journalctl -u crumplezone -f     # joins, leaves, "[net] ..." lines for what was refused
systemd-analyze security crumplezone
```

Updating a server to a new release: `docs/server-update.md`.

Players: `./crumplezone --join SERVER_IP:PORT --password 'the password'` (`:PORT` may be left off for 27015).

## Tests

`./build/run_tests --jobs 4 net/` runs the multiplayer tests, including the hostile-peer ones
(`net/sec-*`, in `tests/net_security.cpp`; `net/port-parse` and `net/custom-port` are the any-port ones): wrong and right passwords, connection floods,
slow connections, forged and mangled messages, forged datagrams, oversized levels, and a
fuzzer that throws thousands of random and mutated messages at a host and a client.
