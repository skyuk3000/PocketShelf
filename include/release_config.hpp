#pragma once

#include "updater.hpp"

namespace romm3ds::release {

// Production releases replace these runtime-linked placeholders with a reviewed
// HTTPS release origin, stable key ID, and matching Ed25519 public key. Keeping
// them out of the header also ensures the fail-closed build still links and
// exercises the real verifier path instead of optimizing it away.
extern const bool kConfigured;
extern const char kReleaseBaseUrl[];
extern const char kReleaseKeyId[];
extern const std::array<std::uint8_t, 32> kReleasePublicKey;

TrustedUpdateKey trustedUpdateKey();

} // namespace romm3ds::release
