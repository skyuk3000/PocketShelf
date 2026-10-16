# PocketShelf next-generation save sync

Status: Checkpoint-folder sync is implemented as the safe device-facing path. Native title archive access remains disabled.

## Verified platform facts

### Installed CIA titles

- PocketShelf already obtains exact title IDs for CIAs that it installs with `AM_GetCiaFileInfo`, stores a receipt keyed by the RomM file ID, and verifies the receipt against `AM_GetTitleList(MEDIATYPE_SD, ...)`. This is a sound binding. A title name, product code, or ROM filename is not a safe substitute for the receipt because none is guaranteed to identify the corresponding RomM ROM.
- libctru exposes installed-title enumeration through `AM_GetTitleCount`, `AM_GetTitleList`, and `AM_GetTitleInfo`.
- A regular CTR user save is opened as `ARCHIVE_USER_SAVEDATA` with a 12-byte binary archive path containing `{mediaType, titleIdLow, titleIdHigh}`. Checkpoint uses exactly this form.
- Save archive writes are journaled. A restore must call `FSUSER_ControlArchive(..., ARCHIVE_ACTION_COMMIT_SAVE_DATA, ...)`. Checkpoint then deletes the title secure value with `FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, ...)` so a restored older save is not rejected as a rollback.
- These APIs existing does **not** mean the current PocketShelf package may use them. `packaging/pocketshelf.rsf` grants only `DirectSdmc`. Checkpoint requests broad filesystem-tool categories in its exheader. PocketShelf must treat cross-title archive access as unavailable until an on-device read-only probe succeeds under an intentionally reviewed package profile.
- GBA Virtual Console saves are not ordinary CTR save archives in Checkpoint: its implementation steals a PxiFS0 session, accesses FSPXI, and recalculates a console-specific CMAC. PocketShelf must not claim GBA VC support.
- DSiWare saves live under TWL NAND paths and require permissions PocketShelf does not have. They are out of scope.

### RetroArch on 3DS

RetroArch's current 3DS frontend sets:

- config: `sdmc:/retroarch/retroarch.cfg`
- default battery-save root: `sdmc:/retroarch/cores/savefiles`
- default save-state root: `sdmc:/retroarch/cores/savestates`
- ordinary battery-save suffix: `.srm`

The effective battery-save path can be redirected by these config keys:

- `savefile_directory`
- `savefiles_in_content_dir`
- `sort_savefiles_enable` (append the runtime core `library_name`)
- `sort_savefiles_by_content_enable` (append the content parent-directory name before the core name)

PocketShelf must parse the installed config rather than assume defaults. If per-core sorting is enabled, it must use an observed RetroArch runtime `library_name`; it must not guess this from the `.cia` or core filename. Core-specific auxiliary files, multiple memory files, and save states are separate future adapters, not implied by locating the ordinary `.srm` file.

## Capability matrix

| Source | Discover | Export | Import | Current decision |
|---|---:|---:|---:|---|
| PocketShelf managed SD directory | yes | yes | yes | Retained as fallback/staging |
| Checkpoint local backup folders | yes, exact Checkpoint layout + title-ID binding | yes, deterministic bounded `.pscp` package | yes, as a new validated Checkpoint backup folder | Implemented preferred path |
| RetroArch ordinary `.srm` on SD | yes, from config + ROM path + optional runtime core name | safe after file stability check | safe with backup + atomic same-directory replace | Parser/path foundation retained; not wired to UI |
| CTR CIA user-save archive | exact title ID only; archive access must be probed | potentially available with reviewed FS permissions | destructive/non-atomic and requires commit + secure-value handling | Read-only probe first; no current implementation |
| GBA VC | title can be listed, save format/access is special | unsupported | unsupported | Do not expose |
| DSiWare/TWL | title can be listed, save is in TWL NAND | unsupported | unsupported | Do not expose |
| extdata/shared extdata | separate archive class and semantics | deferred | deferred | Do not conflate with title save data |

## Architecture

### 1. Explicit bindings, never filename guesses

Persist one binding per RomM ROM under `sdmc:/3ds/PocketShelf/save-bindings/`:

```text
rom_id
provider = managed | retroarch_srm | ctr_user_savedata
content_path = sdmc:/...             # RetroArch only
auto_core_library_name = ...         # RetroArch only; observed, not guessed
title_id = 00040000XXXXXXXX          # CTR only; from PocketShelf install receipt/manual verified selection
media = sd                            # CTR only
```

Bindings are created from:

1. a PocketShelf CIA install receipt;
2. an explicit user selection from AM's installed-title list; or
3. an explicit RetroArch ROM path plus the parsed config and, when required, runtime core library name.

Never auto-bind by display title, product code, ROM basename, or fuzzy matching.

### 2. Provider boundary

Introduce a platform-neutral orchestration layer and platform adapters:

