#include "updater.hpp"

#include "monocypher-ed25519.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>

namespace romm3ds {
namespace {

bool fail(UpdateManifestError value, UpdateManifestError& error) {
    error = value;
    return false;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <std::size_t N>
bool parseHex(std::string_view text, std::array<std::uint8_t, N>& output) {
    if (text.size() != N * 2) return false;
    for (std::size_t i = 0; i < N; ++i) {
        const int high = hexValue(text[i * 2]);
        const int low = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

template <typename T>
bool parseCanonicalDecimal(std::string_view text, T& output) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    T value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
    output = value;
    return true;
}

bool parseTitleId(std::string_view text, std::uint64_t& output) {
    if (text.size() != 16) return false;
    std::uint64_t value = 0;
    for (char ch : text) {
        if (ch >= 'a' && ch <= 'f') return false;
        const int digit = hexValue(ch);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<std::uint64_t>(digit);
    }
    output = value;
    return value != 0;
}

bool httpsUrl(std::string_view url) {
    if (url.size() < 9 || url.compare(0, 8, "https://") != 0 ||
        url.size() > kMaxUpdateUrlBytes) return false;
    const std::size_t authority_end = url.find_first_of("/?#", 8);
    if (authority_end == 8 || url.find('#') != std::string_view::npos) return false;
    for (unsigned char ch : url) if (ch <= 0x20 || ch == 0x7f) return false;
    return true;
}

std::string_view httpsOrigin(std::string_view url) {
    if (!httpsUrl(url)) return {};
    const std::size_t end = url.find_first_of("/?", 8);
    return url.substr(0, end == std::string_view::npos ? url.size() : end);
}

bool validVersionName(std::string_view value) {
    if (value.empty() || value.size() > kMaxUpdateVersionNameBytes) return false;
    for (unsigned char ch : value) {
        if (!(ch == '.' || ch == '-' || ch == '+' ||
              (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z'))) return false;
    }
    return true;
}

bool validKeyId(std::string_view value) {
    if (value.empty() || value.size() > kMaxUpdateKeyIdBytes) return false;
    for (unsigned char ch : value) {
        if (!(ch == '-' || ch == '_' || (ch >= '0' && ch <= '9') ||
              (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) return false;
    }
    return true;
}

std::uint32_t rotateRight(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32 - bits));
}

constexpr std::array<std::uint32_t, 64> kShaConstants = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

} // namespace

bool parseUpdateManifest(std::string_view contents, UpdateManifest& output,
                         UpdateManifestError& error) {
    if (contents.size() > kMaxUpdateManifestBytes)
        return fail(UpdateManifestError::TooLarge, error);
    if (contents.empty() || contents.back() != '\n' || contents.find('\r') != std::string_view::npos)
        return fail(UpdateManifestError::NonCanonical, error);

    constexpr std::array<std::string_view, 9> keys = {
        "format", "version", "version_name", "title_id", "size", "cia_url",
        "sha256", "key_id", "signature",
    };
    std::array<std::string_view, keys.size()> values{};
    std::size_t offset = 0;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const std::size_t newline = contents.find('\n', offset);
        if (newline == std::string_view::npos)
            return fail(UpdateManifestError::MissingField, error);
        const std::string_view line = contents.substr(offset, newline - offset);
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || line.substr(0, equals) != keys[index])
            return fail(UpdateManifestError::NonCanonical, error);
        values[index] = line.substr(equals + 1);
        offset = newline + 1;
    }
    if (offset != contents.size()) return fail(UpdateManifestError::NonCanonical, error);

    UpdateManifest parsed;
    if (values[0] != "pocketshelf-update-v1")
        return fail(UpdateManifestError::InvalidFormat, error);
    if (!parseCanonicalDecimal(values[1], parsed.version) || parsed.version == 0)
        return fail(UpdateManifestError::NonCanonical, error);
    if (!validVersionName(values[2]))
        return fail(UpdateManifestError::InvalidVersionName, error);
    parsed.version_name.assign(values[2].data(), values[2].size());
    if (!parseTitleId(values[3], parsed.title_id))
        return fail(UpdateManifestError::InvalidTitleId, error);
    if (!parseCanonicalDecimal(values[4], parsed.size) || parsed.size == 0)
        return fail(UpdateManifestError::NonCanonical, error);
    if (!httpsUrl(values[5])) return fail(UpdateManifestError::InvalidUrl, error);
    parsed.cia_url.assign(values[5].data(), values[5].size());
    if (!parseHex(values[6], parsed.sha256)) return fail(UpdateManifestError::InvalidHash, error);
    for (char ch : values[6]) if (ch >= 'A' && ch <= 'F')
        return fail(UpdateManifestError::NonCanonical, error);
    if (!validKeyId(values[7])) return fail(UpdateManifestError::InvalidKeyId, error);
    parsed.key_id.assign(values[7].data(), values[7].size());
    std::array<std::uint8_t, 64> signature{};
    if (!parseHex(values[8], signature)) return fail(UpdateManifestError::InvalidSignature, error);
    for (char ch : values[8]) if (ch >= 'A' && ch <= 'F')
        return fail(UpdateManifestError::NonCanonical, error);
    parsed.signature.assign(signature.begin(), signature.end());

    output = std::move(parsed);
    error = UpdateManifestError::None;
    return true;
}

std::string updateManifestSignedBytes(const UpdateManifest& manifest) {
    char title_id[17];
    std::snprintf(title_id, sizeof(title_id), "%016llX",
                  static_cast<unsigned long long>(manifest.title_id));
    return "format=pocketshelf-update-v1\nversion=" + std::to_string(manifest.version) +
           "\nversion_name=" + manifest.version_name + "\ntitle_id=" + title_id +
           "\nsize=" + std::to_string(manifest.size) + "\ncia_url=" + manifest.cia_url +
           "\nsha256=" + hexSha256(manifest.sha256) + "\nkey_id=" + manifest.key_id + "\n";
}

UpdateSignatureStatus verifyUpdateManifestSignature(const UpdateManifest& manifest,
                                                    const TrustedUpdateKey& trusted_key) noexcept {
    if (!trusted_key.configured || trusted_key.key_id.empty())
        return UpdateSignatureStatus::NoTrustedKey;
    if (manifest.key_id != trusted_key.key_id) return UpdateSignatureStatus::WrongKeyId;
    if (manifest.signature.size() != 64) return UpdateSignatureStatus::Invalid;
    const std::string signed_bytes = updateManifestSignedBytes(manifest);
    return crypto_ed25519_check(manifest.signature.data(), trusted_key.public_key.data(),
                                reinterpret_cast<const std::uint8_t*>(signed_bytes.data()),
                                signed_bytes.size()) == 0 ? UpdateSignatureStatus::Valid :
                                                          UpdateSignatureStatus::Invalid;
}

UpdatePolicy evaluateUpdatePolicy(const UpdateManifest& manifest,
                                  std::uint32_t current_version,
                                  std::uint64_t expected_title_id,
                                  std::string_view manifest_url,
                                  const TrustedUpdateKey& trusted_key) noexcept {
    if (!trusted_key.configured) return UpdatePolicy::UnsupportedNoTrustedKey;
    if (manifest.key_id != trusted_key.key_id) return UpdatePolicy::RejectWrongKeyId;
    if (manifest.title_id != expected_title_id) return UpdatePolicy::RejectWrongTitle;
    if (!httpsUrl(manifest_url) || !httpsUrl(manifest.cia_url))
        return UpdatePolicy::RejectInsecureUrl;
    if (httpsOrigin(manifest.cia_url) != httpsOrigin(manifest_url))
        return UpdatePolicy::RejectCrossOrigin;
    if (manifest.size > kMaxUpdateCiaBytes) return UpdatePolicy::RejectTooLarge;
    if (verifyUpdateManifestSignature(manifest, trusted_key) != UpdateSignatureStatus::Valid)
        return UpdatePolicy::RejectBadSignature;
    if (manifest.version <= current_version) return UpdatePolicy::RejectNotNewer;
    return UpdatePolicy::ReadyToStage;
}

std::string updateManifestUrl(std::string_view release_base_url) {
    while (!release_base_url.empty() && release_base_url.back() == '/')
        release_base_url.remove_suffix(1);
    return std::string(release_base_url) + "/pocketshelf-update-v1.txt";
}

const char* updateManifestErrorMessage(UpdateManifestError error) noexcept {
    switch (error) {
        case UpdateManifestError::None: return "no error";
        case UpdateManifestError::TooLarge: return "manifest is too large";
        case UpdateManifestError::MissingField: return "manifest field is missing";
        case UpdateManifestError::DuplicateField: return "manifest field is duplicated";
        case UpdateManifestError::UnknownField: return "manifest contains an unknown field";
        case UpdateManifestError::InvalidFormat: return "manifest format is invalid";
        case UpdateManifestError::NonCanonical: return "manifest is not canonical";
        case UpdateManifestError::InvalidNumber: return "manifest number is invalid";
        case UpdateManifestError::InvalidVersionName: return "version name is invalid";
        case UpdateManifestError::InvalidTitleId: return "title ID is invalid";
        case UpdateManifestError::InvalidUrl: return "CIA URL must be valid HTTPS";
        case UpdateManifestError::InvalidHash: return "SHA-256 is invalid";
        case UpdateManifestError::InvalidKeyId: return "signing key ID is invalid";
        case UpdateManifestError::InvalidSignature: return "signature is invalid or missing";
    }
    return "manifest error";
}

const char* updatePolicyMessage(UpdatePolicy policy) noexcept {
    switch (policy) {
        case UpdatePolicy::UnsupportedNoTrustedKey:
            return "Updates disabled: production release key is not configured";
        case UpdatePolicy::RejectWrongKeyId: return "Update rejected: untrusted signing key ID";
        case UpdatePolicy::RejectWrongTitle: return "Update rejected: wrong title ID";
        case UpdatePolicy::RejectInsecureUrl: return "Update rejected: HTTPS is required";
        case UpdatePolicy::RejectCrossOrigin: return "Update rejected: CIA changed release origin";
        case UpdatePolicy::RejectBadSignature: return "Update rejected: Ed25519 signature failed";
        case UpdatePolicy::RejectNotNewer: return "Update rejected: version is not newer";
        case UpdatePolicy::RejectTooLarge: return "Update rejected: CIA exceeds staging limit";
        case UpdatePolicy::ReadyToStage: return "Signed update is ready to stage";
    }
    return "Update rejected";
}

Sha256::Sha256() noexcept
    : state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
             0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}

void Sha256::transform(const std::uint8_t block[64]) noexcept {
    std::uint32_t words[64]{};
    for (unsigned i = 0; i < 16; ++i) {
        words[i] = static_cast<std::uint32_t>(block[i * 4]) << 24 |
                   static_cast<std::uint32_t>(block[i * 4 + 1]) << 16 |
                   static_cast<std::uint32_t>(block[i * 4 + 2]) << 8 |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (unsigned i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^
                                 (words[i - 15] >> 3);
        const std::uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^
                                 (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    std::uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3];
    std::uint32_t e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for (unsigned i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choice + kShaConstants[i] + words[i];
        const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
    state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
}

void Sha256::update(const void* data, std::size_t size) noexcept {
    if (finished_ || size == 0) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    total_bytes_ += size;
    while (size != 0) {
        const std::size_t copied = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, copied);
        buffered_ += copied;
        bytes += copied;
        size -= copied;
        if (buffered_ == buffer_.size()) { transform(buffer_.data()); buffered_ = 0; }
    }
}

std::array<std::uint8_t, 32> Sha256::finish() noexcept {
    if (!finished_) {
        const std::uint64_t bit_length = total_bytes_ * 8;
        buffer_[buffered_++] = 0x80;
        if (buffered_ > 56) {
            std::fill(buffer_.begin() + buffered_, buffer_.end(), 0);
            transform(buffer_.data());
            buffered_ = 0;
        }
        std::fill(buffer_.begin() + buffered_, buffer_.begin() + 56, 0);
        for (unsigned i = 0; i < 8; ++i)
            buffer_[63 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
        transform(buffer_.data());
        finished_ = true;
    }
    std::array<std::uint8_t, 32> digest{};
    for (unsigned i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return digest;
}

UpdateArtifactVerifier::UpdateArtifactVerifier(
        std::uint64_t expected_size,
        const std::array<std::uint8_t, 32>& expected_hash) noexcept
    : expected_size_(expected_size), expected_hash_(expected_hash) {}

void UpdateArtifactVerifier::update(const void* data, std::size_t size) noexcept {
    if (size > std::numeric_limits<std::uint64_t>::max() - received_) {
        overflowed_ = true;
        return;
    }
    received_ += size;
    hash_.update(data, size);
}

UpdateArtifactStatus UpdateArtifactVerifier::finish() noexcept {
    if (overflowed_ || received_ != expected_size_) return UpdateArtifactStatus::SizeMismatch;
    return hash_.finish() == expected_hash_ ? UpdateArtifactStatus::Valid :
                                              UpdateArtifactStatus::HashMismatch;
}

StagedUpdatePolicy evaluateStagedUpdate(bool manifest_verified,
                                       bool artifact_verified,
                                       std::uint64_t manifest_title_id,
                                       std::uint32_t manifest_version,
                                       std::uint64_t package_title_id,
                                       std::uint32_t package_version,
                                       bool user_confirmed) noexcept {
    if (!manifest_verified || !artifact_verified) return StagedUpdatePolicy::RejectUnverified;
    if (manifest_title_id != package_title_id || manifest_version != package_version)
        return StagedUpdatePolicy::RejectPackageMetadata;
    return user_confirmed ? StagedUpdatePolicy::DeferredInstallReady :
                            StagedUpdatePolicy::AwaitingUserConfirmation;
}

std::string hexSha256(const std::array<std::uint8_t, 32>& digest) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = hex[digest[i] >> 4];
        result[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    return result;
}

std::string sha256Hex(std::string_view value) {
    Sha256 hash;
    hash.update(value.data(), value.size());
    return hexSha256(hash.finish());
}

} // namespace romm3ds