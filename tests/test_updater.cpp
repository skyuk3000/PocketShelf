#include "updater.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

constexpr std::uint64_t kPocketShelfTitleId = 0x0004000000F3D500ULL;
constexpr std::array<std::uint8_t, 32> kTestPublicKey = {
    0x6a,0x74,0x06,0xc6,0xd5,0x56,0xb7,0x6c,0x05,0x2e,0xb6,0xa4,0xc4,0x8e,0xfc,0x54,
    0x7c,0x4e,0x85,0x28,0x36,0x97,0xeb,0xdf,0xc9,0x2b,0x02,0x86,0x15,0x97,0x70,0xe6,
};
constexpr char kTestSignature[] =
    "dcf2c92c6f4b4a8a2a85244264447cf30d2c4ac3dc5b9d00e76a6118e84761da"
    "48248f7da863dc4b3977848e71a9bf8b9283c3b49fe7800b8fa2d8215426aa01";

std::string signedBytes() {
    return "format=pocketshelf-update-v1\n"
           "version=98\n"
           "version_name=0.6.2\n"
           "title_id=0004000000F3D500\n"
           "size=123456\n"
           "cia_url=https://updates.example/releases/pocketshelf.cia\n"
           "sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n"
           "key_id=pocketshelf-test-1\n";
}

std::string manifest(std::string signature = kTestSignature) {
    return signedBytes() + "signature=" + signature + "\n";
}

romm3ds::TrustedUpdateKey testKey(std::string key_id = "pocketshelf-test-1") {
    return romm3ds::TrustedUpdateKey{true, std::move(key_id), kTestPublicKey};
}

romm3ds::UpdateManifest parse(std::string text = manifest()) {
    romm3ds::UpdateManifest parsed;
    romm3ds::UpdateManifestError error = romm3ds::UpdateManifestError::None;
    require(romm3ds::parseUpdateManifest(text, parsed, error), "fixture manifest should parse");
    return parsed;
}

void test_manifest_signed_bytes_are_exact_and_canonical() {
    const auto parsed = parse();
    require(romm3ds::updateManifestSignedBytes(parsed) == signedBytes(),
            "signed bytes must be the fixed ordered fields through key_id with LF endings");

    auto crlf = manifest();
    for (std::size_t at = 0; (at = crlf.find('\n', at)) != std::string::npos; at += 2)
        crlf.replace(at, 1, "\r\n");
    romm3ds::UpdateManifest ignored;
    romm3ds::UpdateManifestError error = romm3ds::UpdateManifestError::None;
    require(!romm3ds::parseUpdateManifest(crlf, ignored, error) &&
                error == romm3ds::UpdateManifestError::NonCanonical,
            "CRLF manifests must fail rather than changing signed-byte interpretation");

    std::string reordered = manifest();
    const std::string first = "version=98\nversion_name=0.6.2\n";
    const std::string swapped = "version_name=0.6.2\nversion=98\n";
    reordered.replace(reordered.find(first), first.size(), swapped);
    require(!romm3ds::parseUpdateManifest(reordered, ignored, error) &&
                error == romm3ds::UpdateManifestError::NonCanonical,
            "field order is part of the canonical format");
}

void test_real_ed25519_signature_verification_fails_closed() {
    const auto parsed = parse();
    require(romm3ds::verifyUpdateManifestSignature(parsed, testKey()) ==
                romm3ds::UpdateSignatureStatus::Valid,
            "known Ed25519 signature should verify with the exact test public key");
    require(romm3ds::verifyUpdateManifestSignature(parsed, testKey("wrong-key")) ==
                romm3ds::UpdateSignatureStatus::WrongKeyId,
            "wrong key ID must fail before accepting a signature");

    auto bad_signature = parsed;
    bad_signature.signature[0] ^= 1;
    require(romm3ds::verifyUpdateManifestSignature(bad_signature, testKey()) ==
                romm3ds::UpdateSignatureStatus::Invalid,
            "changed signature must fail verification");

    auto tampered = parsed;
    ++tampered.size;
    require(romm3ds::verifyUpdateManifestSignature(tampered, testKey()) ==
                romm3ds::UpdateSignatureStatus::Invalid,
            "changing any signed field must invalidate the signature");

    auto no_key = testKey();
    no_key.configured = false;
    require(romm3ds::verifyUpdateManifestSignature(parsed, no_key) ==
                romm3ds::UpdateSignatureStatus::NoTrustedKey,
            "an unconfigured production key must fail closed");
}

void test_manifest_parser_rejects_unsigned_malformed_and_unbounded_input() {
    romm3ds::UpdateManifest parsed;
    romm3ds::UpdateManifestError error = romm3ds::UpdateManifestError::None;
    require(!romm3ds::parseUpdateManifest(manifest(""), parsed, error) &&
                error == romm3ds::UpdateManifestError::InvalidSignature,
            "unsigned update metadata must be rejected");
    require(!romm3ds::parseUpdateManifest(manifest() + "extra=unsafe\n", parsed, error) &&
                error == romm3ds::UpdateManifestError::NonCanonical,
            "trailing fields must fail closed");
    require(!romm3ds::parseUpdateManifest(std::string(romm3ds::kMaxUpdateManifestBytes + 1, 'x'),
                                          parsed, error) &&
                error == romm3ds::UpdateManifestError::TooLarge,
            "manifest byte ceiling must be enforced before parsing");

    auto leading_zero = manifest();
    leading_zero.replace(leading_zero.find("version=98"), 10, "version=098");
    require(!romm3ds::parseUpdateManifest(leading_zero, parsed, error) &&
                error == romm3ds::UpdateManifestError::NonCanonical,
            "non-canonical decimal numbers must be rejected");
}

