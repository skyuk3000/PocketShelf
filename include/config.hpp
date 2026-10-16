#pragma once

namespace romm3ds::config {

// First-run fallback. The in-app Settings screen persists the base URL and token.
inline constexpr char kDefaultBaseUrl[] = "http://192.168.1.6:890";
inline constexpr char kSettingsPath[] = "sdmc:/3ds/PocketShelf/settings.ini";
inline constexpr char kCatalogCachePath[] = "sdmc:/3ds/PocketShelf/cache/catalog.ndjson";
inline constexpr char kCoverCacheDirectory[] = "sdmc:/3ds/PocketShelf/cache/covers";
inline constexpr char kSaveDirectory[] = "sdmc:/3ds/PocketShelf/saves";
inline constexpr char kSaveBindingDirectory[] = "sdmc:/3ds/PocketShelf/save-bindings";
inline constexpr char kInstallReceiptDirectory[] = "sdmc:/3ds/PocketShelf/installed";
inline constexpr char kUserAgent[] = "PocketShelf/0.6.1";

} // namespace romm3ds::config
