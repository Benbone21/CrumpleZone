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
```

- **Not as root.** It refuses to start as root (`--allow-root` overrides: don't). Make a user for it.
- **Firewall: only the game port**, TCP and UDP 27015. The LAN-discovery ports (UDP 27016/27017)
  are off on the server (`--lan` turns them on, for a server on your own network).
- It never writes a file and needs nothing from the network but those two sockets.

Firewall examples (the server is IPv4 only; keep your SSH rule or you lock yourself out):

```bash
# ufw
sudo ufw default deny incoming && sudo ufw allow OpenSSH
sudo ufw allow 27015/tcp && sudo ufw allow 27015/udp && sudo ufw enable
```

```nft
# /etc/nftables.conf (the same, with a limit on new connections to the game port)
table inet filter {
  chain input {
    type filter hook input priority 0; policy drop;
    ct state established,related accept
    iif lo accept
    tcp dport 22 accept
    tcp dport 27015 ct state new limit rate 30/second burst 60 packets accept
    udp dport 27015 accept
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

Players: `./crumplezone --join SERVER_IP:27015 --password 'the password'`.

## Tests

`./build/run_tests --jobs 4 net/` runs the multiplayer tests, including the hostile-peer ones
(`net/sec-*`, in `tests/net_security.cpp`): wrong and right passwords, connection floods,
slow connections, forged and mangled messages, forged datagrams, oversized levels, and a
fuzzer that throws thousands of random and mutated messages at a host and a client.
