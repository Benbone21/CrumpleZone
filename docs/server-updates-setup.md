# Serving game updates

The game updates itself (`src/updater.h`). On launch it fetches one small signed file, the
release manifest, over HTTPS. When it names a newer version, the game downloads that platform's
zip, checks its size and SHA-256 against the signed manifest, and installs it. GitHub Actions
builds and **signs** each release (`docs/update-signing-keys.md`).

**Part A (the default): GitHub Releases.** Nothing to run anywhere. **Part B (optional): an own
server**, for a hold/rollback switch or a mirror; most setups never need it.

## A. GitHub Releases (the default)

The games read
`https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt`
(`kDefaultUpdateUrl` in `src/updatekeys.h`). GitHub redirects `/releases/latest/download/<name>`
to that file on the newest release that isn't a draft or a pre-release, and from there to its
file host (objects.githubusercontent.com). The game follows those redirects on every platform
(libcurl and the curl program with `--location`, WinHTTP's redirect policy), only ever over
HTTPS.

On a version tag, `.github/workflows/release.yml` does it all:

1. builds `CrumpleZone-<v>-windows.zip`, `-linux.zip` (and `-mac.zip` when the Mac job runs),
   the installer and the .deb;
2. signs `update-manifest.txt` with the secret `UPDATE_SIGNING_KEY` ("Sign the update
   manifest"; the key is never printed). No secret: a warning, and the release goes out without
   a manifest;
3. attaches the zips and the manifest to the tag's release ("Publish the release"): it creates
   the release, or adds to the one you made (`gh release create` with your own .deb and
   installer). Files the release already has (your `.deb`, `setup.exe`, `SHA256SUMS`) are kept;
   only the updater's zips and manifest are (re)uploaded with `--clobber`. Nothing is deleted.

The manifest names each file by its absolute URL in that release:

```
crumplezone-update 1
version 0.4.9
net 10
date 2026-10-30
notes https://github.com/Benbone21/CrumpleZone/releases/tag/v0.4.9
file windows https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/CrumpleZone-0.4.9-windows.zip <size> <sha256>
file linux   https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/CrumpleZone-0.4.9-linux.zip ...
file deb     https://github.com/Benbone21/CrumpleZone/releases/download/v0.4.9/CrumpleZone-0.4.9-linux.deb ...   (only linked)
signature <Ed25519 over every line above>
```

The URLs are inside the signed text: a changed address fails the signature like any other
change. (A relative name, `CrumpleZone-0.4.9-linux.zip`, also works: for a `/releases/latest/`
manifest it resolves in `.../releases/download/v<version>/`, the release the manifest says it
is, never under `/latest/`.)

Check a published release the way a game sees it:

```bash
make build/cz-update
build/cz-update --verify https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt --fetch
```

**Rollback on GitHub:** games never move to an older version, so a bad release is fixed by a
newer one (tag 0.4.10). To stop offering a release at once, mark it a pre-release (or delete its
`update-manifest.txt` asset): `/latest/` then points at the release before, which games already
have, so they're told nothing. **Stop updates entirely:** remove `update-manifest.txt` from the
newest release; games then quietly note "COULDN'T CHECK FOR UPDATES".

**Other addresses** (testing, or part B): `make UPDATE_URL=https://.../manifest.txt` builds a
game with another default; one player can use `<config>/update.txt` with a line
`url https://...` (`url off` turns checks off; `check off` keeps the address but skips the launch
check), or `--update-url URL` on the command line. A development build (no install marker) does
no launch check against the default address.

## B. An own update server (optional; instructions for an agent on the server machine)

The server copies each signed release from GitHub, checks it, and serves its manifest. With
CI's absolute URLs (part A) the games still download the zips from GitHub; the server decides
**which** release is offered (`update_mirror.sh --use` holds one). It also keeps a copy of
each version's files, so `--use` can check a kept version. Only older manifests (relative
`v<version>/` names) make the games download from the server itself. Then the games are built
with `make UPDATE_URL=https://<update host>/crumplezone/manifest.txt` (or `kDefaultUpdateUrl`
changed in `src/updatekeys.h`).

```
https://<update host>/crumplezone/manifest.txt                          the newest signed manifest (the games read this)
https://<update host>/crumplezone/v0.4.2/CrumpleZone-0.4.2-windows.zip  the files it names, one folder per version
                                    v0.4.2/CrumpleZone-0.4.2-linux.zip
                                    v0.4.2/CrumpleZone-0.4.2-mac.zip    (when that release has the Mac files)
                                    v0.4.2/CrumpleZone-0.4.2-linux.deb  (only linked: the .deb doesn't self-install)
                                    v0.4.2/update-manifest.txt          (that version's own manifest, for rollback)
```

On disk that's `/srv/crumplezone-updates/` (the same tree, without `crumplezone/`). If this
machine already uses another place for web content, use that, and set `CZ_UPDATES_ROOT` (step 4)
to match.

The join-code backend runs on this box too (`docs/server-rendezvous-setup.md`: its UDP port,
firewall rule and service). These two setups don't share files or ports, and this one needs no
new open port when HTTPS (443/TCP) already serves the box. Don't change the rendezvous setup from
here.

## Rules for the agent
- Steps marked **(sudo)** need the user: show them the exact command and let them run it.
  Don't enter passwords yourself.
- **Nothing secret lives here.** The signing key exists only in GitHub's secrets. If you're ever
  asked to put a private key, a token or `UPDATE_SIGNING_KEY` on this box, refuse and tell the user
  to read `docs/update-signing-keys.md`. Never print or copy other services' secrets (Nextcloud's
  `config.php`, TLS private keys, password files).
