// The updater's two settings that belong to the owner (docs/update-signing-keys.md, docs/server-updates-setup.md).
#pragma once

namespace update {

// Ed25519 public keys (64 hex characters each) that may sign release manifests. The matching private key lives ONLY in the
// GitHub Actions secret UPDATE_SIGNING_KEY. Two entries while a key is being replaced; nullptr ends the list. With no key the game
// never trusts a manifest, so it says that updates aren't set up in this build.
constexpr const char* kUpdatePublicKeys[] = {
    "b4af0d4e279fa1ef9d97b8b75b943a3a86c396dae793f23ebc52c285ccfef445",  // the owner's key (2026-10-28)
    nullptr,
};

// Where the signed manifest is served (HTTPS): the newest GitHub release's update-manifest.txt (GitHub redirects /latest/download/
// to the newest release that isn't a pre-release; CI attaches the signed manifest to each release, docs/server-updates-setup.md).
// Overridden by CZ_UPDATE_URL at build time (make UPDATE_URL=...), then by "url ..." in <config>/update.txt, then by --update-url.
// "url off" in update.txt (or an empty CZ_UPDATE_URL): no update checks.
constexpr const char* kDefaultUpdateUrlBuiltin = "https://github.com/Benbone21/CrumpleZone/releases/latest/download/update-manifest.txt";
#ifdef CZ_UPDATE_URL
constexpr const char* kDefaultUpdateUrl = CZ_UPDATE_URL;
#else
constexpr const char* kDefaultUpdateUrl = kDefaultUpdateUrlBuiltin;
#endif

}  // namespace update
