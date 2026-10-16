# PocketShelf

PocketShelf is a native Nintendo 3DS client for **RomM 5.4**, targeted at New Nintendo 3DS-family systems. It connects directly to RomM over a trusted LAN, pages and caches a bounded catalogue, browses cover art on the top screen, streams `.cia` files into AM, streams other supported ROM files to SD, and exchanges managed save files with RomM.

No bridge is required.

## Security warning

PocketShelf intentionally supports only plain `http://` RomM URLs. The bearer token is:

- sent unencrypted over the LAN on every authenticated RomM catalogue, media, user-action, save, and download request;
- stored unencrypted in `sdmc:/3ds/PocketShelf/settings.ini`;
- masked in PocketShelf's settings UI and never included in status messages or logs.

Use this only on a trusted, isolated LAN. HTTP provides no server authentication, confidentiality, or integrity. A hostile network peer can steal the token or replace downloaded content. Installing a CIA changes Home Menu title storage; install only software you trust and are legally entitled to use.

Executable self-updates do **not** use the RomM HTTP origin or bearer token. The update foundation requires a separately compiled HTTPS release origin, an exact trusted Ed25519 public key/key ID, a canonical signed manifest, same-origin CIA URL, strict version/title/size checks, streaming SHA-256, and staged CIA metadata verification. The repository intentionally leaves the production release key configuration disabled in `source/release_config.cpp`; see [Secure release updates](docs/secure-updates.md) for the exact signed bytes, threat model, staging/recovery policy, and GitHub release-key setup.

## Direct RomM 5.4 integration

The client uses the RomM 5.4 OpenAPI contract exposed by the configured server:

- Catalogue: `GET /api/roms`
- Pagination: `limit` and `offset`
- Required file data: `with_files=true`
- File content: `GET /api/roms/{file_id}/files/content/{file_name}`
- Save inventory: `GET /api/saves?rom_id={rom_id}`
- Save content: `GET /api/saves/{save_id}/content`
- Versioned save upload: `POST /api/saves?rom_id={rom_id}&slot=PocketShelf` with multipart field `saveFile`
- Cover content: the same-origin `path_cover_small` resource returned by RomM, falling back to `path_cover_large` when the small derivative is missing; RomM's `?ts=` cache-busting query is retained and URL-normalized
- Screenshot content: the first bounded same-ROM path from `merged_screenshots`
- User properties: `PUT /api/roms/{rom_id}/props`, including the documented `update_last_played` and `remove_last_played` query flags
- Authentication: `Authorization: Bearer ***`

The download path's `{id}` is the **RomM file ID**, not the ROM ID. PocketShelf percent-encodes the raw filename once and rejects redirects, unsafe filenames, path traversal, non-HTTP base URLs, and cover URLs outside the configured RomM origin.

Catalogue requests use pages of 12 ROMs and disable the large character, filter-value, and ROM-ID indexes. Each JSON page is capped at 768 KiB and parsed by a small purpose-built parser that retains only bounded fields needed by the UI and transfer code. Titles and other schema strings are capped at 450 bytes, summaries at 768 bytes, URLs at 512 bytes, metadata lists at eight retained values, screenshots at four paths, files at 16 per ROM page item, and cache records at 12 KiB. The client supports at most 512 cached file entries and a 2 MiB catalogue cache.

## Settings and filters

The bottom screen uses a persistent Universal-DB-style touch rail with four pages: **Details**, **Search & Filters**, **Sort**, and **Settings**. Tap a rail icon to switch immediately; Select and L/R are not required.

- **Details** shows a scrollable label/value metadata grid for platform, region, genres, developer, publisher, franchise, collection, RomM user state, file type, and downloaded/installed awareness. A separate Summary section is limited to six wrapped lines. Missing values say `Not supplied by RomM`; values skipped or shortened by PocketShelf's bounds are identified separately instead of looking like server omissions. The action-card positions and hitboxes remain fixed.
- **Search & Filters** provides touch cards for the persisted file type, session title search, and server-side RomM metadata filters. Metadata input accepts bounded `genre=`, `developer=`, `publisher=`, `franchise=`, and `collection=` terms, with multiple terms separated by semicolons.
- **Sort** toggles the loaded catalogue between title A-Z and Z-A without refetching.
- **Settings** provides direct URL, token, and connect/refresh cards. URL and token editing use the software keyboard.