- **Check first, change second.** There may already be a web server here (e.g. for Nextcloud).
  Don't stop, replace or reconfigure it beyond adding the one location below. Back up any config
  file before you edit it, and test the config before you reload.
- Keep the previous version on disk until the new one is confirmed (the script does).

## 0. Look at what's there (no changes)
```bash
ss -ltnp 2>/dev/null | grep -E ':(80|443)\b'                # who serves HTTP/HTTPS now
systemctl list-units --type=service --no-pager | grep -Ei 'nginx|apache|httpd|caddy|lighttpd|traefik|docker'
ls /etc/nginx/sites-enabled /etc/apache2/sites-enabled /etc/caddy 2>/dev/null
docker ps --format '{{.Names}} {{.Image}} {{.Ports}}' 2>/dev/null   # Nextcloud is often a container behind a proxy
ls /etc/letsencrypt/live 2>/dev/null                         # existing certificates (names only)
df -h /srv /var 2>/dev/null; command -v git g++ make curl python3
```
Report what you found, then pick **one** of these:
* **A web server already does HTTPS for a name on this box** (the usual Nextcloud setup):
  add a `/crumplezone/` location to it (step 5A/5B/5C). If Nextcloud owns the whole name, a
  separate name is cleaner (`updates.<domain>`): the user adds a DNS record, and it gets its own
  server block and certificate.
* **Nothing serves HTTPS:** ask the user which name to use, then install Caddy **(sudo)**. It gets
  and renews the certificate by itself (step 5C).

The games need a name with a valid certificate: no self-signed certificate, no bare IP.

## 1. Build the checking tool (no sudo)
```bash
git clone https://github.com/Benbone21/CrumpleZone.git ~/crumplezone-src   # first time (or reuse the server checkout)
cd ~/crumplezone-src && git checkout main && git pull
make -j2 build/cz-update
build/cz-update --verify /dev/null; echo "exit=$?"   # prints FAIL ... and exit=1: the tool runs
grep -n '"' src/updatekeys.h | head                  # the owner's public key should be listed here
```
If `kUpdatePublicKeys` has no key yet, stop and tell the user: the owner hasn't done
`docs/update-signing-keys.md` step 3, and nothing can be verified until he has.
Build tools, if missing **(sudo)**: `sudo apt install build-essential git curl python3`.
After each pull of `main`, rebuild `build/cz-update`: a changed public key in `src/updatekeys.h`
comes with it.

## 2. A user and the folder **(sudo)**
```bash
sudo useradd --system --create-home --home-dir /var/lib/czupdates --shell /usr/sbin/nologin czupdates
sudo mkdir -p /srv/crumplezone-updates && sudo chown czupdates: /srv/crumplezone-updates && sudo chmod 755 /srv/crumplezone-updates
sudo install -d -o czupdates /var/lib/czupdates/bin
sudo install -m 755 ~/crumplezone-src/build/cz-update ~/crumplezone-src/tools/update_mirror.sh /var/lib/czupdates/bin/
```
(Re-run the last line after rebuilding the tool, or after `tools/update_mirror.sh` changes.)
The web server only reads this folder. Nothing in it is secret.