```cpp
enum class SaveProviderKind { ManagedFile, RetroArchSrm, CtrUserSaveData };

struct SaveObjectInfo {
    std::string logical_name;
    std::uint64_t size;
    std::int64_t modified;
};

class SaveProvider {
public:
    virtual ProbeResult probe(const SaveBinding&) = 0;
    virtual Result enumerate(const SaveBinding&, EntrySink&) = 0;
    virtual Result exportTo(const SaveBinding&, PackageWriter&, ProgressSink&) = 0;
    virtual Result importFrom(const SaveBinding&, PackageReader&, ImportJournal&, ProgressSink&) = 0;
};
```

Concrete files proposed:

- `include/save_sources.hpp`, `source/save_sources.cpp`: bounded RetroArch config parsing/path resolution (started in this slice).
- `include/save_binding.hpp`, `source/save_binding.cpp`: versioned, bounded binding parser and atomic persistence.
- `include/save_provider.hpp`: provider/result interfaces without libctru types.
- `source/save_provider_sd.cpp`: managed file and RetroArch `.srm` provider.
- `source/save_provider_ctr.cpp`: libctru-only, initially read-only probe/export.
- `include/save_package.hpp`, `source/save_package.cpp`: deterministic package manifest, bounded entries/paths/total size, streaming hashes.
- `include/save_transaction.hpp`, `source/save_transaction.cpp`: backup, journal, stage, verify, apply, rollback.
- `source/save_transport.cpp`: upload/download streams; no provider owns HTTP.

### 3. Export format

Checkpoint backups are represented by PocketShelf's deterministic `.pscp` container so the multi-file tree remains compatible with RomM's single-file save endpoint. The binary format contains:

- magic and format version;
- exact 64-bit title ID;
- original Checkpoint backup name;
- file count and total payload bytes;
- sorted relative UTF-8 paths and 64-bit file sizes followed by file data.

Creation and import reject symlinks/special files, dot/traversal components, more than 128 files, paths over 240 bytes, nesting over 16 directories, duplicate paths, empty backups, malformed/trailing data, title-ID mismatch, and packages over 512 KiB. Packaging and extraction copy in 32 KiB chunks. The package is structurally validated to EOF before any extraction begins.

This format is intentionally a PocketShelf transport package rather than a format Checkpoint opens directly. A downloaded package is expanded into a normal Checkpoint backup directory, after which Checkpoint performs the user-approved live-title restore.

### 4. Conflict model

Track the last successful sync tuple locally:

```text
provider snapshot digest
remote save ID
remote updated_at
remote size/digest when available
```

On refresh:

- only local changed -> offer upload;
- only remote changed -> offer import;
- neither changed -> up to date;
- both changed or no common baseline -> conflict; require explicit direction;
- ambiguous source/binding -> disable sync, do not guess.

RomM's versioned `PocketShelf` slot remains the remote history. A local import always creates a local backup first.

### 5. Safe import transaction

#### Checkpoint backup folder (implemented)

1. Bind the RomM game to an exact installed SD title ID, from a verified PocketShelf receipt or explicit 16-hex-digit user entry.
2. Resolve exactly one Checkpoint title folder using Checkpoint's `0x%05X ` prefix rule; reject missing or duplicate matches.
3. Stream the remote `.pscp` file to PocketShelf staging with the 512 KiB bound.
4. Validate magic/version, complete structure, bounds, duplicate/traversing paths, EOF, and exact title ID before extraction.
5. Extract through a hidden same-parent temporary directory.
6. Rename the complete directory to a new collision-refusing `PocketShelf-YYYYMMDD-HHMMSS` Checkpoint backup.
7. Leave the live title save untouched. The user restores through Checkpoint.

#### SD file / RetroArch (designed, not UI-wired)

1. Verify RetroArch is not currently writing: sample size and mtime, wait, sample again. If unstable, abort.
2. Stream remote content to a same-directory temporary file with an enforced length bound and running hash.
3. Flush and close the temporary file.
4. Rename the current file to a collision-free timestamped backup.
5. Rename the temporary file to the target.
6. If activation fails, restore the backup.

#### CTR user-save archive

A title archive restore is not filesystem-atomic. It therefore requires a recoverable journal:

1. Probe/open the exact bound title archive.
2. Export the current archive to a verified SD backup before mutation.
3. Fully validate the incoming package and bounds before mutation.
4. Write journal phase `prepared` and flush it.
5. Clear the archive root, stream entries into the archive, and verify sizes/hashes.
6. Commit with `ARCHIVE_ACTION_COMMIT_SAVE_DATA`.
7. Delete the title secure value only after the new tree is complete.
8. Mark the journal complete.
9. On any failure after clearing begins, restore the verified pre-import backup and commit it. On next launch, an incomplete journal must force recovery before another sync.

No cancellation is allowed after native archive mutation begins; cancellation is safe during export/staging only.

### 6. Bounded streaming