The first-run base URL is `http://192.168.1.6:890`. A token is not compiled into the application.

Settings are atomically written to:

```text
sdmc:/3ds/PocketShelf/settings.ini
```

Version-1 settings containing `manifest_url=` are migrated to that URL's HTTP origin. The token remains empty and must be entered in-app.

## Catalogue and cover cache

- Normalized catalogue records are atomically cached at `sdmc:/3ds/PocketShelf/cache/catalog.ndjson`.
- The catalogue cache is capped at 512 entries and 2 MiB.
- A successful refresh replaces the cache; changing the URL, token, or file-type filter invalidates it.
- Encoded PNG/JPEG covers are cached under `sdmc:/3ds/PocketShelf/cache/covers/` using numeric ROM IDs only. Screenshots are bounded and displayed without entering the cover cache, so they cannot replace a cached cover.
- Cover downloads are capped at 512 KiB and 512x512 decoded pixels.
- Cover cache pruning retains at most 32 files and 8 MiB, removing oldest files first.
- Full ROM or CIA payloads are never accumulated in RAM.

## Save data

- PocketShelf now prefers **Checkpoint's local backup tree** at `sdmc:/3ds/Checkpoint/saves/`. It follows Checkpoint's actual 3DS layout: title directories begin with `0x%05X ` where the value is the low 32 bits of the title ID shifted right by eight, and each child directory is one Checkpoint backup.
- A RomM game is bound to an exact installed title ID. PocketShelf automatically uses its verified CIA-install receipt when available; otherwise **Y** opens a bounded picker containing only installed SD application titles with exactly one matching Checkpoint folder. The picker uses Checkpoint's folder label for display, retains AM title size/version metadata when `AM_GetTitleInfo` succeeds, supports touch or D-Pad selection, and requires explicit confirmation before atomically storing `sdmc:/3ds/PocketShelf/save-bindings/<rom_id>.titleid`. **X** inside the picker retains manual 16-digit entry as a fallback. ROM filename/title guessing is not used, and duplicate matching Checkpoint folders are rejected as ambiguous.
- Upload packages the user-selected Checkpoint backup into PocketShelf's deterministic `.pscp` container and sends it through RomM's versioned `PocketShelf` slot. Packaging is streamed with a 32 KiB buffer, rejects symlinks, traversal, more than 128 files, paths over 240 bytes, nesting over 16 levels, empty backups, and packages over **512 KiB**. The final bounded package is loaded for libctru `http:C`, whose raw POST API still requires a complete body.
- Download streams a matching `.pscp` file to a bounded staging file, validates the complete package and exact title ID before extraction, writes into a hidden temporary directory, and atomically renames it to a new `PocketShelf-YYYYMMDD-HHMMSS` backup under the bound Checkpoint title directory. It does **not** restore into the live title archive; the user reviews/restores the new backup with Checkpoint.
- The existing managed directory `sdmc:/3ds/PocketShelf/saves/<rom_id>/` remains the fallback when no Checkpoint title is bound. Managed downloads retain temporary-file, timestamped backup, and rollback behavior; managed uploads retain RomM versioning.
- Local inventory is bounded at 16 managed files or 64 Checkpoint backups; remote inventory is capped at 32 files and 128 KiB of JSON. Checkpoint mode displays every matching remote package for the exact bound title ID. The Save Data page shows bounded two-row local/remote windows; Left/Right changes the selected upload/download version and either visible row can be selected by touch.
- PocketShelf does not mutate `ARCHIVE_USER_SAVEDATA`, clear secure values, handle GBA VC/TWL saves, or broaden its filesystem permissions. Those operations remain deferred until separate on-hardware permission and recovery testing is complete.

## UI and controls