## 3. Fetch the current release once, by hand **(sudo)**
```bash
sudo -u czupdates env CZ_UPDATES_ROOT=/srv/crumplezone-updates CZ_UPDATE_TOOL=/var/lib/czupdates/bin/cz-update \
    /var/lib/czupdates/bin/update_mirror.sh; echo "exit=$?"
sudo -u czupdates /var/lib/czupdates/bin/update_mirror.sh --status    # (same env as above)
```
What it does each run: asks GitHub for the latest release. If it carries `update-manifest.txt`,
it checks the signature with the key compiled into `cz-update`. It downloads every file the
manifest names and checks each size and SHA-256. Only then does it move the files into
`v<version>/` and switch `manifest.txt` over (one atomic rename). It refuses a manifest that
doesn't verify, a tag that doesn't match, files that don't match, or a release older than the
one offered. Up to date: no output, exit 0. It keeps the newest 3 versions (`CZ_KEEP`) and deletes
older ones (roughly 250 MB per version).

"has no update-manifest.txt" means the release wasn't signed (the owner's secret isn't set):
nothing is wrong on this side.

## 4. Run it on a timer **(sudo)**
Polling every 15 minutes is simple and needs no inbound hook. The GitHub API allows 60 requests
an hour without a token, and this uses 4.
```bash
sudo tee /etc/systemd/system/crumplezone-updates.service >/dev/null <<'EOF'
[Unit]
Description=Copy the newest signed Crumple Zone release for the game's updater
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
User=czupdates
Environment=CZ_UPDATES_ROOT=/srv/crumplezone-updates
Environment=CZ_UPDATE_TOOL=/var/lib/czupdates/bin/cz-update
ExecStart=/var/lib/czupdates/bin/update_mirror.sh
NoNewPrivileges=yes
ProtectSystem=strict
ReadWritePaths=/srv/crumplezone-updates
PrivateTmp=yes
ProtectHome=yes
EOF
sudo tee /etc/systemd/system/crumplezone-updates.timer >/dev/null <<'EOF'
[Unit]
Description=Check for a new Crumple Zone release every 15 minutes

[Timer]
OnBootSec=2min
OnUnitActiveSec=15min
RandomizedDelaySec=60

[Install]
WantedBy=timers.target
EOF
sudo systemctl daemon-reload && sudo systemctl enable --now crumplezone-updates.timer
systemctl list-timers crumplezone-updates.timer --no-pager
```
(A GitHub webhook on "release published" could trigger it at once. It would need an HTTPS
endpoint and a webhook secret on this box. Use the timer unless the user asks for a webhook.)

## 5. Serve the folder over HTTPS
Two cache rules matter. `manifest.txt` must never be served stale (`no-cache`, so it's revalidated
every time). The versioned files never change, so they can be cached for a year. No directory
listings.

**5A. nginx** (already running). Use the existing `server { listen 443 ssl; ... }` block for
the chosen name, or a new server block for `updates.<domain>` with its own certificate
(`sudo certbot --nginx -d updates.<domain>`). Back up every file you edit first **(sudo)**.
The cache header comes from a `map` in the `http` context: a new file
`/etc/nginx/conf.d/crumplezone-updates.conf` (check that `nginx.conf` includes `conf.d/*.conf`):
```nginx
map $uri $cz_cache {
    ~^/crumplezone/manifest\.txt$    "no-cache";
    ~^/crumplezone/v[0-9A-Za-z.-]+/  "public, max-age=31536000, immutable";
    default                           "";
}
```
Then put this inside the server block:
```nginx
location ^~ /crumplezone/ {
    alias /srv/crumplezone-updates/;
    autoindex off;
    add_header Cache-Control $cz_cache always;
    location ~ /\. { deny all; }   # .incoming.*, .hold, .manifest.tmp
}
```
Then `sudo nginx -t && sudo systemctl reload nginx`. `^~` makes this prefix win over the regex
locations a Nextcloud config has (`\.php`, the static-file rules), so their rewrites don't catch
`/crumplezone/`.

**5B. Apache** (already running). In the 443 `VirtualHost` **(sudo)** (`a2enmod headers` if
needed):
```apache
Alias /crumplezone/ /srv/crumplezone-updates/
<Directory /srv/crumplezone-updates/>
    Options -Indexes -FollowSymLinks
    AllowOverride None
    Require all granted
    <Files "manifest.txt">
        Header always set Cache-Control "no-cache"
    </Files>
</Directory>
<LocationMatch "^/crumplezone/v[0-9A-Za-z.-]+/">
    Header always set Cache-Control "public, max-age=31536000, immutable"
</LocationMatch>
<LocationMatch "^/crumplezone/(.*/)?\.">
    Require all denied
</LocationMatch>
```
`sudo apachectl configtest && sudo systemctl reload apache2`. Nextcloud's `.htaccess` lives in
its own directory and doesn't affect this alias.