void test_verified_policy_rejects_downgrade_title_and_url_changes() {
    auto parsed = parse();
    const auto key = testKey();
    require(romm3ds::evaluateUpdatePolicy(parsed, 97, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::ReadyToStage,
            "valid newer signed update should be stageable");
    require(romm3ds::evaluateUpdatePolicy(parsed, 98, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectNotNewer,
            "equal version replay must be rejected");
    require(romm3ds::evaluateUpdatePolicy(parsed, 99, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectNotNewer,
            "downgrade must be rejected");

    parsed = parse(); parsed.title_id++;
    require(romm3ds::evaluateUpdatePolicy(parsed, 97, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectWrongTitle,
            "wrong title ID must be rejected");
    parsed = parse(); parsed.cia_url = "http://updates.example/releases/pocketshelf.cia";
    require(romm3ds::evaluateUpdatePolicy(parsed, 97, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectInsecureUrl,
            "plain HTTP executable updates must be rejected");
    parsed = parse(); parsed.cia_url = "https://cdn.example/pocketshelf.cia";
    require(romm3ds::evaluateUpdatePolicy(parsed, 97, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectCrossOrigin,
            "cross-origin executable update must be rejected");
    parsed = parse(); parsed.size = romm3ds::kMaxUpdateCiaBytes + 1;
    require(romm3ds::evaluateUpdatePolicy(parsed, 97, kPocketShelfTitleId,
                "https://updates.example/pocketshelf-update-v1.txt", key) ==
                romm3ds::UpdatePolicy::RejectTooLarge,
            "signed CIA size must remain within the fixed staging ceiling");
}

void test_staged_artifact_requires_exact_size_hash_metadata_and_confirmation() {
    const auto parsed = parse();
    romm3ds::UpdateArtifactVerifier artifact(parsed.size, parsed.sha256);
    artifact.update("abc", 3);
    require(artifact.finish() == romm3ds::UpdateArtifactStatus::SizeMismatch,
            "short staged update must fail its signed size");

    romm3ds::UpdateArtifactVerifier valid(3, parsed.sha256);
    valid.update("a", 1); valid.update("bc", 2);
    require(valid.finish() == romm3ds::UpdateArtifactStatus::Valid,
            "streamed artifact should verify exact bytes and SHA-256");

    auto wrong_hash = parsed.sha256; wrong_hash[0] ^= 1;
    romm3ds::UpdateArtifactVerifier bad_hash(3, wrong_hash);
    bad_hash.update("abc", 3);
    require(bad_hash.finish() == romm3ds::UpdateArtifactStatus::HashMismatch,
            "hash mismatch must fail staging");

    require(romm3ds::evaluateStagedUpdate(true, true, kPocketShelfTitleId, 98,
                                          kPocketShelfTitleId, 98, false) ==
                romm3ds::StagedUpdatePolicy::AwaitingUserConfirmation,
            "verified staged artifact must still require explicit confirmation");
    require(romm3ds::evaluateStagedUpdate(true, true, kPocketShelfTitleId, 98,
                                          kPocketShelfTitleId, 98, true) ==
                romm3ds::StagedUpdatePolicy::DeferredInstallReady,
            "confirmation creates only a deferred-install state");
    require(romm3ds::evaluateStagedUpdate(true, true, kPocketShelfTitleId, 98,
                                          kPocketShelfTitleId, 99, true) ==
                romm3ds::StagedUpdatePolicy::RejectPackageMetadata,
            "CIA package version must match the signed manifest");
    require(romm3ds::evaluateStagedUpdate(true, true, kPocketShelfTitleId, 98,
                                          kPocketShelfTitleId + 1, 98, true) ==
                romm3ds::StagedUpdatePolicy::RejectPackageMetadata,
            "CIA package title ID must match the signed manifest");
}

void test_hash_foundation_matches_sha256_vectors() {
    require(romm3ds::sha256Hex("") ==
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty SHA-256 vector should match");
    romm3ds::Sha256 hash;
    hash.update("a", 1); hash.update("bc", 2);
    require(romm3ds::hexSha256(hash.finish()) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "streaming SHA-256 vector should match");
}
} // namespace

int main() {
    test_manifest_signed_bytes_are_exact_and_canonical();
    test_real_ed25519_signature_verification_fails_closed();
    test_manifest_parser_rejects_unsigned_malformed_and_unbounded_input();
    test_verified_policy_rejects_downgrade_title_and_url_changes();
    test_staged_artifact_requires_exact_size_hash_metadata_and_confirmation();
    test_hash_foundation_matches_sha256_vectors();
    std::cout << "All updater tests passed\n";
    return 0;
}
