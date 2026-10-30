# Update signing keys (the owner's steps)

The game updates itself from GitHub Releases (`src/updater.h`): it reads the newest release's
`update-manifest.txt`. It only installs a release whose manifest is signed with your **private
key**, and it checks that signature with the **public key** compiled into it. CI signs each
release and attaches the manifest and the zips to it (`.github/workflows/release.yml`, the steps
"Sign the update manifest" and "Publish the release"). An own server is optional
(`docs/server-updates-setup.md`).

| What | Where it lives | Secret? |
|---|---|---|
| Private key (64 hex characters) | a file on your own machine, a backup you trust, and the GitHub Actions secret `UPDATE_SIGNING_KEY`. **Nowhere else**: not in the repository, not on the server, not in a chat. | yes |
| Public key (64 hex characters) | `src/updatekeys.h`, `kUpdatePublicKeys` | no |
| Update address | `src/updatekeys.h`, `kDefaultUpdateUrl`: `https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt` (set already) | no |

You run every step here yourself. No agent creates, sees or stores the private key.

## 1. Make the key pair (once)

On your own Linux machine, in the repository:

```bash
make build/cz-update
build/cz-update --keygen ~/crumplezone-update-signing.key
```

It writes the private key to that file (mode 600) and prints only the **public** key. Keep the
file outside the repository. Back it up somewhere safe (a password manager's file attachment,
or an offline USB stick). If it's lost, you can't sign releases until you change keys (step 6).
It refuses to overwrite an existing file.

To see the public key again later: `build/cz-update --pubkey ~/crumplezone-update-signing.key`.

## 2. Put the private key into GitHub

Repository **Settings → Secrets and variables → Actions → New repository secret**:

* Name: `UPDATE_SIGNING_KEY`
* Secret: the file's contents (the 64 hex characters)

Or, with the GitHub CLI: `gh secret set UPDATE_SIGNING_KEY < ~/crumplezone-update-signing.key`.

GitHub hides the value in logs. Only the release workflow's signing step reads it, and
`cz-update` never prints it or writes it to disk. Pull requests from forks don't get secrets.

## 3. Put the public key into the code (done: `b4af0d4e...`)

Edit `src/updatekeys.h`:

```cpp
constexpr const char* kUpdatePublicKeys[] = {
    "<the 64 hex characters of your public key>",   // <- from step 1
    nullptr,
};
```

The address (`kDefaultUpdateUrl`) is GitHub's link to the newest release's manifest; change it
only to use an own server (`docs/server-updates-setup.md`, part B). Commit to `main`.

Without a key, the game checks nothing and says "UPDATES AREN'T SET UP IN THIS BUILD" when
asked.

## 4. Release as usual

`git tag v0.4.2 && git push origin v0.4.2`. Check two things after the workflow finishes:

* The publish job's log has `signed dist/update-manifest.txt: version 0.4.2, ... (compiled into this source)`.
  A warning "No update manifest" means the secret is missing, or doesn't match the public key
  in `src/updatekeys.h`. The release is still published, as before, but without a manifest, so
  no game is offered it (they keep seeing the previous release's, if any).
* The release has `update-manifest.txt`, `CrumpleZone-<v>-windows.zip` and `CrumpleZone-<v>-linux.zip`
  (and the Mac zip when the Mac job ran), besides the usual installer, .deb and DMG.
* `build/cz-update --verify https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt --fetch`
  says OK for the signature and each download (what a game sees).

Your own `.deb` and installer: making the release yourself with `gh release create` (before or
after CI) is fine. CI only adds files the release doesn't have yet, so it never replaces your
`.deb`, `setup.exe` or `SHA256SUMS`; it always (re)uploads its zips and `update-manifest.txt`.
**Don't replace those zips** with your own builds (`gh release upload ... --clobber`): the
manifest signed CI's files, so a game would refuse yours as "doesn't match" (safely, but no
update). If you must, sign a new manifest for them yourself (`cz-update --sign ... --key-file`)
and upload it too.

The newest release that isn't a pre-release is what `.../releases/latest/` points at, so
games see a release as soon as it carries the manifest. A pre-release is never offered. The
manifest lists the Mac zip only when the Mac job ran for that tag. To add the Mac files later and
re-sign the manifest: Actions → **release** → **Run workflow** on the tag. That rebuilds
everything and uploads it again.

## 5. The first updating release

Games from before the updater (0.4.0 and older) can't update themselves. Players install the
first release with the updater by hand. After that, the game offers each new version on launch.
It also offers one when a join code finds the host on a newer version.

* **Windows:** the installer (no admin). Updates replace the install folder
  (`%LOCALAPPDATA%\Programs\Crumple Zone`). The version before is kept as `Crumple Zone.old`.
* **macOS:** drag the app to Applications. The first launch needs right-click → Open, because it
  isn't notarized. Updates downloaded by the game aren't quarantined, so there is no second
  Gatekeeper prompt. The account must be able to write to the app's folder (any admin account
  can write to /Applications), else the game says so.
* **Linux:** the new `CrumpleZone-<v>-linux.zip`: unzip it, then run `./install.sh`. That installs
  into `~/.local` (no sudo) and updates itself. The `.deb` still installs system-wide, but it
  can't update itself (its files belong to apt; the game never touches them): on a new release
  it shows a card once per release, "INSTALLED FROM THE .DEB, WHICH CAN'T UPDATE ITSELF", with
  the two ways (the new .deb, COPY LINK; or the per-user zip and `install.sh`).

## 6. Changing keys (lost, leaked, or just old)

The game accepts a manifest signed by **any** key in `kUpdatePublicKeys`, so a key change can
overlap:

1. Make a new pair (step 1, a new file name).
2. Add the new public key to `kUpdatePublicKeys` next to the old one, and release with the
   **old** secret still set. Wait until most players have that release.
3. Replace the secret `UPDATE_SIGNING_KEY` with the new private key. Remove the old public key
   from the code, then release.

**If the private key leaked:** do step 3 at once (new key only), release, and delete the old key
file. To abuse an old key, someone also has to serve their manifest at your update address (your
GitHub releases, or your server or its DNS) or break HTTPS. Even then, games stop trusting the old key once they update
past it. **If it's lost:** the same, but players on the old key must download the release by hand
once.

## Checking a signature by hand

```bash
build/cz-update --verify https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt --fetch   # key compiled in; downloads and checks each file
build/cz-update --verify update-manifest.txt --key <public key hex> --files .   # a downloaded manifest and its files
```

Independently of this code, OpenSSL 3 checks the same signature:
the body is every line before `signature`, and the public key in PEM is the DER prefix
`302a300506032b6570032100` followed by the 32 key bytes, in base64.
`openssl pkeyutl -verify -pubin -inkey pub.pem -rawin -in body -sigfile sig.bin`.