The top screen shows the selected cover or its first bounded RomM screenshot, plus the title and adjacent-title placeholders. The bottom screen is a persistent touch-page interface for details/actions, search/type/metadata filters, title sorting, and URL/token/connection settings. Media failures report whether RomM supplied no path, returned a non-200 status, returned a non-image body, or supplied bytes that failed decoding.

| Control | Action |
|---|---|
| Touch rail | Switch Details, Search & Filters, Sort, or Settings immediately |
| Touch page card | Activate transfer, Save Data, filter, sort, URL/token edit, or connect |
| D-Pad Left/Right | Previous/next cover; on Save Data, previous/next local or remote backup for the focused action |
| D-Pad Up/Down | Move focus between controls; move in the Type chooser or Checkpoint title picker |
| A | Activate the focused control; explicitly confirm the current chooser selection |
| B | Return to Details; cancel an active chooser or transfer |
| Y (Save Data) | Open the installed-title/Checkpoint binding picker |
| Y (Library) | Toggle RomM `now_playing`; enabling it also updates `last_played` |
| L / R (Library) | Cycle RomM play status / user rating |
| Select (Library) | Switch between cover and the first available screenshot |
| Select+Y / Select+L / Select+R / Select+X | Toggle backlog / cycle difficulty / advance completion by 10% / toggle last-played state |
| X | Refresh the catalogue; in the title picker, open manual title-ID fallback |
| Start | Exit |

Bottom-screen text now uses a larger, host-checked readable type scale throughout details, lists, cards, settings, save selection, and confirmation screens. Status messages wrap to two lines where the page allows it. Text wrapping, displayed rows, strings, page bodies, catalogue records, files per ROM, covers, and Citro2D render objects all have fixed ceilings. `C2D_Init` uses a 2,048-object budget, above the conservative host-tested worst-case frame estimate.

## Transfers

- `.cia` responses stream through one 32 KiB buffer into a bounded SD staging file. PocketShelf validates the CIA-header byte total using the actual bytes returned by each `httpcDownloadData` call and inspects the staged title ID before opening an AM import handle. A verified-new title uses `AM_StartCiaInstall`. An existing title shows its exact title ID and requires an explicit **A: OVERWRITE** confirmation; PocketShelf then reopens the complete staged CIA, rechecks that the title ID still matches the confirmed target, and only then calls `AM_StartCiaInstallOverwrite`. B cancels and removes staging before overwrite starts; write failures and cancellation abort the AM import rather than finishing it.
- Other allowlisted files stream through the same bounded buffer into type-specific SD directories.
- Partial writes are retried for AM, partial SD files are removed after failure/cancellation, and known lengths are verified.
- Known-size non-CIA downloads check free SD space before starting.
- Non-CIA presence is checked against the exact PocketShelf download path. Successful CIA installs record the CIA title ID and verify it against AM's installed SD title list; CIAs installed before this feature or outside PocketShelf remain explicitly `unknown` rather than guessed.
- HTTP 401, 403, 404, redirects, server failures, transport failures, malformed JSON, incomplete responses, unsafe filenames, and cache failures have separate safe status messages.
- The token is attached as a bearer header but is never interpolated into a URL or error message.

## Secure update staging

- `pocketshelf-update-v1.txt` is a fixed-order, LF-only, line-oriented manifest capped at 2,048 bytes. Ed25519 signs the exact canonical bytes from `format=` through the LF after `key_id=`; ambiguous JSON canonicalization is not used.
- PocketShelf vendors Monocypher 4.0.3's portable Ed25519 verifier and compiles the same implementation for host tests and devkitARM. Missing trust configuration, wrong key IDs, malformed or unsigned manifests, signature failure, replay/downgrade versions, wrong title IDs, non-HTTPS URLs, cross-origin URLs, oversized artifacts, and hash/size mismatches all fail closed before a deferred update can be retained.
- A valid CIA is streamed into a bounded SD temporary file, then its signed SHA-256/size and `AM_GetCiaFileInfo` title ID/version are checked. The user must explicitly confirm retaining it. PocketShelf stores only a deferred CIA plus its signed manifest under `sdmc:/3ds/PocketShelf/update/`; it does not overwrite or install the running PocketShelf title.
- The existing installed title is the recovery copy. Cancellation or verification/commit failure removes new temporary state. Self-replacement remains disabled until a separately reviewed installer can reverify immediately before installation and provide a tested rollback path.

