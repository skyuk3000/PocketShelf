# PocketShelf

PocketShelf is a native Nintendo 3DS client for **RomM 5.4**. It lets you browse your RomM library, view covers and screenshots, download supported game files, install CIA files with confirmation, and exchange managed save backups from a New Nintendo 3DS-family system.

No bridge service is required: PocketShelf connects directly to RomM on your local network.

> **Development status:** PocketShelf 0.6.1 is source-only development software. This repository does not currently publish release binaries. Hardware testing is still required before a production release, and automatic production updates are intentionally disabled.

## What it can do

- Browse a paginated RomM 5.4 catalogue with covers, screenshots, search, filters, and title sorting.
- Download supported ROM and media files to the SD card without loading the full file into memory.
- Stage and install new CIA titles, with explicit confirmation before overwriting an existing title.
- Track downloaded files and PocketShelf-installed CIA titles.
- Upload and download save backups through RomM.
- Work with Checkpoint backup folders, while keeping PocketShelf's managed-save directory as a fallback.
- Cache a bounded catalogue and cover set for faster browsing.

PocketShelf targets **New Nintendo 3DS-family systems**. Older 3DS-family hardware is not the current target.

## Important trusted-LAN warning

PocketShelf supports only plain `http://` RomM server URLs. Your RomM bearer token is sent unencrypted with authenticated requests and is stored unencrypted in:

```text
sdmc:/3ds/PocketShelf/settings.ini
```

Use PocketShelf only on a **trusted, isolated LAN**. HTTP does not authenticate the server or protect the token and downloaded content from another device on the network. A hostile network peer could steal the token or replace a download.

Installing a CIA changes Home Menu title storage. Install only software you trust and are legally entitled to use.

## Requirements

- A New Nintendo 3DS-family system set up to run homebrew or install CIA packages.
- A reachable **RomM 5.4** server on the same trusted LAN.
- A RomM bearer token entered on the Settings page.
- For building from source: devkitPro's `3ds-dev` group, `3ds-libpng`, and `3ds-libjpeg-turbo`.

## Build and install

There are no official downloadable builds yet. To create a development build from source:

```sh
make clean-host
make host-test
make clean
make
make cia
make verify-cia
```

Build outputs include:

- `pocketshelf.3dsx` for the Homebrew Launcher
- `pocketshelf.smdh` metadata for the homebrew build
- `pocketshelf.cia` for installation with a trusted CIA installer
- `pocketshelf.elf` development output

Use the normal placement required by your Homebrew Launcher for the `.3dsx` and `.smdh` files, or install your locally verified `.cia` with your preferred trusted installer. No binary from this repository should currently be treated as a production release.

## First connection

1. Start PocketShelf and open **Settings** from the bottom-screen rail.
2. Set the RomM base URL. The initial value is `http://192.168.1.6:890`.
3. Enter your RomM bearer token.
4. Choose **Connect/Refresh**.

The token is masked in the interface, but it is not encrypted on disk or on the network.

## Saves

PocketShelf prefers Checkpoint's 3DS backup tree at:

```text
sdmc:/3ds/Checkpoint/saves/
```

For a RomM game, you can bind an installed title that has a matching Checkpoint folder. PocketShelf packages the selected backup for upload and places downloaded backups into a new Checkpoint backup directory for review and restoration in Checkpoint. It does **not** restore directly into a game's live save archive.

When no Checkpoint title is bound, PocketShelf uses its managed-save fallback under:

```text
sdmc:/3ds/PocketShelf/saves/<rom_id>/
```

See [Save synchronization architecture](docs/save-sync-architecture.md) for the full safety model and format details.

## Supported file formats

CIA is the only directly installable format. Other supported formats are downloaded to the SD card:

`cia`, `3ds`, `3dsx`, `nds`, `gba`, `gb`, `gbc`, `nes`, `smc`, `sfc`, `n64`, `z64`, `v64`, `bin`, `cue`, `iso`, `chd`, `pbp`, `gg`, `sms`, `gen`, `md`, `32x`, `pce`, `ws`, and `wsc`.

## Basic controls

| Control | Action |
|---|---|
| Touch bottom rail | Switch between Details, Search & Filters, Sort, and Settings |
| D-Pad | Browse games and move between controls or backup choices |
| A | Activate or confirm |
| B | Go back or cancel |
| X | Refresh the catalogue; opens manual title-ID entry in the save-title picker |
| Y | Open save-title binding on the Save Data page; toggle `now_playing` in the library |
| L / R | Change play status or rating in the library |
| Select | Switch between cover and screenshot in the library |
| Start | Exit |

CIA overwrite, update staging, and other destructive or security-sensitive actions require separate confirmation.

## Development release checklist

The current **0.6.1** binaries are local development/test artifacts, not updater-compatible production releases. They have not been published by this repository, and they must not be attached to a GitHub release or advertised as production builds. Production updater trust remains intentionally disabled: `source/release_config.cpp` has no release origin, key ID, or Ed25519 public key.

SHA-256 hashes for the current local development artifacts built from commit `597c1b1e1e63b06e1005ec6fff08b2348d20426f`:

| Artifact | SHA-256 |
|---|---|
| `pocketshelf.3dsx` | `d8d8f4f5144236273dbeb32f6e7fc32fa3bd168298ae4c7f321b18820967e43c` |
| `pocketshelf.cia` | `c802f8258bd55ec8b7a5a538cb516317c7b2a40d7c0be18aa6a0c07af488f3b0` |
| `pocketshelf.elf` | `dec6cdc25d7f859e91b84b8029655a706c4240eeecdc75e9b88c259600f27713` |
| `pocketshelf.smdh` | `10990f6fbb69e8aede1a94119a7e7a32727f507b100ec2cea79ce51f81678dca` |

Before considering a future production release:

- [ ] Run `make clean-host && make host-test` on a standard host toolchain.
- [ ] Rebuild with the official devkitPro 3DS toolchain, then run `make verify-cia`; record hashes from that clean build rather than reusing the development hashes above.
- [ ] Test both `.3dsx` launch and `.cia` installation on a supported New Nintendo 3DS-family console.
- [ ] Exercise first-run settings, authenticated catalogue pagination, search/filter/sort, covers/screenshots, downloads, CIA new-install/explicit-overwrite/cancellation, and Checkpoint/managed-save selection and transfer.
- [ ] Test network interruption, malformed/oversized responses, cancellation cleanup, sleep/Home Menu transitions, and low/free-space failures without leaving partial files or open installs.
- [ ] Confirm the updater remains unavailable and fails closed while production trust is disabled. Enabling it requires a separate reviewed change with an HTTPS release origin, public key/key ID, signed canonical manifest, staged-CIA verification, and rollback/recovery testing; never commit the private signing key.
- [ ] Publish nothing until the clean build, package inspection, hardware results, release notes, and final artifact hashes have been independently reviewed.

## Technical documentation

- [Developer notes](docs/developer-notes.md): RomM endpoints, limits, cache behavior, controls, transfer behavior, host-test coverage, and deferred work.
- [Save synchronization architecture](docs/save-sync-architecture.md): Checkpoint and managed-save formats, binding, validation, and recovery behavior.
- [Secure release updates](docs/secure-updates.md): signed-manifest format, threat model, staging, key rotation, and recovery policy.

The updater currently verifies and stages only when a separately reviewed production trust configuration exists. The checked-in build has no production release origin or trusted release key, and it cannot self-replace the running title.
