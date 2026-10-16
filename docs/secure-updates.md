# Secure release updates

PocketShelf uses a compact, canonical, line-oriented manifest so the Nintendo 3DS client can parse and authenticate releases with fixed memory ceilings. This is separate from RomM access: RomM remains an explicitly trusted-LAN `http://` connection, while executable updates require a separately compiled `https://` release origin.

## Manifest format

The manifest filename is `pocketshelf-update-v1.txt`. It is at most 2,048 bytes and contains exactly these nine lines in this order, with ASCII `=` separators and LF (`0A`) endings, including the final line ending:

```text
format=pocketshelf-update-v1
version=98
version_name=0.6.2
title_id=0004000000F3D500
size=123456
cia_url=https://updates.example/releases/pocketshelf.cia
sha256=<64 lowercase hexadecimal characters>
key_id=<release key identifier>
signature=<128 lowercase hexadecimal characters>
```

Canonical rules:

- no blank lines, comments, CRLF, duplicate, omitted, reordered, or extra fields;
- `version` and `size` are unsigned decimal without a sign or leading zeroes;
- `title_id` is exactly 16 uppercase hexadecimal characters;
- `sha256` and `signature` are lowercase hexadecimal;
- `version_name` contains only ASCII letters, digits, `.`, `-`, and `+` and is at most 31 bytes;
- `key_id` contains only ASCII letters, digits, `-`, and `_` and is at most 63 bytes;
- `cia_url` is a bounded HTTPS URL without whitespace, control characters, or a fragment.

The Ed25519 signature covers the exact UTF-8/ASCII bytes of the first eight canonical lines, from `format=` through the LF after `key_id=`. The `signature=` line is not signed. PocketShelf reconstructs those canonical bytes from the parsed fields and verifies them with the exact compiled key ID and 32-byte public key.

## Validation and staging sequence

PocketShelf fails closed unless every step succeeds:

1. A production release origin and trusted Ed25519 public key are compiled into `source/release_config.cpp` through the declarations in `include/release_config.hpp`.
2. The manifest is fetched over HTTPS without the RomM bearer token. Normal certificate validation remains enabled; there is no self-signed-certificate or ignore-TLS-errors mode.
3. The manifest is canonical, bounded, signed by the exact trusted key ID, targets PocketShelf title ID `0004000000F3D500`, names a strictly newer encoded title version, stays under the 64 MiB staging ceiling, and points to an HTTPS CIA on the same origin as the manifest.
4. The CIA is streamed to `sdmc:/3ds/PocketShelf/update/pocketshelf.cia.tmp`; it is never accumulated in RAM. PocketShelf enforces the signed byte count while computing SHA-256.
5. The completed temporary file must match the signed size and SHA-256. `AM_GetCiaFileInfo` must then report the exact signed title ID and encoded title version.
6. The user must explicitly press A to retain the verified artifact. B deletes it.
7. Only then are the CIA and its signed manifest committed as the deferred state:
   - `sdmc:/3ds/PocketShelf/update/pocketshelf.cia`
   - `sdmc:/3ds/PocketShelf/update/pocketshelf-update-v1.txt`

PocketShelf does **not** install or overwrite its running title in this foundation. The currently installed application remains the rollback copy. If staging is interrupted, cancelled, malformed, mismatched, or cannot commit both files, temporary/new staged state is removed. Recovery is manual: delete the update directory or reinstall a trusted CIA by the normal console workflow. A later installer design must reverify the saved manifest, signature, artifact hash/size, and CIA metadata immediately before installation and must define a tested rollback mechanism before self-replacement is enabled.

## Threat model

This foundation is intended to resist a hostile LAN/Wi-Fi peer, compromised HTTP path, mirror tampering, wrong-title substitution, truncated or extended payloads, cross-origin URL substitution, replay of the current release, and downgrade to an older encoded title version. It does not protect against compromise of the offline release private key, malicious source/build infrastructure, vulnerabilities in the application or cryptographic implementation, or an already-compromised console.

The release signature is the executable authenticity boundary. SHA-256 alone is not authentication. HTTPS is additionally required to reduce metadata exposure and transport manipulation, but PocketShelf still verifies the release signature and payload digest independently.

## Production release-key setup

The repository intentionally ships with `kConfigured = false`, an empty release origin/key ID, and no trusted production public key in `source/release_config.cpp`. Test coverage contains only a clearly marked public test key and a precomputed signature; no test or production private key is stored.

Before enabling public releases:

1. Generate an Ed25519 key pair in an offline or dedicated signing environment. For example, OpenSSL can create one with `openssl genpkey -algorithm Ed25519`. Protect the private PEM as a release secret; never commit it, print it in CI logs, copy it to the console, or place it in the release archive.
2. Export and independently verify the 32-byte public key. Set a stable key ID, the HTTPS release base URL, the public key bytes, and `kConfigured = true` in `source/release_config.cpp` through a reviewed release-only change.
3. Produce the final CIA first. Read its encoded title version and title ID with the existing package verifier, then compute its exact byte count and SHA-256.
4. Write the first eight manifest lines canonically with those exact values. Sign those bytes with Ed25519, append the lowercase-hex `signature=` line, and independently verify the signature with the compiled public key before publishing.
5. Publish the manifest and CIA on the same stable HTTPS origin with no redirects. GitHub release asset downloads commonly redirect to another host, so they do not satisfy this strict same-origin policy by themselves. A GitHub Pages/custom-domain release directory or another static origin that serves both files directly is suitable.
6. Run the full host, devkitPro, and CIA verification gates and publish fresh hashes of the shipped artifacts. Hardware behavior remains a separate validation step.

## Rotation and recovery

The current foundation trusts one key. Planned rotation should be performed before retiring the old key: publish an old-key-signed PocketShelf release that embeds the next public key/key ID, require users to install that transition release, and only then sign later manifests with the new key. Do not silently replace a key under the same key ID.

If the active private key is lost, continue using the installed trust anchor until a deliberately signed transition can be made. If it is suspected compromised, there is no safe in-band recovery from that key: disable release publishing, disclose the affected key ID, and require a manually obtained/reinstalled build containing a new trusted public key. A server-side key change alone cannot restore trust to existing clients.

## Vendored verifier

`third_party/monocypher/` contains Monocypher 4.0.3's official C sources, including its optional Ed25519/SHA-512 implementation. The files retain their upstream BSD-2-Clause OR CC0-1.0 license headers. Both host tests and the devkitARM build compile the same verifier sources.