## Supported file extensions

`cia`, `3ds`, `3dsx`, `nds`, `gba`, `gb`, `gbc`, `nes`, `smc`, `sfc`, `n64`, `z64`, `v64`, `bin`, `cue`, `iso`, `chd`, `pbp`, `gg`, `sms`, `gen`, `md`, `32x`, `pce`, `ws`, and `wsc`.

Only `.cia` is installable. Other types are downloads.

## Build and verification

Install devkitPro's `3ds-dev` group plus `3ds-libpng` and `3ds-libjpeg-turbo`, then run:

```sh
make clean-host
make host-test
make clean
make
make cia
make verify-cia
```

Outputs:

- `pocketshelf.3dsx`
- `pocketshelf.smdh`
- `pocketshelf.elf`
- `pocketshelf.cia`

Package version is **0.6.1** (encoded title version 97). `make verify-cia` checks the intended title ID, product code, encoded title version, main-thread priority, and container/section hashes using the project-local `ctrtool`.

The host suite covers direct RomM page parsing, save inventory/schema parsing, real save endpoint URLs, Unicode escapes, safe managed paths, backup collision handling, install receipts, downloaded-file awareness, pagination URL construction, correct file-ID downloads, single filename encoding, RomM 5.4 cache-busted cover URLs, external-cover rejection, settings/token/filter migration and bounds, atomic settings/cache persistence, path safety, partial writes, text wrapping, type selection, legacy parsers, the Citro2D object budget, canonical update signed-byte construction, real Ed25519 verification, tamper/wrong-key/replay/downgrade/title/URL rejection, artifact hash/size checks, and deferred staging policy.

## Development release checklist

The current **0.6.1** binaries are local development/test artifacts, not updater-compatible production releases. They have not been published by this repository, and they must not be attached to a GitHub release or advertised as production builds. Production updater trust remains intentionally disabled: `source/release_config.cpp` has no release origin, key ID, or Ed25519 public key, so these artifacts cannot satisfy the signed production-update path.

SHA-256 hashes for the current development artifacts built from commit `597c1b1e1e63b06e1005ec6fff08b2348d20426f`:

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

## Conservative scope and deferred work

This implementation deliberately keeps a bounded, working slice:

- It parses bounded JSON pages in RAM rather than implementing a fully streaming SAX parser. Pages are small and capped; the full catalogue response is never loaded as one giant document.
- It retains only bounded browsing fields: genres, developers, publishers, franchises, collections, four screenshot paths, and the documented RomM user properties used by the UI; other metadata is skipped. Oversized optional summaries and metadata values do not make a playable ROM disappear: the parser consumes them safely and records an omission/truncation marker for the Info page.
- It downloads and renders one selected cover or screenshot texture at a time, while only encoded cover files are cached on SD.
- The Settings page contains a complete secure verification/staging path, but the checked-in production trust configuration is intentionally disabled. Public releases must first configure a reviewed HTTPS origin and Ed25519 public key/key ID in `source/release_config.cpp`; no private key belongs in the repository. Even after configuration, PocketShelf only stages and records a user-confirmed deferred update. Executable self-replacement remains intentionally disabled pending a tested rollback-capable installer.
- It does not support WebP covers, resumable transfers, archive-member selection, multi-file ZIP construction, save states, automatic emulator save-path mapping, native 3DS save archive extraction/injection, or lid-closed/background transfers. RomM 5.4 represents game favorites through a favorite collection rather than a `rom_user` favorite field; collection creation/membership mutation is deliberately deferred instead of inventing a property action.
- Build and package verification do not claim hardware runtime testing. HTTP service behavior, AM permissions, Home Menu installation, cancellation timing, Wi-Fi behavior, and visual layout still require testing on a supported New Nintendo 3DS-family console.
