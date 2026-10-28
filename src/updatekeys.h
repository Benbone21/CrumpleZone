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

// Where the signed manifest is served (HTTPS). Overridden by CZ_UPDATE_URL at build time, then by "url ..." in
// <config>/update.txt, then by --update-url. "" (the default): no update checks.
#ifdef CZ_UPDATE_URL
constexpr const char* kDefaultUpdateUrl = CZ_UPDATE_URL;
#else
constexpr const char* kDefaultUpdateUrl = "";
#endif

}  // namespace update