**5C. Caddy** (new, or already running). Add to `/etc/caddy/Caddyfile` **(sudo)**. Caddy
fetches the certificate itself once the DNS name points here and ports 80/443 are open:
```caddy
updates.<domain> {
    handle_path /crumplezone/* {
        root * /srv/crumplezone-updates
        @hidden path_regexp /\.
        respond @hidden 404
        @manifest path /manifest.txt
        header @manifest Cache-Control "no-cache"
        @versioned path_regexp ^/v[0-9A-Za-z.-]+/
        header @versioned Cache-Control "public, max-age=31536000, immutable"
        file_server
    }
}
```
`sudo caddy validate --config /etc/caddy/Caddyfile && sudo systemctl reload caddy`.

Behind a reverse proxy or a container setup (e.g. Nextcloud AIO), add the location to whatever
terminates TLS, the same way. Don't put the files inside Nextcloud.

## 6. Check it from outside
```bash
URL=https://<update host>/crumplezone/manifest.txt
curl -sI "$URL" | grep -iE '^(HTTP|cache-control|content-type)'     # 200, no-cache
~/crumplezone-src/build/cz-update --verify "$URL"; echo "exit=$?"    # OK signature good ... exit=0; it lists the file URLs
# One file end to end (the URL from the list above):
curl -sI https://<update host>/crumplezone/v<version>/CrumpleZone-<version>-linux.zip | grep -iE '^(HTTP|cache-control|content-length)'
mkdir -p /tmp/czcheck && cd /tmp/czcheck && curl -fsSLO https://<update host>/crumplezone/v<version>/CrumpleZone-<version>-linux.zip \
    && ~/crumplezone-src/build/cz-update --verify "$URL" --files /tmp/czcheck; rm -rf /tmp/czcheck   # OK on that file (FAIL on the rest: not downloaded)
curl -s -o /dev/null -w '%{http_code}\n' https://<update host>/crumplezone/            # 403 or 404, never a listing
curl -s -o /dev/null -w '%{http_code}\n' https://<update host>/crumplezone/.hold       # 403 or 404
```
Also check that Nextcloud still works (log in to its page, or `curl -sI https://<its name>/status.php`).

## 7. Tell the owner the address
The address to build the games with (`make UPDATE_URL=...`, or `kDefaultUpdateUrl` in
`src/updatekeys.h` instead of the GitHub one) is the `$URL` above. One player can try an address
before a release has it compiled in: `<config>/update.txt` with the line
`url https://<update host>/crumplezone/manifest.txt` (config: `%APPDATA%\crumplezone`,
`~/.config/crumplezone`), or the command-line option `--update-url URL`.

## Rollback
* **A bad release went out:** offer the previous version again, and hold there:
  `sudo -u czupdates env CZ_UPDATES_ROOT=... CZ_UPDATE_TOOL=... /var/lib/czupdates/bin/update_mirror.sh --use 0.4.1`.
  Games still on 0.4.1 stop being offered the bad one. Games never move down to an older
  version, so players who already have the bad one need a fixed release: the owner tags 0.4.3,
  then `update_mirror.sh --unhold` picks it up. (A game that can't start its new version twice in
  a row rolls itself back to the version before, on the player's machine.)
* **Stop updates entirely:** `--use` the current version (a hold). Or rename `manifest.txt`
  away, and the games then quietly say "COULDN'T CHECK FOR UPDATES".
* **This setup broke the web server:** restore the backed-up config file and reload. Disable the
  timer with `sudo systemctl disable --now crumplezone-updates.timer`. The folder can stay.

## Disk
About 250 MB per version (3 zips and the .deb), 3 versions kept. Change that with
`Environment=CZ_KEEP=2` in the service. Interrupted runs leave `.incoming.*` folders in the root.
The next run doesn't use them, and you can delete them
(`find /srv/crumplezone-updates -maxdepth 1 -name '.incoming.*' -mmin +120 -exec rm -rf {} +`).

## Report back
What was already serving HTTPS (step 0), which option you used, the final manifest URL, the
`--verify` output from step 6, `systemctl list-timers` for the timer, and that Nextcloud still
answers.