- Keep one 32 KiB transfer buffer.
- Bound manifest/config bytes, entry count, nesting depth, relative path bytes, each file size, and total uncompressed bytes before applying an import.
- Stream source -> hash/container -> SD staging; never hold a native archive or large save in RAM.
- Current `http:C` upload calls accept a complete raw body and PocketShelf currently builds the entire multipart body in memory, so the existing 512 KiB upload cap remains truthful.
- Larger native packages require a separate streaming transport. Because PocketShelf supports only trusted-LAN plain HTTP, a future SOC-based HTTP/1.1 client can send a known `Content-Length` while reading the staged package in 32 KiB chunks. Do not remove the cap until that transport is implemented and tested against RomM.

## Implemented safe slice

`checkpoint_sync` and the Save Data UI now provide:

- authoritative discovery of `sdmc:/3ds/Checkpoint/saves/` title folders using Checkpoint's `(u32)titleId >> 8` prefix rule;
- exact installed-title binding from PocketShelf CIA receipts or explicit user entry, persisted atomically per RomM ROM ID;
- bounded discovery of title and backup directories with ambiguity and symlink rejection;
- deterministic multi-file `.pscp` package creation and validation with 32 KiB streaming, 128-entry/240-byte-path/16-level bounds, and a truthful 512 KiB package/upload ceiling;
- selectable, bounded local and remote backup lists with D-Pad/touch navigation;
- RomM versioned upload of the selected Checkpoint backup;
- bounded download, pre-extraction validation, title-ID enforcement, hidden temporary extraction, and atomic activation as a new Checkpoint backup directory;
- remote inventory filtering so a bound title sees every PocketShelf Checkpoint package for that exact title ID and can select the version to download;
- preservation of the prior managed-save behavior as fallback;
- no calls that open or mutate `ARCHIVE_USER_SAVEDATA`.

`save_sources` continues to provide the bounded RetroArch parser/path resolver, but RetroArch sync remains unwired pending observed runtime core identities and file behavior.

## Required hardware observations before the next device-facing slice

1. Copy or report the four relevant lines from `sdmc:/retroarch/retroarch.cfg`.
2. For each core to support first, report RetroArch's displayed core/library name, exact ROM path, and the files created/changed after an in-game save. Start with one GB/GBC/GBA and one SNES/NES title.
3. Confirm whether PocketShelf-installed CIA entries still have their `.titleid` receipts and provide one non-sensitive title ID chosen for a read-only probe.
4. Run a read-only archive-open probe build under the **current** PocketShelf permissions and record the exact `Result`. Do not broaden permissions yet.
5. If native archive support is still desired after that expected failure, test a separately labeled permission-review build that adds only the required filesystem category, then enumerate and export one disposable title's archive. Import remains disabled during this phase.
6. Record the archive entry count, total bytes, maximum depth/path length, and whether export is byte-stable across two runs with the game closed.

## Primary sources

- libctru FS API and archive actions: https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/services/fs.h
- libctru AM title enumeration: https://github.com/devkitPro/libctru/blob/9b55eda44cf80b971503991e0c78f9bc8fe50425/libctru/include/3ds/services/am.h
- Checkpoint SD backup root and CTR folder naming: https://github.com/BernardoGiordano/Checkpoint/blob/4001e75f673dda3320169481e3359ff8bdb3bc1b/3ds/source/paths.cpp
- Checkpoint backup-directory enumeration: https://github.com/BernardoGiordano/Checkpoint/blob/4001e75f673dda3320169481e3359ff8bdb3bc1b/3ds/source/title.cpp
- Checkpoint CTR save archive path: https://github.com/BernardoGiordano/Checkpoint/blob/4001e75f673dda3320169481e3359ff8bdb3bc1b/3ds/source/archive.cpp
- Checkpoint restore commit and secure-value handling: https://github.com/BernardoGiordano/Checkpoint/blob/4001e75f673dda3320169481e3359ff8bdb3bc1b/3ds/source/io.cpp
- Checkpoint filesystem access profile: https://github.com/BernardoGiordano/Checkpoint/blob/4001e75f673dda3320169481e3359ff8bdb3bc1b/3ds/assets/app.rsf
- RomM 5.4 save upload/version naming: https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/endpoints/saves.py and https://github.com/rommapp/romm/blob/68f71259f7c6de39fbeedff9cc9ac114c18ee250/backend/handler/asset_store.py
- RetroArch 3DS default paths: https://github.com/libretro/RetroArch/blob/f002f8e03e3f2dd8a479f0508b9fefba305b4b47/frontend/drivers/platform_ctr.c
- RetroArch save redirect ordering and `.srm` naming: https://github.com/libretro/RetroArch/blob/f002f8e03e3f2dd8a479f0508b9fefba305b4b47/runloop.c
- RetroArch config keys: https://github.com/libretro/RetroArch/blob/f002f8e03e3f2dd8a479f0508b9fefba305b4b47/configuration.c
