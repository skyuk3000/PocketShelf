#include "release_config.hpp"

namespace romm3ds::release {

// Fail-closed production placeholder. No private or test release key belongs
// here. A public release must replace only these public trust-anchor values in a
// reviewed release configuration change.
const bool kConfigured = false;
const char kReleaseBaseUrl[] = "";
const char kReleaseKeyId[] = "";
const std::array<std::uint8_t, 32> kReleasePublicKey{};

TrustedUpdateKey trustedUpdateKey() {
    return TrustedUpdateKey{kConfigured, kReleaseKeyId, kReleasePublicKey};
}

} // namespace romm3ds::release
