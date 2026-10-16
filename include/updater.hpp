#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace romm3ds {

inline constexpr std::size_t kMaxUpdateManifestBytes = 2048;
inline constexpr std::size_t kMaxUpdateUrlBytes = 512;
inline constexpr std::size_t kMaxUpdateVersionNameBytes = 31;
inline constexpr std::size_t kMaxUpdateKeyIdBytes = 63;
inline constexpr std::uint64_t kMaxUpdateCiaBytes = 64ULL * 1024ULL * 1024ULL;

struct UpdateManifest {
    std::uint32_t version = 0;
    std::string version_name;
    std::uint64_t title_id = 0;
    std::uint64_t size = 0;
    std::string cia_url;
    std::array<std::uint8_t, 32> sha256{};
    std::string key_id;
    std::vector<std::uint8_t> signature;
};

struct TrustedUpdateKey {
    bool configured = false;
    std::string key_id;
    std::array<std::uint8_t, 32> public_key{};
};

enum class UpdateManifestError {
    None,
    TooLarge,
    MissingField,
    DuplicateField,
    UnknownField,
    InvalidFormat,
    NonCanonical,
    InvalidNumber,
    InvalidVersionName,
    InvalidTitleId,
    InvalidUrl,
    InvalidHash,
    InvalidKeyId,
    InvalidSignature,
};

enum class UpdateSignatureStatus {
    Valid,
    NoTrustedKey,
    WrongKeyId,
    Invalid,
};

enum class UpdatePolicy {
    UnsupportedNoTrustedKey,
    RejectWrongKeyId,
    RejectWrongTitle,
    RejectInsecureUrl,
    RejectCrossOrigin,
    RejectBadSignature,
    RejectNotNewer,
    RejectTooLarge,
    ReadyToStage,
};

enum class UpdateArtifactStatus {
    Valid,
    SizeMismatch,
    HashMismatch,
};

enum class StagedUpdatePolicy {
    RejectUnverified,
    RejectPackageMetadata,
    AwaitingUserConfirmation,
    DeferredInstallReady,
};

bool parseUpdateManifest(std::string_view contents, UpdateManifest& output,
                         UpdateManifestError& error);
std::string updateManifestSignedBytes(const UpdateManifest& manifest);
UpdateSignatureStatus verifyUpdateManifestSignature(const UpdateManifest& manifest,
                                                    const TrustedUpdateKey& trusted_key) noexcept;
UpdatePolicy evaluateUpdatePolicy(const UpdateManifest& manifest,
                                  std::uint32_t current_version,
                                  std::uint64_t expected_title_id,
                                  std::string_view manifest_url,
                                  const TrustedUpdateKey& trusted_key) noexcept;
std::string updateManifestUrl(std::string_view release_base_url);
const char* updateManifestErrorMessage(UpdateManifestError error) noexcept;
const char* updatePolicyMessage(UpdatePolicy policy) noexcept;

class Sha256 {
public:
    Sha256() noexcept;
    void update(const void* data, std::size_t size) noexcept;
    std::array<std::uint8_t, 32> finish() noexcept;

private:
    void transform(const std::uint8_t block[64]) noexcept;
    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

class UpdateArtifactVerifier {
public:
    UpdateArtifactVerifier(std::uint64_t expected_size,
                           const std::array<std::uint8_t, 32>& expected_hash) noexcept;
    void update(const void* data, std::size_t size) noexcept;
    UpdateArtifactStatus finish() noexcept;
    std::uint64_t received() const noexcept { return received_; }

private:
    std::uint64_t expected_size_ = 0;
    std::array<std::uint8_t, 32> expected_hash_{};
    Sha256 hash_;
    std::uint64_t received_ = 0;
    bool overflowed_ = false;
};

StagedUpdatePolicy evaluateStagedUpdate(bool manifest_verified,
                                       bool artifact_verified,
                                       std::uint64_t manifest_title_id,
                                       std::uint32_t manifest_version,
                                       std::uint64_t package_title_id,
                                       std::uint32_t package_version,
                                       bool user_confirmed) noexcept;

std::string hexSha256(const std::array<std::uint8_t, 32>& digest);
std::string sha256Hex(std::string_view value);

} // namespace romm3ds
