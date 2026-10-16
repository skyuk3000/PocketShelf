#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include "bottom_navigation.hpp"
#include "cache.hpp"
#include "catalog_parser.hpp"
#include "checkpoint_sync.hpp"
#include "cia_header.hpp"
#include "config.hpp"
#include "cover_image.hpp"
#include "cover_status.hpp"
#include "library_state.hpp"
#include "release_config.hpp"
#include "romm_api.hpp"
#include "save_data.hpp"
#include "save_selection.hpp"
#include "save_sources.hpp"
#include "settings.hpp"
#include "text_layout.hpp"
#include "title_picker.hpp"
#include "transfer_status.hpp"
#include "type_chooser.hpp"
#include "ui_limits.hpp"
#include "updater.hpp"
#include "write_all.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <vector>

namespace {

constexpr std::size_t kTransferBufferSize = 32 * 1024;
constexpr std::uint32_t kAppVersion = 97;
constexpr std::uint64_t kAppTitleId = 0x0004000000F3D500ULL;
using romm3ds::ui::kMaxRenderObjects;
constexpr u64 kHttpStatusTimeoutNs = 15'000'000'000ULL;
constexpr Result kInvalidWriteProgressResult = static_cast<Result>(
    MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, RD_INVALID_SIZE));
u32 kBackground = C2D_Color32(17, 15, 24, 255);
u32 kPanel = C2D_Color32(31, 27, 43, 255);
u32 kPanelRaised = C2D_Color32(48, 41, 65, 255);
u32 kAccent = C2D_Color32(181, 143, 255, 255);
u32 kWarm = C2D_Color32(213, 188, 255, 255);
u32 kText = C2D_Color32(244, 241, 248, 255);
u32 kMuted = C2D_Color32(166, 158, 177, 255);
u32 kDanger = C2D_Color32(238, 129, 145, 255);
constexpr u32 kBlack = C2D_Color32(0, 0, 0, 255);

void applyTheme(romm3ds::Theme theme) {
    if (theme == romm3ds::Theme::Light) {
        kBackground = C2D_Color32(246, 244, 249, 255);
        kPanel = C2D_Color32(231, 227, 237, 255);
        kPanelRaised = C2D_Color32(216, 208, 229, 255);
        kAccent = C2D_Color32(103, 68, 162, 255);
        kWarm = C2D_Color32(125, 83, 190, 255);
        kText = C2D_Color32(39, 34, 46, 255);
        kMuted = C2D_Color32(99, 91, 108, 255);
        kDanger = C2D_Color32(177, 50, 72, 255);
        return;
    }
    kBackground = C2D_Color32(17, 15, 24, 255);
    kPanel = C2D_Color32(31, 27, 43, 255);
    kPanelRaised = C2D_Color32(48, 41, 65, 255);
    kAccent = C2D_Color32(181, 143, 255, 255);
    kWarm = C2D_Color32(213, 188, 255, 255);
    kText = C2D_Color32(244, 241, 248, 255);
    kMuted = C2D_Color32(166, 158, 177, 255);
    kDanger = C2D_Color32(238, 129, 145, 255);
}

enum class Screen {
    Library,
    TypeChooser,
    SaveData,
    TitlePicker,
};

enum class SavePageControl {
    Download,
    Upload,
    Refresh,
    Back,
};

enum class SaveConfirmation {
    None,
    ReplaceLocal,
};

enum class SaveProvider {
    Unsupported,
    RomAdjacentSav,
    Checkpoint,
};


bool isPending(Result result) {
    return result == static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING);
}

class HttpRequest {
public:
    HttpRequest() = default;
    HttpRequest(const HttpRequest&) = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;
    HttpRequest(HttpRequest&&) = delete;
    HttpRequest& operator=(HttpRequest&&) = delete;

    ~HttpRequest() {
        if (opened_) {
            if (!complete_) httpcCancelConnection(&context_);
            httpcCloseContext(&context_);
        }
    }

    Result begin(const char* url, const std::string& token, u32& status_code) {
        Result result = httpcOpenContext(&context_, HTTPC_METHOD_GET, url, 0);
        if (R_FAILED(result)) return result;
        opened_ = true;

        result = httpcAddRequestHeaderField(&context_, "Accept-Encoding", "identity");
        if (R_FAILED(result)) return result;
        result = httpcAddRequestHeaderField(&context_, "User-Agent", romm3ds::config::kUserAgent);
        if (R_FAILED(result)) return result;
        if (!token.empty()) {
            const std::string authorization = "Bearer " + token;
            result = httpcAddRequestHeaderField(&context_, "Authorization", authorization.c_str());
            if (R_FAILED(result)) return result;
        }
        result = httpcBeginRequest(&context_);
        if (R_FAILED(result)) return result;
        return httpcGetResponseStatusCodeTimeout(&context_, &status_code, kHttpStatusTimeoutNs);
    }

    Result beginPost(const char* url, const std::string& token, const char* content_type,
                     std::vector<u8>&& body, u32& status_code) {
        body_ = std::move(body);
        if (body_.size() > UINT32_MAX) return kInvalidWriteProgressResult;
        Result result = httpcOpenContext(&context_, HTTPC_METHOD_POST, url, 0);
        if (R_FAILED(result)) return result;
        opened_ = true;
        result = httpcAddRequestHeaderField(&context_, "Accept-Encoding", "identity");
        if (R_FAILED(result)) return result;
        result = httpcAddRequestHeaderField(&context_, "User-Agent", romm3ds::config::kUserAgent);
        if (R_FAILED(result)) return result;
        result = httpcAddRequestHeaderField(&context_, "Content-Type", content_type);
        if (R_FAILED(result)) return result;
        if (!token.empty()) {
            const std::string authorization = "Bearer " + token;
            result = httpcAddRequestHeaderField(&context_, "Authorization", authorization.c_str());
            if (R_FAILED(result)) return result;
        }
        result = httpcAddPostDataRaw(&context_,
            reinterpret_cast<const u32*>(body_.data()), static_cast<u32>(body_.size()));
        if (R_FAILED(result)) return result;
        result = httpcBeginRequest(&context_);
        if (R_FAILED(result)) return result;
        return httpcGetResponseStatusCodeTimeout(&context_, &status_code, kHttpStatusTimeoutNs);
    }

    Result beginPutJson(const char* url, const std::string& token, std::string body,
                        u32& status_code) {
        body_.assign(body.begin(), body.end());
        if (body_.size() > UINT32_MAX) return kInvalidWriteProgressResult;
        Result result = httpcOpenContext(&context_, HTTPC_METHOD_PUT, url, 0);
        if (R_FAILED(result)) return result;
        opened_ = true;
        result = httpcAddRequestHeaderField(&context_, "Accept-Encoding", "identity");
        if (R_FAILED(result)) return result;
        result = httpcAddRequestHeaderField(&context_, "User-Agent", romm3ds::config::kUserAgent);
        if (R_FAILED(result)) return result;
        result = httpcAddRequestHeaderField(&context_, "Content-Type", "application/json");
        if (R_FAILED(result)) return result;
        if (!token.empty()) {
            const std::string authorization = "Bearer " + token;
            result = httpcAddRequestHeaderField(&context_, "Authorization", authorization.c_str());
            if (R_FAILED(result)) return result;
        }
        result = httpcAddPostDataRaw(&context_, reinterpret_cast<const u32*>(body_.data()),
                                     static_cast<u32>(body_.size()));
        if (R_FAILED(result)) return result;
        result = httpcBeginRequest(&context_);
        if (R_FAILED(result)) return result;
        return httpcGetResponseStatusCodeTimeout(&context_, &status_code, kHttpStatusTimeoutNs);
    }

    Result download(void* buffer, u32 capacity, u32& downloaded) {
        return httpcDownloadData(&context_, static_cast<u8*>(buffer), capacity, &downloaded);
    }

    Result downloadState(u32& downloaded, u32& total) {
        downloaded = 0;
        total = 0;
        return httpcGetDownloadSizeState(&context_, &downloaded, &total);
    }

    Result responseHeader(const char* name, char* value, u32 capacity) {
        return httpcGetResponseHeader(&context_, name, value, capacity);
    }

    void markComplete() { complete_ = true; }

private:
    httpcContext context_{};
    bool opened_ = false;
    bool complete_ = false;
    std::vector<u8> body_;
};

class ClientApp {
public:
    ClientApp(C3D_RenderTarget* top, C3D_RenderTarget* bottom, C2D_TextBuf text_buffer,
              bool http_ready, bool am_ready, Result http_result, Result am_result)
        : top_(top), bottom_(bottom), text_buffer_(text_buffer), http_ready_(http_ready),
          am_ready_(am_ready) {
        settings_.base_url = romm3ds::config::kDefaultBaseUrl;
        romm3ds::Settings loaded;
        romm3ds::SettingsError settings_error = romm3ds::SettingsError::None;
        if (romm3ds::loadSettings(romm3ds::config::kSettingsPath, loaded, settings_error)) {
            settings_ = std::move(loaded);
        } else if (settings_error != romm3ds::SettingsError::NotFound) {
            std::snprintf(status_, sizeof(status_), "Settings ignored: %s",
                          romm3ds::settingsErrorMessage(settings_error));
            has_result_ = true;
        }
        applyTheme(settings_.theme);
        type_filter_ = settings_.type_filter;
        if (am_ready_) refreshInstalledTitles();
        if (romm3ds::loadCatalogCache(romm3ds::config::kCatalogCachePath, entries_)) {
            setStatus("Cached catalogue loaded; press X to refresh");
        }
        if (!http_ready_) {
            setResultStatus("HTTP service unavailable", http_result);
        } else if (!am_ready_) {
            setResultStatus("AM unavailable; browsing only", am_result);
        } else if (settings_.token.empty()) {
            setStatus("Open Settings and enter a RomM token");
        } else if (entries_.empty()) {
            setStatus("Ready");
        }
    }

    void run() {
        if (http_ready_ && !settings_.token.empty()) fetchLibrary();
        else if (!entries_.empty()) fetchCover(entries_[selected_]);
        hidSetRepeatParameters(20, 5);

        while (aptMainLoop()) {
            hidScanInput();
            const u32 pressed = hidKeysDown();
            const u32 repeated = hidKeysDownRepeat();
            const u32 held = hidKeysHeld();
            if (pressed & KEY_START) break;

            if (screen_ == Screen::TypeChooser) {
                handleTypeChooserInput(pressed, repeated, held);
            } else if (screen_ == Screen::TitlePicker) {
                handleTitlePickerInput(pressed, repeated);
            } else if (screen_ == Screen::SaveData) {
                handleSaveDataInput(pressed, repeated);
            } else {
                handleLibraryInput(pressed, repeated, held);
            }
            render();
        }
    }

private:
    void setStatus(const char* text) {
        std::snprintf(status_, sizeof(status_), "%s", text);
        has_result_ = false;
    }

    void setResultStatus(const char* text, Result result) {
        std::snprintf(status_, sizeof(status_), "%s (Result 0x%08lX)", text,
                      static_cast<unsigned long>(static_cast<u32>(result)));
        has_result_ = true;
    }

    void drawText(float x, float y, float scale, u32 color, const char* string) {
        C2D_Text text;
        if (C2D_TextParse(&text, text_buffer_, string) == nullptr) return;
        C2D_TextOptimize(&text);
        C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, color);
    }

    void drawWrappedText(float x, float y, float line_height, float scale, u32 color,
                         const std::string& value, std::size_t columns,
                         std::size_t max_lines) {
        const auto lines = romm3ds::ui::wrapText(value, columns, max_lines);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            drawText(x, y + static_cast<float>(i) * line_height, scale, color,
                     lines[i].c_str());
        }
    }

    void drawBottomStatus(float y) {
        drawWrappedText(60, y, romm3ds::ui::kBottomLineHeight,
                        romm3ds::ui::kBottomStatusScale,
                        has_result_ ? kDanger : kMuted, status_, 38,
                        romm3ds::ui::kBottomStatusLines);
    }

    void activateBottomControl(romm3ds::ui::BottomControl control) {
        switch (control) {
            case romm3ds::ui::BottomControl::DetailsScreenshots:
                if (entries_.empty()) { setStatus("No game selected"); break; }
                screenshot_selection_.setCount(entries_[selected_].screenshots.size());
                bottom_navigation_.openDetailsView(romm3ds::ui::DetailsView::Screenshots);
                cover_resource_key_.clear();
                cover_.clear();
                fetchCover(entries_[selected_]);
                break;
            case romm3ds::ui::BottomControl::DetailsManage:
                if (entries_.empty()) { setStatus("No game selected"); break; }
                bottom_navigation_.openDetailsView(romm3ds::ui::DetailsView::Manage);
                break;
            case romm3ds::ui::BottomControl::ScreenshotsPrevious:
                screenshot_selection_.previous();
                if (!entries_.empty()) { cover_resource_key_.clear(); cover_.clear(); fetchCover(entries_[selected_]); }
                break;
            case romm3ds::ui::BottomControl::ScreenshotsNext:
                screenshot_selection_.next();
                if (!entries_.empty()) { cover_resource_key_.clear(); cover_.clear(); fetchCover(entries_[selected_]); }
                break;
            case romm3ds::ui::BottomControl::ManageNowPlaying: toggleNowPlaying(); break;
            case romm3ds::ui::BottomControl::ManageLastPlayed: toggleLastPlayed(); break;
            case romm3ds::ui::BottomControl::ManageBacklogged: toggleBacklogged(); break;
            case romm3ds::ui::BottomControl::ManagePlayStatus: cyclePlayStatus(); break;
            case romm3ds::ui::BottomControl::ManageRating: cycleRating(); break;
            case romm3ds::ui::BottomControl::ManageDifficulty: cycleDifficulty(); break;
            case romm3ds::ui::BottomControl::ManageCompletion: cycleCompletion(); break;
            case romm3ds::ui::BottomControl::DetailsBack:
                bottom_navigation_.openDetailsView(romm3ds::ui::DetailsView::Info);
                cover_resource_key_.clear();
                cover_.clear();
                if (!entries_.empty()) fetchCover(entries_[selected_]);
                break;
            case romm3ds::ui::BottomControl::DetailsTransfer:
                if (entries_.empty()) setStatus("No file selected");
                else if (!http_ready_) setStatus("Transfer disabled: HTTP service unavailable");
                else if (entries_[selected_].action == romm3ds::CatalogAction::InstallCia && !am_ready_)
                    setStatus("Install disabled: AM service unavailable");
                else transfer(entries_[selected_]);
                break;
            case romm3ds::ui::BottomControl::DetailsSaveData:
                if (entries_.empty()) setStatus("No game selected");
                else openSaveData();
                break;
            case romm3ds::ui::BottomControl::FilterType:
                chooseTypeFilter();
                break;
            case romm3ds::ui::BottomControl::FilterSearch:
                editSearch();
                break;
            case romm3ds::ui::BottomControl::FilterMetadata:
                editMetadataFilter();
                break;
            case romm3ds::ui::BottomControl::SortTitle:
                if (sort_applied_) sort_ascending_ = !sort_ascending_;
                else { sort_applied_ = true; sort_ascending_ = true; }
                sortEntriesByTitle();
                setStatus(sort_ascending_ ? "Sorted by title A-Z" : "Sorted by title Z-A");
                break;
            case romm3ds::ui::BottomControl::SettingsUrl:
                editBaseUrl();
                break;
            case romm3ds::ui::BottomControl::SettingsToken:
                editToken();
                break;
            case romm3ds::ui::BottomControl::SettingsConnect:
                if (http_ready_ && !settings_.token.empty()) fetchLibrary();
                else setStatus(settings_.token.empty() ? "Enter a RomM token first" :
                                                         "Refresh disabled: HTTP service unavailable");
                break;
            case romm3ds::ui::BottomControl::SettingsTheme: {
                romm3ds::Settings candidate = settings_;
                candidate.theme = settings_.theme == romm3ds::Theme::Dark ?
                    romm3ds::Theme::Light : romm3ds::Theme::Dark;
                if (persistSettings(candidate, false)) {
                    applyTheme(settings_.theme);
                    setStatus(settings_.theme == romm3ds::Theme::Light ?
                              "Light theme saved" : "Dark theme saved");
                }
                break;
            }
            case romm3ds::ui::BottomControl::SettingsUpdate:
                checkForUpdate();
                break;
            case romm3ds::ui::BottomControl::None:
                break;
        }
    }

    void sortEntriesByTitle() {
        if (entries_.empty()) return;
        const std::uint64_t selected_id = entries_[selected_].file_id;
        std::sort(entries_.begin(), entries_.end(), [this](const auto& left, const auto& right) {
            return sort_ascending_ ? left.title < right.title : left.title > right.title;
        });
        selected_ = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].file_id == selected_id) { selected_ = i; break; }
        }
    }

    bool putUserProps(const std::string& url, const std::string& body, const char* success) {
        if (entries_.empty() || !http_ready_ || settings_.token.empty() || body.empty()) return false;
        HttpRequest request;
        u32 status_code = 0;
        const Result result = request.beginPutJson(url.c_str(), settings_.token, body, status_code);
        if (R_FAILED(result)) { setResultStatus("RomM user action failed", result); return false; }
        if (status_code != 200) {
            std::snprintf(status_, sizeof(status_), "RomM user action failed (HTTP %lu)",
                          static_cast<unsigned long>(status_code));
            has_result_ = true;
            return false;
        }
        std::string response;
        if (!receiveBounded(request, response, 16 * 1024, "RomM user action response failed"))
            return false;
        setStatus(success);
        return true;
    }

    void toggleNowPlaying() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const bool enabled = !entry.now_playing;
        std::string url = enabled ?
            romm3ds::rommLastPlayedUrl(settings_.base_url, entry.rom_id, true) :
            romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id);
        if (putUserProps(url, romm3ds::rommUserPropsBody(
                romm3ds::RommUserAction::NowPlaying, enabled ? 1 : 0),
                enabled ? "Marked now playing and updated last played" : "Cleared now playing")) {
            entry.now_playing = enabled;
            if (enabled) entry.has_last_played = true;
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void cyclePlayStatus() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        romm3ds::RommUserAction action = romm3ds::RommUserAction::StatusIncomplete;
        const char* next = "incomplete";
        if (entry.status == "incomplete") { action = romm3ds::RommUserAction::StatusFinished; next = "finished"; }
        else if (entry.status == "finished") { action = romm3ds::RommUserAction::StatusCompleted100; next = "completed_100"; }
        else if (entry.status == "completed_100") { action = romm3ds::RommUserAction::StatusRetired; next = "retired"; }
        else if (entry.status == "retired") { action = romm3ds::RommUserAction::StatusNeverPlaying; next = "never_playing"; }
        else if (entry.status == "never_playing") { action = romm3ds::RommUserAction::StatusClear; next = ""; }
        if (putUserProps(romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id),
                         romm3ds::rommUserPropsBody(action, 0), "RomM play status updated")) {
            entry.status = next;
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void cycleRating() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const unsigned next = entry.rating >= 10 ? 0 : entry.rating + 1;
        if (putUserProps(romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id),
                         romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Rating, next),
                         "RomM rating updated")) {
            entry.rating = static_cast<std::uint8_t>(next);
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void toggleBacklogged() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const bool enabled = !entry.backlogged;
        if (putUserProps(romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id),
                         romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Backlogged,
                                                    enabled ? 1 : 0),
                         enabled ? "Added to RomM backlog" : "Removed from RomM backlog")) {
            entry.backlogged = enabled;
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void cycleDifficulty() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const unsigned next = entry.difficulty >= 10 ? 0 : entry.difficulty + 1;
        if (putUserProps(romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id),
                         romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Difficulty, next),
                         "RomM difficulty updated")) {
            entry.difficulty = static_cast<std::uint8_t>(next);
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void cycleCompletion() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const unsigned next = entry.completion >= 100 ? 0 :
                              std::min(100u, static_cast<unsigned>(entry.completion) + 10u);
        if (putUserProps(romm3ds::rommUserPropsUrl(settings_.base_url, entry.rom_id),
                         romm3ds::rommUserPropsBody(romm3ds::RommUserAction::Completion, next),
                         "RomM completion updated")) {
            entry.completion = static_cast<std::uint8_t>(next);
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void toggleLastPlayed() {
        if (entries_.empty()) return;
        auto& entry = entries_[selected_];
        const bool played = !entry.has_last_played;
        if (putUserProps(romm3ds::rommLastPlayedUrl(settings_.base_url, entry.rom_id, played),
                         "{}", played ? "Marked played in RomM" : "Cleared RomM last played")) {
            entry.has_last_played = played;
            saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        }
    }

    void handleLibraryInput(u32 pressed, u32 repeated, u32 held) {
        const bool details_page = bottom_navigation_.page() == romm3ds::ui::BottomPage::Details;
        const bool info_view = details_page &&
            bottom_navigation_.detailsView() == romm3ds::ui::DetailsView::Info;
        const bool screenshots_view = details_page &&
            bottom_navigation_.detailsView() == romm3ds::ui::DetailsView::Screenshots;
        if (pressed & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            if (info_view && touch.px >= romm3ds::ui::kBottomContentX &&
                touch.py >= romm3ds::ui::kDetailsMetadataY &&
                touch.py < romm3ds::ui::kDetailsActionsY) {
                details_scroll_.beginTouch(touch.py);
            } else {
                const bool was_screenshots = screenshots_view;
                const auto action = bottom_navigation_.touch(touch.px, touch.py);
                if (action.kind == romm3ds::ui::BottomTouchKind::PageChanged) {
                    details_scroll_.endTouch();
                    if (was_screenshots && !entries_.empty()) {
                        cover_resource_key_.clear(); cover_.clear(); fetchCover(entries_[selected_]);
                    }
                } else if (action.kind == romm3ds::ui::BottomTouchKind::ControlActivated) {
                    activateBottomControl(action.control);
                    return;
                }
            }
        } else if ((held & KEY_TOUCH) && info_view) {
            touchPosition touch{};
            hidTouchRead(&touch);
            details_scroll_.dragTouch(touch.py, romm3ds::ui::kDetailsMetadataLineHeight);
        } else if (!(held & KEY_TOUCH)) {
            details_scroll_.endTouch();
        }

        if (info_view) {
            circlePosition circle{};
            hidCircleRead(&circle);
            if (circle_scroll_cooldown_ > 0) --circle_scroll_cooldown_;
            if (circle_scroll_cooldown_ == 0 && circle.dy >= 40) {
                details_scroll_.scrollBy(-1);
                circle_scroll_cooldown_ = 4;
            } else if (circle_scroll_cooldown_ == 0 && circle.dy <= -40) {
                details_scroll_.scrollBy(1);
                circle_scroll_cooldown_ = 4;
            }
        } else {
            circle_scroll_cooldown_ = 0;
        }

        if (repeated & KEY_DUP) bottom_navigation_.previous();
        if (repeated & KEY_DDOWN) bottom_navigation_.next();
        if (pressed & KEY_A) {
            activateBottomControl(bottom_navigation_.focused());
            return;
        }

        if (screenshots_view && (repeated & (KEY_DLEFT | KEY_DRIGHT))) {
            if (repeated & KEY_DLEFT) screenshot_selection_.previous();
            else screenshot_selection_.next();
            if (!entries_.empty()) { cover_resource_key_.clear(); cover_.clear(); fetchCover(entries_[selected_]); }
            return;
        }

        const std::size_t previous_selection = selected_;
        if (!entries_.empty()) {
            if ((repeated & KEY_DLEFT) && selected_ > 0) --selected_;
            if ((repeated & KEY_DRIGHT) && selected_ + 1 < entries_.size()) ++selected_;
        }
        if (pressed & KEY_X) {
            if (http_ready_) fetchLibrary();
            else setStatus("Refresh disabled: HTTP service unavailable");
        }
        if (pressed & KEY_B) {
            if (details_page && bottom_navigation_.detailsView() != romm3ds::ui::DetailsView::Info) {
                bottom_navigation_.openDetailsView(romm3ds::ui::DetailsView::Info);
                if (!entries_.empty()) { cover_resource_key_.clear(); cover_.clear(); fetchCover(entries_[selected_]); }
            } else {
                bottom_navigation_.setPage(romm3ds::ui::BottomPage::Details);
            }
        }
        if (!entries_.empty() && selected_ != previous_selection) {
            details_scroll_.reset();
            screenshot_selection_.setCount(entries_[selected_].screenshots.size());
            cover_resource_key_.clear();
            fetchCover(entries_[selected_]);
        }
    }

    void editSearch() {
        SwkbdState keyboard; swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 64);
        swkbdSetHintText(&keyboard, "Search titles (blank = all)");
        swkbdSetInitialText(&keyboard, search_.c_str());
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Search", true);
        char entered[65]{};
        if (swkbdInputText(&keyboard, entered, sizeof(entered)) == SWKBD_BUTTON_RIGHT) {
            search_ = entered; fetchLibrary();
        }
    }

    void editMetadataFilter() {
        SwkbdState keyboard;
        swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 96);
        swkbdSetHintText(&keyboard, "genre=RPG; developer=Game Freak");
        swkbdSetInitialText(&keyboard, metadata_filter_text_.c_str());
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Apply", true);
        char entered[97]{};
        if (swkbdInputText(&keyboard, entered, sizeof(entered)) != SWKBD_BUTTON_RIGHT) return;
        const std::string value = entered;
        romm3ds::RommFilters parsed;
        if (!romm3ds::parseRommFilters(value, parsed)) {
            setStatus("Use key=value terms separated by ; or clear");
            return;
        }
        romm_filters_ = std::move(parsed);
        metadata_filter_text_ = (value == "clear") ? "" : value;
        fetchLibrary();
    }

    void chooseTypeFilter() {
        type_chooser_.emplace(type_filter_);
        type_touch_active_ = false;
        circle_scroll_cooldown_ = 0;
        screen_ = Screen::TypeChooser;
    }

    void applyTypeChooser() {
        if (!type_chooser_) return;
        romm3ds::Settings candidate = settings_;
        candidate.type_filter.assign(type_chooser_->value().data(), type_chooser_->value().size());
        type_touch_active_ = false;
        type_chooser_.reset();
        screen_ = Screen::Library;
        if (persistSettings(candidate) && http_ready_ && !settings_.token.empty()) fetchLibrary();
    }

    void handleTypeChooserInput(u32 pressed, u32 repeated, u32 held) {
        if (!type_chooser_) { screen_ = Screen::Library; return; }
        if (pressed & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            if (touch.px < 52) {
                const auto action = bottom_navigation_.touch(touch.px, touch.py);
                if (action.kind == romm3ds::ui::BottomTouchKind::PageChanged) {
                    type_chooser_.reset();
                    screen_ = Screen::Library;
                    return;
                }
            } else if (touch.px >= 58 && touch.px < 310 &&
                       touch.py >= romm3ds::ui::kTypeChooserY &&
                       touch.py < romm3ds::ui::kTypeChooserY +
                                      static_cast<int>(romm3ds::ui::TypeChooserState::kVisibleRows) *
                                          romm3ds::ui::kTypeChooserStride) {
                type_touch_x_ = touch.px;
                type_touch_y_ = touch.py;
                type_touch_active_ = true;
                type_chooser_->beginTouch(touch.py);
            }
        } else if ((held & KEY_TOUCH) && type_touch_active_) {
            touchPosition touch{};
            hidTouchRead(&touch);
            type_touch_x_ = touch.px;
            type_touch_y_ = touch.py;
            type_chooser_->dragTouch(touch.py, romm3ds::ui::kTypeChooserStride);
        } else if (!(held & KEY_TOUCH) && type_touch_active_) {
            const std::size_t row = romm3ds::ui::typeChoiceAt(
                type_touch_x_, type_touch_y_, type_chooser_->visibleCount());
            type_touch_active_ = false;
            if (type_chooser_->endTouch(row)) {
                applyTypeChooser();
                return;
            }
        }
        if (repeated & KEY_DUP) type_chooser_->up();
        if (repeated & KEY_DDOWN) type_chooser_->down();
        circlePosition circle{};
        hidCircleRead(&circle);
        if (circle_scroll_cooldown_ > 0) --circle_scroll_cooldown_;
        if (circle_scroll_cooldown_ == 0 && circle.dy >= 40) {
            type_chooser_->up();
            circle_scroll_cooldown_ = 4;
        } else if (circle_scroll_cooldown_ == 0 && circle.dy <= -40) {
            type_chooser_->down();
            circle_scroll_cooldown_ = 4;
        }
        if (pressed & KEY_B) {
            type_touch_active_ = false;
            type_chooser_.reset();
            screen_ = Screen::Library;
            return;
        }
        if (pressed & KEY_A) applyTypeChooser();
    }

    bool refreshInstalledTitles() {
        installed_title_ids_.clear();
        installed_title_choices_.clear();
        u32 count = 0;
        if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) ||
            count > romm3ds::ui::kMaxInstalledTitlesForPicker) return false;
        if (count == 0) return true;
        installed_title_ids_.resize(count);
        u32 read = 0;
        if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, installed_title_ids_.data()))) {
            installed_title_ids_.clear();
            return false;
        }
        installed_title_ids_.resize(read);
        std::vector<AM_TitleInfo> title_info(read);
        const bool have_title_info = read != 0 && R_SUCCEEDED(AM_GetTitleInfo(
            MEDIATYPE_SD, read, installed_title_ids_.data(), title_info.data()));
        installed_title_choices_.reserve(read);
        for (u32 i = 0; i < read; ++i) {
            const u64 title_id = installed_title_ids_[i];
            const AM_TitleInfo info = have_title_info ? title_info[i] : AM_TitleInfo{};
            installed_title_choices_.push_back(romm3ds::ui::InstalledTitleChoice{
                title_id,
                have_title_info && info.titleID == title_id ? info.size : 0,
                have_title_info && info.titleID == title_id ?
                    static_cast<std::uint16_t>(info.version) : static_cast<std::uint16_t>(0),
            });
        }
        std::sort(installed_title_ids_.begin(), installed_title_ids_.end());
        return true;
    }

    std::string managedSaveDirectoryFor(const romm3ds::CatalogEntry& entry) const {
        return romm3ds::managedSaveDirectory(romm3ds::config::kSaveDirectory, entry.rom_id);
    }

    std::size_t selectableLocalSaveCount() const {
        return save_provider_ == SaveProvider::Checkpoint ? checkpoint_backups_.size() :
               save_provider_ == SaveProvider::RomAdjacentSav ? local_saves_.size() : 0;
    }

    void syncSaveSelectionCounts() {
        save_selection_.setCounts(selectableLocalSaveCount(), remote_saves_.size());
    }

    bool installedTitle(std::uint64_t title_id) const {
        return std::binary_search(installed_title_ids_.begin(), installed_title_ids_.end(), title_id);
    }

    void refreshLocalSaves() {
        local_saves_.clear();
        checkpoint_backups_.clear();
        checkpoint_title_id_ = 0;
        checkpoint_title_directory_.clear();
        rom_adjacent_save_path_.clear();
        save_provider_ = SaveProvider::Unsupported;
        if (entries_.empty()) return;
        const auto& entry = entries_[selected_];
        if (entry.action != romm3ds::CatalogAction::InstallCia) {
            const std::string rom_path = romm3ds::downloadDirectoryForType(entry.type) + "/" +
                                         entry.file_name;
            if (romm3ds::inspectLocalPayload(rom_path).present &&
                romm3ds::resolveRomAdjacentSavePath(
                    rom_path, entry.type, rom_adjacent_save_path_)) {
                save_provider_ = SaveProvider::RomAdjacentSav;
                const auto state = romm3ds::inspectLocalPayload(rom_adjacent_save_path_);
                if (state.present) {
                    const std::size_t slash = rom_adjacent_save_path_.rfind('/');
                    local_saves_.push_back(romm3ds::LocalSaveFile{
                        state.size, state.modified,
                        rom_adjacent_save_path_.substr(slash + 1), rom_adjacent_save_path_});
                }
            }
            syncSaveSelectionCounts();
            return;
        }
        const std::string binding_path = romm3ds::checkpointBindingPath(
            romm3ds::config::kSaveBindingDirectory, entry.rom_id);
        std::uint64_t title_id = 0;
        bool bound = romm3ds::loadCheckpointBinding(binding_path, title_id);
        if (!bound) {
            bound = romm3ds::loadInstallReceipt(
                romm3ds::installReceiptPath(romm3ds::config::kInstallReceiptDirectory,
                                            entry.file_id), title_id);
            if (bound && installedTitle(title_id))
                romm3ds::saveCheckpointBindingAtomic(binding_path, title_id);
        }
        if (bound && installedTitle(title_id) &&
            romm3ds::findCheckpointTitleDirectory(romm3ds::kCheckpointSavesRoot, title_id,
                                                  checkpoint_title_directory_)) {
            if (!romm3ds::scanCheckpointBackups(checkpoint_title_directory_, checkpoint_backups_)) {
                checkpoint_backups_.clear();
                checkpoint_title_directory_.clear();
            } else {
                checkpoint_title_id_ = title_id;
                save_provider_ = SaveProvider::Checkpoint;
            }
        }
        syncSaveSelectionCounts();
    }

    bool scanCheckpointTitleChoices(std::vector<romm3ds::ui::CheckpointTitleChoice>& output) {
        output.clear();
        DIR* directory = opendir(romm3ds::kCheckpointSavesRoot);
        if (!directory) return false;
        std::size_t seen = 0;
        bool ok = true;
        while (dirent* item = readdir(directory)) {
            const std::string name = item->d_name;
            if (name == "." || name == "..") continue;
            if (++seen > romm3ds::kMaxCheckpointTitleDirectories) { ok = false; break; }
            std::uint32_t unique_id = 0;
            if (!romm3ds::parseCheckpointTitleFolder(name, unique_id)) continue;
            const std::string path = std::string(romm3ds::kCheckpointSavesRoot) + "/" + name;
            struct stat info {};
            if (::lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) continue;
            const std::size_t separator = name.find(' ');
            if (separator == std::string::npos || separator + 1 >= name.size()) continue;
            output.push_back(romm3ds::ui::CheckpointTitleChoice{
                unique_id, name.substr(separator + 1), path});
        }
        closedir(directory);
        if (!ok) output.clear();
        return ok;
    }

    void openCheckpointTitlePicker() {
        if (entries_.empty()) return;
        if (!am_ready_) {
            setStatus("Title picker unavailable: AM service is not ready");
            return;
        }
        refreshInstalledTitles();
        std::vector<romm3ds::ui::CheckpointTitleChoice> checkpoint_titles;
        if (!scanCheckpointTitleChoices(checkpoint_titles)) {
            setStatus("Could not scan the bounded Checkpoint title folder list");
            return;
        }
        std::vector<romm3ds::ui::TitlePickerCandidate> candidates;
        if (!romm3ds::ui::buildTitlePickerCandidates(
                installed_title_choices_, checkpoint_titles, candidates)) {
            setStatus("Too many installed Checkpoint titles for the safe picker");
            return;
        }
        title_picker_.emplace(std::move(candidates), checkpoint_title_id_);
        screen_ = Screen::TitlePicker;
        setStatus(title_picker_->empty() ?
            "No installed application has a unique Checkpoint save folder" :
            "Choose an installed Checkpoint title; A confirms the exact title ID");
    }

    void confirmCheckpointTitlePicker() {
        if (!title_picker_ || entries_.empty()) return;
        const auto* choice = title_picker_->current();
        if (!choice) {
            setStatus("No installed Checkpoint title is available to bind");
            return;
        }
        std::string verified_directory;
        if (!installedTitle(choice->title_id) ||
            !romm3ds::findCheckpointTitleDirectory(romm3ds::kCheckpointSavesRoot,
                                                   choice->title_id, verified_directory) ||
            verified_directory != choice->checkpoint_directory) {
            setStatus("Installed titles or Checkpoint folders changed; reopen the picker");
            return;
        }
        const std::string path = romm3ds::checkpointBindingPath(
            romm3ds::config::kSaveBindingDirectory, entries_[selected_].rom_id);
        if (!romm3ds::saveCheckpointBindingAtomic(path, choice->title_id)) {
            setStatus("Could not save Checkpoint title binding");
            return;
        }
        title_picker_.reset();
        screen_ = Screen::SaveData;
        refreshLocalSaves();
        fetchRemoteSaves();
        setStatus("Checkpoint title binding saved from the installed-title picker");
    }

    void bindCheckpointTitleManually() {
        if (entries_.empty()) return;
        SwkbdState keyboard;
        swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 16);
        swkbdSetHintText(&keyboard, "Fallback: 16-digit installed title ID");
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Bind", true);
        swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        char entered[17]{};
        if (swkbdInputText(&keyboard, entered, sizeof(entered)) != SWKBD_BUTTON_RIGHT) {
            setStatus("Checkpoint binding unchanged");
            return;
        }
        std::uint64_t title_id = 0;
        for (std::size_t i = 0; i < 16; ++i) {
            const unsigned char ch = static_cast<unsigned char>(entered[i]);
            unsigned digit = 0;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
            else { setStatus("Title ID must be exactly 16 hexadecimal digits"); return; }
            title_id = (title_id << 4) | digit;
        }
        if (entered[16] != '\0' || title_id == 0 || !installedTitle(title_id)) {
            setStatus("Title ID is not in the installed SD title list");
            return;
        }
        std::string checkpoint_directory;
        if (!romm3ds::findCheckpointTitleDirectory(romm3ds::kCheckpointSavesRoot, title_id,
                                                   checkpoint_directory)) {
            setStatus("No unique Checkpoint save folder matches that title ID");
            return;
        }
        const std::string path = romm3ds::checkpointBindingPath(
            romm3ds::config::kSaveBindingDirectory, entries_[selected_].rom_id);
        if (!romm3ds::saveCheckpointBindingAtomic(path, title_id)) {
            setStatus("Could not save Checkpoint title binding");
            return;
        }
        title_picker_.reset();
        screen_ = Screen::SaveData;
        refreshLocalSaves();
        fetchRemoteSaves();
        setStatus("Checkpoint title binding saved with manual fallback");
    }

    void handleTitlePickerInput(u32 pressed, u32 repeated) {
        if (!title_picker_) { screen_ = Screen::SaveData; return; }
        if (pressed & KEY_B) {
            title_picker_.reset();
            screen_ = Screen::SaveData;
            setStatus("Checkpoint binding unchanged");
            return;
        }
        if (pressed & KEY_X) {
            bindCheckpointTitleManually();
            return;
        }
        if (pressed & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            const std::size_t row = romm3ds::ui::titlePickerRowAt(
                touch.px, touch.py, title_picker_->visibleCount());
            if (row != romm3ds::ui::kNoTitlePickerRow) {
                title_picker_->selectVisible(row);
                return;
            }
            if (touch.py >= 198 && touch.py < 228) {
                if (touch.px < 160) confirmCheckpointTitlePicker();
                else {
                    title_picker_.reset();
                    screen_ = Screen::SaveData;
                    setStatus("Checkpoint binding unchanged");
                }
                return;
            }
        }
        if (repeated & KEY_DUP) title_picker_->up();
        if (repeated & KEY_DDOWN) title_picker_->down();
        if (pressed & KEY_A) confirmCheckpointTitlePicker();
    }

    bool receiveBounded(HttpRequest& request, std::string& output, std::size_t limit,
                        const char* failure) {
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) { setStatus("Not enough memory for response buffer"); return false; }
        output.clear();
        Result result = 0;
        do {
            u32 downloaded = 0;
            result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (output.size() + downloaded > limit) {
                setStatus("RomM response exceeds the safe limit");
                return false;
            }
            output.append(reinterpret_cast<const char*>(buffer.get()), downloaded);
            if (R_FAILED(result) && !isPending(result)) {
                setResultStatus(failure, result);
                return false;
            }
        } while (isPending(result));
        request.markComplete();
        return true;
    }

    void fetchRemoteSaves() {
        remote_saves_.clear();
        syncSaveSelectionCounts();
        if (entries_.empty() || !http_ready_ || settings_.token.empty() ||
            save_provider_ == SaveProvider::Unsupported) return;
        const auto& entry = entries_[selected_];
        const std::string url = romm3ds::rommSaveInventoryUrl(settings_.base_url, entry.rom_id);
        HttpRequest request;
        if (!beginHttp(request, url.c_str(), "Save inventory request failed")) return;
        std::string json;
        if (!receiveBounded(request, json, romm3ds::kMaxSaveInventoryBytes,
                            "Save inventory download failed")) return;
        romm3ds::SaveInventory inventory;
        romm3ds::SaveParseError error = romm3ds::SaveParseError::None;
        if (!romm3ds::parseSaveInventory(json, entry.rom_id, inventory, error)) {
            std::snprintf(status_, sizeof(status_), "Save inventory: %s",
                          romm3ds::saveParseErrorMessage(error));
            has_result_ = true;
            return;
        }
        remote_saves_ = std::move(inventory.remote);
        if (checkpoint_title_id_ != 0) {
            remote_saves_.erase(std::remove_if(remote_saves_.begin(), remote_saves_.end(),
                [this](const romm3ds::RemoteSaveFile& save) {
                    return !romm3ds::isCheckpointPackageFilename(save.file_name,
                                                                 checkpoint_title_id_);
                }), remote_saves_.end());
        } else if (save_provider_ == SaveProvider::RomAdjacentSav) {
            const std::size_t slash = rom_adjacent_save_path_.rfind('/');
            const std::string file_name = rom_adjacent_save_path_.substr(slash + 1);
            remote_saves_.erase(std::remove_if(remote_saves_.begin(), remote_saves_.end(),
                [&file_name](const romm3ds::RemoteSaveFile& save) {
                    return !romm3ds::isRomAdjacentSaveVersion(file_name, save.file_name);
                }), remote_saves_.end());
        }
        syncSaveSelectionCounts();
        setStatus("Save inventory refreshed");
    }

    void openSaveData() {
        save_confirmation_ = SaveConfirmation::None;
        save_control_ = SavePageControl::Download;
        screen_ = Screen::SaveData;
        refreshLocalSaves();
        fetchRemoteSaves();
        if (save_provider_ == SaveProvider::Unsupported) {
            setStatus(entries_[selected_].action == romm3ds::CatalogAction::InstallCia ?
                "Bind an installed title with a Checkpoint folder; Y opens the picker" :
                "No exact ROM-adjacent .sav binding for this downloaded file");
        }
    }

    void activateSaveControl() {
        switch (save_control_) {
            case SavePageControl::Download: downloadSave(); break;
            case SavePageControl::Upload: uploadSave(); break;
            case SavePageControl::Refresh:
                save_confirmation_ = SaveConfirmation::None;
                refreshLocalSaves();
                fetchRemoteSaves();
                break;
            case SavePageControl::Back:
                save_confirmation_ = SaveConfirmation::None;
                screen_ = Screen::Library;
                break;
        }
    }

    void handleSaveDataInput(u32 pressed, u32 repeated) {
        if (pressed & KEY_B) {
            save_confirmation_ = SaveConfirmation::None;
            screen_ = Screen::Library;
            return;
        }
        if (pressed & KEY_Y) {
            save_confirmation_ = SaveConfirmation::None;
            if (!entries_.empty() &&
                entries_[selected_].action == romm3ds::CatalogAction::InstallCia) {
                openCheckpointTitlePicker();
            } else if (save_provider_ == SaveProvider::RomAdjacentSav) {
                setStatus("ROM-adjacent .sav provider is fixed to this exact downloaded ROM");
            } else {
                setStatus("No safe .sav provider: download a supported GB/GBC/GBA/NDS ROM first");
            }
            return;
        }
        if (pressed & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            if (touch.px >= 12 && touch.px < 308 &&
                touch.py >= romm3ds::ui::kSaveLocalY &&
                touch.py < romm3ds::ui::kSaveLocalY + romm3ds::ui::kSaveCardHeight) {
                save_control_ = SavePageControl::Upload;
                save_confirmation_ = SaveConfirmation::None;
                return;
            }
            if (touch.px >= 12 && touch.px < 308 &&
                touch.py >= romm3ds::ui::kSaveRemoteY &&
                touch.py < romm3ds::ui::kSaveRemoteY + romm3ds::ui::kSaveCardHeight) {
                save_control_ = SavePageControl::Download;
                save_confirmation_ = SaveConfirmation::None;
                return;
            }
            if (touch.py >= romm3ds::ui::kSavePrimaryActionsY &&
                touch.py < romm3ds::ui::kSavePrimaryActionsY +
                           romm3ds::ui::kSaveActionHeight) {
                const SavePageControl touched = touch.px < 160 ?
                    SavePageControl::Download : SavePageControl::Upload;
                if (touched != save_control_) save_confirmation_ = SaveConfirmation::None;
                save_control_ = touched;
                activateSaveControl();
                return;
            }
            if (touch.py >= romm3ds::ui::kSaveSecondaryActionsY &&
                touch.py < romm3ds::ui::kSaveSecondaryActionsY +
                           romm3ds::ui::kSaveActionHeight) {
                save_confirmation_ = SaveConfirmation::None;
                save_control_ = touch.px < 160 ? SavePageControl::Refresh : SavePageControl::Back;
                activateSaveControl();
                return;
            }
        }
        if (repeated & (KEY_DUP | KEY_DDOWN)) {
            save_confirmation_ = SaveConfirmation::None;
            int value = static_cast<int>(save_control_);
            value += (repeated & KEY_DDOWN) ? 1 : -1;
            if (value < 0) value = 3;
            if (value > 3) value = 0;
            save_control_ = static_cast<SavePageControl>(value);
        }
        if (repeated & KEY_DLEFT) {
            save_confirmation_ = SaveConfirmation::None;
            if (save_control_ == SavePageControl::Upload) save_selection_.previousLocal();
            else save_selection_.previousRemote();
        }
        if (repeated & KEY_DRIGHT) {
            save_confirmation_ = SaveConfirmation::None;
            if (save_control_ == SavePageControl::Upload) save_selection_.nextLocal();
            else save_selection_.nextRemote();
        }
        if (pressed & KEY_A) activateSaveControl();
    }

    bool ensureSaveDirectories(const std::string& directory) {
        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/PocketShelf", 0777);
        mkdir(romm3ds::config::kSaveDirectory, 0777);
        if (mkdir(directory.c_str(), 0777) != 0 && errno != EEXIST) {
            setStatus("Could not create managed save directory");
            return false;
        }
        return true;
    }

    std::string backupTimestamp() const {
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char timestamp[20];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local);
        return timestamp;
    }

    void downloadCheckpointSave(const romm3ds::RemoteSaveFile& remote) {
        if (remote.size > romm3ds::kMaxCheckpointPackageBytes) {
            setStatus("Checkpoint package exceeds the 512 KiB safe limit");
            return;
        }
        const std::string staging_directory = managedSaveDirectoryFor(entries_[selected_]);
        if (!ensureSaveDirectories(staging_directory)) return;
        const std::string temporary = staging_directory + "/.checkpoint.download.tmp";
        std::remove(temporary.c_str());
        FILE* file = std::fopen(temporary.c_str(), "wb");
        if (!file) { setStatus("Could not create Checkpoint download staging file"); return; }
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) {
            std::fclose(file); std::remove(temporary.c_str());
            setStatus("Not enough memory for Checkpoint download buffer"); return;
        }
        const std::string url = romm3ds::rommSaveContentUrl(settings_.base_url, remote.id);
        HttpRequest request;
        if (!beginHttp(request, url.c_str(), "Checkpoint package request failed")) {
            std::fclose(file); std::remove(temporary.c_str()); return;
        }
        transfer_phase_ = romm3ds::ui::TransferPhase::Generic;
        installing_ = true;
        progress_current_ = 0;
        progress_total_ = static_cast<u32>(remote.size);
        bool complete = false;
        Result result = 0;
        do {
            u32 downloaded = 0;
            result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (progress_current_ + downloaded > romm3ds::kMaxCheckpointPackageBytes ||
                (downloaded && std::fwrite(buffer.get(), 1, downloaded, file) != downloaded)) break;
            progress_current_ += downloaded;
            render();
            if (R_FAILED(result) && !isPending(result)) break;
            if (!isPending(result)) { request.markComplete(); complete = true; }
        } while (isPending(result));
        const bool closed = std::fclose(file) == 0;
        installing_ = false;
        if (!complete || !closed || progress_current_ != remote.size) {
            std::remove(temporary.c_str());
            setStatus("Checkpoint download failed; staging file removed");
            return;
        }
        romm3ds::CheckpointPackageError package_error = romm3ds::CheckpointPackageError::None;
        std::string restored;
        const std::string restore_name = "PocketShelf-" + backupTimestamp();
        const bool restored_ok = romm3ds::restoreCheckpointPackage(
            temporary, checkpoint_title_directory_, checkpoint_title_id_, restore_name,
            restored, package_error);
        std::remove(temporary.c_str());
        if (!restored_ok) {
            std::snprintf(status_, sizeof(status_), "Checkpoint restore: %s",
                          romm3ds::checkpointPackageErrorMessage(package_error));
            has_result_ = true;
            return;
        }
        refreshLocalSaves();
        setStatus("Remote package restored as a new Checkpoint backup");
    }

    void downloadSave() {
        if (remote_saves_.empty()) { setStatus("No remote save is available"); return; }
        if (!http_ready_) { setStatus("Save download disabled: HTTP unavailable"); return; }
        const auto remote = remote_saves_[save_selection_.remoteIndex()];
        if (checkpoint_title_id_ != 0 &&
            romm3ds::isCheckpointPackageFilename(remote.file_name, checkpoint_title_id_)) {
            downloadCheckpointSave(remote);
            return;
        }
        if (save_provider_ != SaveProvider::RomAdjacentSav || rom_adjacent_save_path_.empty()) {
            setStatus("No safe local save provider is bound for this ROM");
            return;
        }
        const std::string target = rom_adjacent_save_path_;
        const bool replacing = romm3ds::inspectLocalPayload(target).present;
        if (replacing && save_confirmation_ != SaveConfirmation::ReplaceLocal) {
            save_confirmation_ = SaveConfirmation::ReplaceLocal;
            setStatus("Local save exists. Tap DOWNLOAD again to back up and replace.");
            return;
        }
        save_confirmation_ = SaveConfirmation::None;
        FS_Archive archive{};
        if (R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
            u64 free_bytes = 0;
            const Result free_result = FSUSER_GetFreeBytes(&free_bytes, archive);
            FSUSER_CloseArchive(archive);
            if (R_SUCCEEDED(free_result) && free_bytes < remote.size) {
                setStatus("Not enough free SD space for save download");
                return;
            }
        }
        const std::string temporary = target + ".download.tmp";
        std::remove(temporary.c_str());
        FILE* file = std::fopen(temporary.c_str(), "wb");
        if (!file) { setStatus("Could not create temporary save file"); return; }
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) {
            std::fclose(file); std::remove(temporary.c_str());
            setStatus("Not enough memory for save download buffer"); return;
        }
        const std::string url = romm3ds::rommSaveContentUrl(settings_.base_url, remote.id);
        HttpRequest request;
        if (!beginHttp(request, url.c_str(), "Save content request failed")) {
            std::fclose(file); std::remove(temporary.c_str()); return;
        }
        transfer_phase_ = romm3ds::ui::TransferPhase::Generic;
        installing_ = true;
        progress_current_ = 0;
        progress_total_ = static_cast<u32>(std::min<std::uint64_t>(remote.size, UINT32_MAX));
        bool complete = false;
        Result result = 0;
        do {
            u32 downloaded = 0;
            result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (downloaded && std::fwrite(buffer.get(), 1, downloaded, file) != downloaded) break;
            progress_current_ += downloaded;
            render();
            if (R_FAILED(result) && !isPending(result)) break;
            if (!isPending(result)) { request.markComplete(); complete = true; }
        } while (isPending(result));
        const bool closed = std::fclose(file) == 0;
        installing_ = false;
        if (!complete || !closed || progress_current_ != remote.size) {
            std::remove(temporary.c_str());
            setStatus("Save download failed; temporary file removed");
            return;
        }
        std::string backup;
        if (replacing) {
            backup = romm3ds::availableBackupPath(target, backupTimestamp());
            if (std::rename(target.c_str(), backup.c_str()) != 0) {
                std::remove(temporary.c_str());
                setStatus("Could not create local save backup");
                return;
            }
        }
        if (std::rename(temporary.c_str(), target.c_str()) != 0) {
            if (!backup.empty()) std::rename(backup.c_str(), target.c_str());
            std::remove(temporary.c_str());
            setStatus("Could not activate downloaded save; backup restored");
            return;
        }
        refreshLocalSaves();
        setStatus(replacing ? "Remote .sav downloaded; previous adjacent save backed up" :
                              "Remote .sav downloaded beside the exact local ROM");
    }

    void uploadSave() {
        const bool checkpoint = save_provider_ == SaveProvider::Checkpoint &&
                                checkpoint_title_id_ != 0 && !checkpoint_backups_.empty();
        if (!checkpoint && local_saves_.empty()) {
            setStatus(save_provider_ == SaveProvider::Unsupported ?
                "No safe save provider is available for this ROM" :
                "No local ROM-adjacent .sav or Checkpoint backup is available");
            return;
        }
        if (!http_ready_) { setStatus("Save upload disabled: HTTP unavailable"); return; }

        std::string file_name;
        std::string file_path;
        std::uint64_t file_size = 0;
        std::string staged_package;
        if (checkpoint) {
            const std::string directory = managedSaveDirectoryFor(entries_[selected_]);
            if (!ensureSaveDirectories(directory)) return;
            staged_package = directory + "/.checkpoint-upload.tmp";
            std::remove(staged_package.c_str());
            romm3ds::CheckpointPackageError package_error = romm3ds::CheckpointPackageError::None;
            const auto& backup = checkpoint_backups_[save_selection_.localIndex()];
            if (!romm3ds::createCheckpointPackage(backup.path,
                                                   backup.name,
                                                   checkpoint_title_id_, staged_package,
                                                   package_error)) {
                std::snprintf(status_, sizeof(status_), "Checkpoint package: %s",
                              romm3ds::checkpointPackageErrorMessage(package_error));
                has_result_ = true;
                return;
            }
            struct stat info {};
            if (::stat(staged_package.c_str(), &info) != 0 || info.st_size < 0) {
                std::remove(staged_package.c_str());
                setStatus("Could not inspect staged Checkpoint package");
                return;
            }
            file_name = romm3ds::checkpointPackageFilename(checkpoint_title_id_);
            file_path = staged_package;
            file_size = static_cast<std::uint64_t>(info.st_size);
        } else {
            const auto& local = local_saves_[save_selection_.localIndex()];
            file_name = local.file_name;
            file_path = local.path;
            file_size = local.size;
        }
        const auto remove_staging = [&staged_package]() {
            if (!staged_package.empty()) std::remove(staged_package.c_str());
        };
        if (!romm3ds::isSafeVersionedUploadFilename(file_name)) {
            remove_staging(); setStatus("Save filename is too long for RomM versioning"); return;
        }
        if (file_size > romm3ds::kMaxSaveUploadBytes) {
            remove_staging(); setStatus("Save upload exceeds the 512 KiB 3DS HTTP limit"); return;
        }
        std::vector<u8> body;
        std::string content_type;
        if (!romm3ds::buildSaveUploadBodyFromFile(file_name, file_path,
                                                   static_cast<std::size_t>(file_size),
                                                   body, content_type)) {
            remove_staging();
            setStatus("Could not build bounded save upload");
            return;
        }
        remove_staging();
        const std::string url = romm3ds::rommSaveUploadUrl(
            settings_.base_url, entries_[selected_].rom_id);
        HttpRequest request;
        u32 status_code = 0;
        const Result begin = request.beginPost(url.c_str(), settings_.token, content_type.c_str(),
                                               std::move(body), status_code);
        if (R_FAILED(begin)) { setResultStatus("Save upload request failed", begin); return; }
        if (status_code != 200) {
            std::snprintf(status_, sizeof(status_), "Save upload failed (HTTP %lu)",
                          static_cast<unsigned long>(status_code));
            has_result_ = true;
            return;
        }
        std::string response;
        if (!receiveBounded(request, response, 64 * 1024, "Save upload response failed")) return;
        fetchRemoteSaves();
        setStatus(checkpoint ? "Checkpoint backup uploaded as a versioned RomM save" :
                               "Save uploaded as a versioned PocketShelf slot entry");
    }

    bool persistSettings(const romm3ds::Settings& candidate, bool reset_library = true) {
        romm3ds::SettingsError error = romm3ds::SettingsError::None;
        if (!romm3ds::saveSettingsAtomic(romm3ds::config::kSettingsPath, candidate, error)) {
            std::snprintf(status_, sizeof(status_), "Not saved: %s",
                          romm3ds::settingsErrorMessage(error));
            has_result_ = true;
            return false;
        }
        settings_ = candidate;
        type_filter_ = settings_.type_filter;
        if (reset_library) {
            entries_.clear();
            selected_ = 0;
            sort_applied_ = false;
            cover_.clear();
            cover_resource_key_.clear();
            details_scroll_.reset();
            std::remove(romm3ds::config::kCatalogCachePath);
            setStatus("Settings saved. Press X to connect.");
        }
        return true;
    }

    void editBaseUrl() {
        SwkbdState keyboard;
        swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2,
                  static_cast<int>(romm3ds::kMaxSettingsUrlBytes));
        swkbdSetHintText(&keyboard, "http://server:port");
        swkbdSetInitialText(&keyboard, settings_.base_url.c_str());
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Save", true);
        swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        char entered[romm3ds::kMaxSettingsUrlBytes + 1]{};
        if (swkbdInputText(&keyboard, entered, sizeof(entered)) != SWKBD_BUTTON_RIGHT) {
            setStatus("Base URL unchanged");
            return;
        }
        romm3ds::Settings candidate = settings_;
        candidate.base_url = entered;
        romm3ds::SettingsError error = romm3ds::SettingsError::None;
        if (!romm3ds::validateBaseUrl(candidate.base_url, error)) {
            std::snprintf(status_, sizeof(status_), "Not saved: %s",
                          romm3ds::settingsErrorMessage(error));
            has_result_ = true;
            return;
        }
        persistSettings(candidate);
    }

    void editToken() {
        SwkbdState keyboard;
        swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2,
                  static_cast<int>(romm3ds::kMaxSettingsTokenBytes));
        swkbdSetHintText(&keyboard, "RomM access token");
        swkbdSetInitialText(&keyboard, settings_.token.c_str());
        swkbdSetPasswordMode(&keyboard, SWKBD_PASSWORD_HIDE);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Save", true);
        swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        char entered[romm3ds::kMaxSettingsTokenBytes + 1]{};
        if (swkbdInputText(&keyboard, entered, sizeof(entered)) != SWKBD_BUTTON_RIGHT) {
            setStatus("Token unchanged");
            return;
        }
        romm3ds::Settings candidate = settings_;
        candidate.token = entered;
        romm3ds::SettingsError error = romm3ds::SettingsError::None;
        if (!romm3ds::validateToken(candidate.token, error)) {
            std::snprintf(status_, sizeof(status_), "Not saved: %s",
                          romm3ds::settingsErrorMessage(error));
            has_result_ = true;
            return;
        }
        persistSettings(candidate);
    }

    bool confirmDeferredUpdate(const romm3ds::UpdateManifest& manifest) {
        char prompt[192];
        std::snprintf(prompt, sizeof(prompt),
                      "Verified PocketShelf %s (%lu bytes). A keeps it staged; B deletes it. Installation remains manual.",
                      manifest.version_name.c_str(),
                      static_cast<unsigned long>(manifest.size));
        setStatus(prompt);
        while (aptMainLoop()) {
            hidScanInput();
            const u32 pressed = hidKeysDown();
            if (pressed & KEY_A) return true;
            if (pressed & (KEY_B | KEY_START)) return false;
            render();
        }
        return false;
    }

    bool stageSignedUpdate(const romm3ds::UpdateManifest& manifest,
                           std::string_view manifest_text) {
        constexpr char kUpdateDirectory[] = "sdmc:/3ds/PocketShelf/update";
        constexpr char kTemporaryPath[] = "sdmc:/3ds/PocketShelf/update/pocketshelf.cia.tmp";
        constexpr char kStagedPath[] = "sdmc:/3ds/PocketShelf/update/pocketshelf.cia";
        constexpr char kStatePath[] = "sdmc:/3ds/PocketShelf/update/pocketshelf-update-v1.txt";
        constexpr char kStateTemporaryPath[] =
            "sdmc:/3ds/PocketShelf/update/pocketshelf-update-v1.txt.tmp";
        constexpr char kTemporaryArchivePath[] =
            "/3ds/PocketShelf/update/pocketshelf.cia.tmp";

        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/PocketShelf", 0777);
        if (mkdir(kUpdateDirectory, 0777) != 0 && errno != EEXIST) {
            setStatus("Could not create the secure update staging directory");
            return false;
        }
        FS_Archive archive{};
        if (R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC,
                                           fsMakePath(PATH_EMPTY, "")))) {
            u64 free_bytes = 0;
            const Result free_result = FSUSER_GetFreeBytes(&free_bytes, archive);
            FSUSER_CloseArchive(archive);
            if (R_SUCCEEDED(free_result) && free_bytes < manifest.size) {
                setStatus("Not enough free SD space for the signed update");
                return false;
            }
        }
        std::remove(kTemporaryPath);
        std::remove(kStateTemporaryPath);
        FILE* output = std::fopen(kTemporaryPath, "wb");
        if (!output) { setStatus("Could not create the update staging file"); return false; }
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) {
            std::fclose(output); std::remove(kTemporaryPath);
            setStatus("Not enough memory for the update buffer");
            return false;
        }
        HttpRequest request;
        if (!beginHttp(request, manifest.cia_url.c_str(), "Update CIA request failed", false)) {
            std::fclose(output); std::remove(kTemporaryPath); return false;
        }
        u32 already = 0, reported_total = 0;
        if (R_FAILED(request.downloadState(already, reported_total)) ||
            (reported_total != 0 && reported_total != manifest.size)) {
            std::fclose(output); std::remove(kTemporaryPath);
            setStatus("Update rejected: Content-Length disagrees with signed size");
            return false;
        }

        romm3ds::UpdateArtifactVerifier verifier(manifest.size, manifest.sha256);
        transfer_phase_ = romm3ds::ui::TransferPhase::Generic;
        installing_ = true;
        progress_current_ = 0;
        progress_total_ = static_cast<u32>(manifest.size);
        bool complete = false, cancelled = false, failed = false;
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_B) { cancelled = true; break; }
            u32 downloaded = 0;
            const Result result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (downloaded != 0) {
                std::size_t written = 0;
                while (written < downloaded) {
                    const std::size_t count = std::fwrite(buffer.get() + written, 1,
                                                          downloaded - written, output);
                    if (count == 0) { failed = true; break; }
                    written += count;
                }
                if (failed) break;
                verifier.update(buffer.get(), downloaded);
                if (verifier.received() > manifest.size) { failed = true; break; }
                progress_current_ = static_cast<u32>(verifier.received());
                render();
            }
            if (R_FAILED(result) && !isPending(result)) { failed = true; break; }
            if (!isPending(result)) { request.markComplete(); complete = true; break; }
        }
        const bool file_flushed = std::fflush(output) == 0;
        const bool file_closed = std::fclose(output) == 0;
        const bool file_ok = file_flushed && file_closed;
        installing_ = false;
        if (cancelled || !complete || failed || !file_ok ||
            verifier.finish() != romm3ds::UpdateArtifactStatus::Valid) {
            std::remove(kTemporaryPath);
            setStatus(cancelled ? "Update staging cancelled; partial file removed" :
                                  "Update rejected: staged size or SHA-256 failed");
            return false;
        }

        Handle staged_handle = 0;
        Result result = FSUSER_OpenFileDirectly(&staged_handle, ARCHIVE_SDMC,
                                                fsMakePath(PATH_EMPTY, ""),
                                                fsMakePath(PATH_ASCII, kTemporaryArchivePath),
                                                FS_OPEN_READ, 0);
        if (R_FAILED(result)) {
            std::remove(kTemporaryPath);
            setResultStatus("Could not inspect staged update CIA", result);
            return false;
        }
        AM_TitleInfo package{};
        const Result info_result = AM_GetCiaFileInfo(MEDIATYPE_SD, &package, staged_handle);
        const Result close_result = FSFILE_Close(staged_handle);
        if (R_FAILED(info_result) || R_FAILED(close_result) ||
            romm3ds::evaluateStagedUpdate(true, true, manifest.title_id, manifest.version,
                                          package.titleID, package.version, false) !=
                romm3ds::StagedUpdatePolicy::AwaitingUserConfirmation) {
            std::remove(kTemporaryPath);
            setStatus("Update rejected: CIA title ID or version metadata mismatch");
            return false;
        }
        if (!confirmDeferredUpdate(manifest)) {
            std::remove(kTemporaryPath);
            setStatus("Verified update deleted; installed PocketShelf was unchanged");
            return false;
        }

        FILE* state = std::fopen(kStateTemporaryPath, "wb");
        const bool state_written = state &&
            std::fwrite(manifest_text.data(), 1, manifest_text.size(), state) == manifest_text.size();
        const bool state_flushed = state_written && std::fflush(state) == 0;
        const bool state_closed = state && std::fclose(state) == 0;
        if (!state_written || !state_flushed || !state_closed) {
            std::remove(kStateTemporaryPath); std::remove(kTemporaryPath);
            setStatus("Could not persist deferred update state; staged CIA removed");
            return false;
        }
        std::remove(kStagedPath);
        std::remove(kStatePath);
        if (std::rename(kTemporaryPath, kStagedPath) != 0 ||
            std::rename(kStateTemporaryPath, kStatePath) != 0) {
            std::remove(kTemporaryPath); std::remove(kStateTemporaryPath);
            std::remove(kStagedPath); std::remove(kStatePath);
            setStatus("Could not commit deferred update state; staging removed");
            return false;
        }
        setStatus("Signed CIA staged and reverified; manual recovery/install is deferred");
        return true;
    }

    void checkForUpdate() {
        if (!romm3ds::release::kConfigured) {
            setStatus("Updates disabled: configure a production HTTPS origin and Ed25519 public key");
            return;
        }
        if (!http_ready_) { setStatus("Update check disabled: HTTP service unavailable"); return; }
        if (!am_ready_) { setStatus("Update staging disabled: AM metadata service unavailable"); return; }
        setStatus("Checking signed update metadata...");
        render();
        HttpRequest request;
        const std::string url = romm3ds::updateManifestUrl(romm3ds::release::kReleaseBaseUrl);
        if (!beginHttp(request, url.c_str(), "Update manifest request failed", false)) return;
        std::string manifest_text;
        if (!receiveBounded(request, manifest_text, romm3ds::kMaxUpdateManifestBytes,
                            "Update manifest download failed")) return;
        romm3ds::UpdateManifest manifest;
        romm3ds::UpdateManifestError error = romm3ds::UpdateManifestError::None;
        if (!romm3ds::parseUpdateManifest(manifest_text, manifest, error)) {
            std::snprintf(status_, sizeof(status_), "Update rejected: %s",
                          romm3ds::updateManifestErrorMessage(error));
            has_result_ = true;
            return;
        }
        const auto policy = romm3ds::evaluateUpdatePolicy(
            manifest, kAppVersion, kAppTitleId, url, romm3ds::release::trustedUpdateKey());
        if (policy != romm3ds::UpdatePolicy::ReadyToStage) {
            setStatus(romm3ds::updatePolicyMessage(policy));
            has_result_ = true;
            return;
        }
        stageSignedUpdate(manifest, manifest_text);
    }

    void renderLibraryTop() {
        drawText(16, 10, 0.42f, kAccent, "POCKETSHELF  /  ROMM");
        char count_line[romm3ds::ui::kCountBufferSize];
        std::snprintf(count_line, sizeof(count_line), "%lu file%s",
                      static_cast<unsigned long>(entries_.size()), entries_.size() == 1 ? "" : "s");
        drawText(322, 12, 0.34f, kMuted, count_line);

        if (entries_.empty()) {
            C2D_DrawRectSolid(48, 55, 0.1f, 304, 126, kPanel);
            drawText(80, 84, 0.62f, kText, "No cached catalogue");
            drawText(64, 116, 0.40f, kMuted, "Enter the RomM URL and token in Settings,");
            drawText(100, 137, 0.40f, kMuted, "then press X to refresh.");
        } else {
            const auto& entry = entries_[selected_];
            C2D_DrawRectSolid(14, 64, 0.1f, 72, 108, kPanel);
            C2D_DrawRectSolid(314, 64, 0.1f, 72, 108, kPanel);
            drawText(39, 110, 0.52f, kMuted, selected_ > 0 ? "<" : " ");
            drawText(342, 110, 0.52f, kMuted, selected_ + 1 < entries_.size() ? ">" : " ");
            if (selected_ > 0) drawWrappedText(19, 179, 11, 0.28f, kMuted, entries_[selected_ - 1].title, 12, 2);
            if (selected_ + 1 < entries_.size()) drawWrappedText(309, 179, 11, 0.28f, kMuted, entries_[selected_ + 1].title, 12, 2);

            C2D_DrawRectSolid(116, 39, 0.1f, 168, 158, kPanelRaised);
            if (cover_.ready()) {
                const float scale = cover_.displayScale(152.0f, 146.0f);
                C2D_DrawImageAt(cover_.image(), 124, 45, 0.3f, nullptr, scale, scale);
            } else {
                drawWrappedText(140, 100, 13, 0.34f, kMuted, cover_status_, 18, 4);
            }
            drawWrappedText(88, 202, 13, 0.42f, kText, entry.title, 31, 2);
        }
    }

    void drawNavigationRail() {
        for (std::size_t i = 0; i < 4; ++i) {
            const auto page = static_cast<romm3ds::ui::BottomPage>(i);
            const bool active = bottom_navigation_.page() == page;
            const float y = static_cast<float>(romm3ds::ui::kBottomRailY +
                                               static_cast<int>(i) *
                                               romm3ds::ui::kBottomRailStride);
            C2D_DrawRectSolid(romm3ds::ui::kBottomRailX, y, 0.1f,
                              romm3ds::ui::kBottomRailWidth,
                              romm3ds::ui::kBottomRailHeight,
                              active ? kPanelRaised : kPanel);
            C2D_DrawRectSolid(romm3ds::ui::kBottomRailX, y, 0.2f, 4,
                              romm3ds::ui::kBottomRailHeight,
                              active ? kAccent : kPanel);
            drawText(i == 2 ? 14 : 20, y + 7, i == 2 ? 0.42f : 0.52f,
                     active ? kAccent : kMuted, romm3ds::ui::bottomPageGlyph(page));
            drawText(10, y + 34, romm3ds::ui::kBottomLabelScale,
                     active ? kText : kMuted,
                     romm3ds::ui::bottomPageName(page));
        }
    }

    void drawControlCard(float y, float height, romm3ds::ui::BottomControl control,
                         const char* label, const char* value) {
        const bool focused = bottom_navigation_.focused() == control;
        C2D_DrawRectSolid(romm3ds::ui::kBottomContentX, y, 0.1f,
                          romm3ds::ui::kBottomContentWidth, height,
                          focused ? kPanelRaised : kPanel);
        C2D_DrawRectSolid(romm3ds::ui::kBottomContentX, y, 0.2f, 4, height,
                          focused ? kAccent : kPanel);
        drawText(72, y + 3, romm3ds::ui::kBottomLabelScale,
                 focused ? kWarm : kMuted, label);
        drawText(72, y + 18, romm3ds::ui::kBottomBodyScale, kText, value);
    }

    void drawDetailsButton(float x, float y, float width,
                           romm3ds::ui::BottomControl control, const char* label,
                           bool enabled = true) {
        const bool focused = bottom_navigation_.focused() == control;
        C2D_DrawRectSolid(x, y, 0.1f, width, romm3ds::ui::kDetailsActionHeight,
                          focused ? kPanelRaised : kPanel);
        C2D_DrawRectSolid(x, y, 0.2f, 4, romm3ds::ui::kDetailsActionHeight,
                          focused ? kAccent : kPanel);
        drawText(x + 12, y + 7, 0.36f, enabled ? kAccent : kMuted, label);
    }

    void renderScreenshotsBottom() {
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "SCREENSHOTS");
        if (entries_.empty()) {
            drawWrappedText(60, 45, 16, romm3ds::ui::kBottomBodyScale, kMuted,
                            "No game selected.", 34, 2);
        } else {
            const auto& entry = entries_[selected_];
            drawWrappedText(60, 29, 15, 0.40f, kText, entry.title, 34, 1);
            char counter[48];
            std::snprintf(counter, sizeof(counter), "%lu / %lu",
                          static_cast<unsigned long>(screenshot_selection_.empty() ? 0 :
                                                     screenshot_selection_.index() + 1),
                          static_cast<unsigned long>(screenshot_selection_.count()));
            drawText(60, 60, 0.52f, screenshot_selection_.empty() ? kMuted : kWarm, counter);
            drawWrappedText(60, 88, 16, romm3ds::ui::kBottomBodyScale, kText,
                            screenshot_selection_.empty() ?
                                "No screenshots are available for this game." :
                                "The selected screenshot is shown on the top screen. Use Left or Right, or tap a button below.",
                            34, 4);
        }
        const int right_x = romm3ds::ui::kBottomContentX +
                            romm3ds::ui::kDetailsActionWidth + romm3ds::ui::kDetailsActionGap;
        drawDetailsButton(romm3ds::ui::kBottomContentX, romm3ds::ui::kScreenshotsActionsY,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::ScreenshotsPrevious, "< PREVIOUS",
                          !screenshot_selection_.empty());
        drawDetailsButton(right_x, romm3ds::ui::kScreenshotsActionsY,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::ScreenshotsNext, "NEXT >",
                          !screenshot_selection_.empty());
        drawDetailsButton(romm3ds::ui::kBottomContentX, romm3ds::ui::kScreenshotsBackY,
                          romm3ds::ui::kBottomContentWidth,
                          romm3ds::ui::BottomControl::DetailsBack, "BACK TO INFO");
    }

    void drawManageRow(std::size_t index, romm3ds::ui::BottomControl control,
                       const char* label, const char* value) {
        const float y = static_cast<float>(romm3ds::ui::kManageRowY +
                                           static_cast<int>(index) * romm3ds::ui::kManageRowStride);
        const bool focused = bottom_navigation_.focused() == control;
        C2D_DrawRectSolid(romm3ds::ui::kBottomContentX, y, 0.1f,
                          romm3ds::ui::kBottomContentWidth, romm3ds::ui::kManageRowHeight,
                          focused ? kPanelRaised : kPanel);
        C2D_DrawRectSolid(romm3ds::ui::kBottomContentX, y, 0.2f, 4,
                          romm3ds::ui::kManageRowHeight, focused ? kAccent : kPanel);
        drawText(70, y + 4, 0.34f, focused ? kWarm : kMuted, label);
        drawText(220, y + 4, 0.34f, kText, value);
    }

    void renderManageBottom() {
        drawText(60, 4, 0.46f, kAccent, "ROMM / MANAGE");
        drawText(214, 7, 0.27f, kMuted, "FAVORITE DEFERRED");
        drawWrappedText(60, 22, 12, 0.29f, has_result_ ? kDanger : kMuted,
                        status_, 42, 1);
        if (entries_.empty()) {
            drawText(60, 52, romm3ds::ui::kBottomBodyScale, kMuted, "No game selected");
            return;
        }
        const auto& entry = entries_[selected_];
        char rating[12], difficulty[12], completion[12];
        std::snprintf(rating, sizeof(rating), "%u / 10", entry.rating);
        std::snprintf(difficulty, sizeof(difficulty), "%u / 10", entry.difficulty);
        std::snprintf(completion, sizeof(completion), "%u%%", entry.completion);
        drawManageRow(0, romm3ds::ui::BottomControl::ManageNowPlaying,
                      "NOW PLAYING", entry.now_playing ? "ON" : "OFF");
        drawManageRow(1, romm3ds::ui::BottomControl::ManageLastPlayed,
                      "LAST PLAYED", entry.has_last_played ? "SET" : "NOT SET");
        drawManageRow(2, romm3ds::ui::BottomControl::ManageBacklogged,
                      "BACKLOG", entry.backlogged ? "YES" : "NO");
        drawManageRow(3, romm3ds::ui::BottomControl::ManagePlayStatus,
                      "PLAY STATUS", entry.status.empty() ? "NOT SET" : entry.status.c_str());
        drawManageRow(4, romm3ds::ui::BottomControl::ManageRating, "RATING", rating);
        drawManageRow(5, romm3ds::ui::BottomControl::ManageDifficulty, "DIFFICULTY", difficulty);
        drawManageRow(6, romm3ds::ui::BottomControl::ManageCompletion, "COMPLETION", completion);
        drawDetailsButton(romm3ds::ui::kBottomContentX, romm3ds::ui::kManageBackY,
                          romm3ds::ui::kBottomContentWidth,
                          romm3ds::ui::BottomControl::DetailsBack, "BACK TO INFO");
    }

    void renderDetailsBottom() {
        if (bottom_navigation_.detailsView() == romm3ds::ui::DetailsView::Screenshots) {
            renderScreenshotsBottom();
            return;
        }
        if (bottom_navigation_.detailsView() == romm3ds::ui::DetailsView::Manage) {
            renderManageBottom();
            return;
        }
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "GAME INFO");
        if (entries_.empty()) {
            details_scroll_.setLineCounts(0, romm3ds::ui::kDetailsMetadataLines);
            drawWrappedText(60, 45, 18, romm3ds::ui::kBottomBodyScale, kMuted,
                            "No game selected. Browse with Left or Right.", 32, 2);
        } else {
            const auto& entry = entries_[selected_];
            drawWrappedText(60, 25, 15, 0.42f, kText, entry.title, 34, 1);
            std::string device_state;
            if (entry.action == romm3ds::CatalogAction::Download) {
                const std::string path = romm3ds::downloadDirectoryForType(entry.type) + "/" +
                                         entry.file_name;
                const auto state = romm3ds::inspectLocalPayload(path);
                device_state = state.present ? "On SD" : "Not downloaded";
            } else {
                std::uint64_t title_id = 0;
                const bool receipt = romm3ds::loadInstallReceipt(
                    romm3ds::installReceiptPath(romm3ds::config::kInstallReceiptDirectory,
                                                entry.file_id), title_id);
                const bool installed = receipt && std::binary_search(
                    installed_title_ids_.begin(), installed_title_ids_.end(), title_id);
                device_state = receipt ? (installed ? "Installed" : "Not installed") :
                                         "Install state unknown";
            }
            const auto detail_lines = romm3ds::ui::buildInfoLines(
                entry, entry.action == romm3ds::CatalogAction::InstallCia ? "CIA" : entry.type,
                device_state);
            details_scroll_.setLineCounts(detail_lines.size(), romm3ds::ui::kDetailsMetadataLines);
            const std::size_t first = details_scroll_.offset();
            const std::size_t count = std::min<std::size_t>(
                romm3ds::ui::kDetailsMetadataLines, detail_lines.size() - first);
            for (std::size_t line = 0; line < count; ++line) {
                const auto& info = detail_lines[first + line];
                const float y = romm3ds::ui::kDetailsMetadataY +
                    static_cast<float>(line * romm3ds::ui::kDetailsMetadataLineHeight);
                if (info.kind == romm3ds::ui::InfoLineKind::Section) {
                    drawText(romm3ds::ui::kDetailsMetadataTextX, y,
                             romm3ds::ui::kDetailsMetadataScale, kAccent,
                             info.value.c_str());
                } else if (info.kind == romm3ds::ui::InfoLineKind::Field) {
                    drawText(romm3ds::ui::kDetailsMetadataTextX, y,
                             romm3ds::ui::kDetailsMetadataScale, kMuted,
                             info.label.c_str());
                    drawText(romm3ds::ui::kDetailsMetadataValueX, y,
                             romm3ds::ui::kDetailsMetadataScale, kText,
                             info.value.c_str());
                } else if (info.kind == romm3ds::ui::InfoLineKind::Continuation) {
                    drawText(romm3ds::ui::kDetailsMetadataValueX, y,
                             romm3ds::ui::kDetailsMetadataScale, kText,
                             info.value.c_str());
                } else {
                    drawText(romm3ds::ui::kDetailsMetadataTextX, y,
                             romm3ds::ui::kDetailsMetadataScale, kText,
                             info.value.c_str());
                }
            }
            if (details_scroll_.maxOffset() != 0) {
                drawText(romm3ds::ui::kDetailsMetadataScrollX,
                         romm3ds::ui::kDetailsMetadataY, 0.30f, kMuted,
                         details_scroll_.offset() == 0 ? "v" : "^");
                drawText(romm3ds::ui::kDetailsMetadataScrollX,
                         romm3ds::ui::kDetailsActionsY - 10, 0.30f, kMuted,
                         details_scroll_.offset() == details_scroll_.maxOffset() ? "^" : "v");
            }
        }
        const int right_x = romm3ds::ui::kBottomContentX +
                            romm3ds::ui::kDetailsActionWidth + romm3ds::ui::kDetailsActionGap;
        drawDetailsButton(romm3ds::ui::kBottomContentX, romm3ds::ui::kDetailsActionsY,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::DetailsSaveData, "SAVE DATA", !entries_.empty());
        drawDetailsButton(right_x, romm3ds::ui::kDetailsActionsY,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::DetailsManage, "ROMM / MANAGE", !entries_.empty());
        drawDetailsButton(romm3ds::ui::kBottomContentX,
                          romm3ds::ui::kDetailsActionsY + romm3ds::ui::kDetailsActionStride,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::DetailsScreenshots, "SCREENSHOTS", !entries_.empty());
        drawDetailsButton(right_x,
                          romm3ds::ui::kDetailsActionsY + romm3ds::ui::kDetailsActionStride,
                          romm3ds::ui::kDetailsActionWidth,
                          romm3ds::ui::BottomControl::DetailsTransfer,
                          entries_.empty() ? "UNAVAILABLE" : "INSTALL / GET", !entries_.empty());
    }

    void renderFiltersBottom() {
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "FIND GAMES");
        drawText(60, 30, romm3ds::ui::kBottomCaptionScale, kMuted, "Tap a large card to edit");
        const std::string type = type_filter_.empty() ? "All supported files" : type_filter_;
        char search[42];
        std::snprintf(search, sizeof(search), "%.38s", search_.empty() ? "All titles" : search_.c_str());
        drawControlCard(romm3ds::ui::kFilterTypeY, romm3ds::ui::kFilterCardHeight,
                        romm3ds::ui::BottomControl::FilterType,
                        "FILE TYPE", type.c_str());
        drawControlCard(romm3ds::ui::kFilterSearchY, romm3ds::ui::kFilterCardHeight,
                        romm3ds::ui::BottomControl::FilterSearch,
                        "TITLE SEARCH", search);
        char metadata[42];
        std::snprintf(metadata, sizeof(metadata), "%.38s",
                      metadata_filter_text_.empty() ? "All metadata" : metadata_filter_text_.c_str());
        drawControlCard(romm3ds::ui::kFilterMetadataY, romm3ds::ui::kFilterCardHeight,
                        romm3ds::ui::BottomControl::FilterMetadata,
                        "METADATA", metadata);
        drawBottomStatus(romm3ds::ui::kBottomStatusY);
    }

    void renderSortBottom() {
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "SORT GAMES");
        drawText(60, 31, romm3ds::ui::kBottomCaptionScale, kMuted, "No network refresh needed");
        drawControlCard(romm3ds::ui::kSortCardY, romm3ds::ui::kSortCardHeight,
                        romm3ds::ui::BottomControl::SortTitle,
                        "TITLE", sort_applied_ ? (sort_ascending_ ? "A to Z" : "Z to A") :
                                                  "Server order - tap to sort");
        drawWrappedText(60, 148, 18, romm3ds::ui::kBottomBodyScale, kText,
                        "Tap again to reverse. Left or Right browses games.", 32, 2);
    }

    void renderSettingsBottom() {
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "SETUP");
        drawText(251, 8, romm3ds::ui::kBottomCaptionScale, kMuted, "v0.6.1");
        char endpoint[42];
        std::snprintf(endpoint, sizeof(endpoint), "%.38s%s", settings_.base_url.c_str(),
                      settings_.base_url.size() > 38 ? "..." : "");
        drawControlCard(romm3ds::ui::kSettingsUrlY, romm3ds::ui::kSettingsCardHeight,
                        romm3ds::ui::BottomControl::SettingsUrl,
                        "ROMM BASE URL", endpoint);
        drawControlCard(romm3ds::ui::kSettingsTokenY, romm3ds::ui::kSettingsCardHeight,
                        romm3ds::ui::BottomControl::SettingsToken,
                        "ACCESS TOKEN", settings_.token.empty() ? "Not set" : "Stored on SD");
        drawControlCard(romm3ds::ui::kSettingsThemeY, romm3ds::ui::kSettingsCardHeight,
                        romm3ds::ui::BottomControl::SettingsTheme,
                        "THEME", settings_.theme == romm3ds::Theme::Light ? "LIGHT" : "DARK");
        drawControlCard(romm3ds::ui::kSettingsConnectY, romm3ds::ui::kSettingsCardHeight,
                        romm3ds::ui::BottomControl::SettingsConnect,
                        "CONNECTION", "CONNECT / REFRESH");
        drawControlCard(romm3ds::ui::kSettingsUpdateY, romm3ds::ui::kSettingsCardHeight,
                        romm3ds::ui::BottomControl::SettingsUpdate,
                        "UPDATE", "CHECK SIGNED UPDATE");
        drawBottomStatus(romm3ds::ui::kBottomStatusY);
    }

    void renderTypeChooserBottom() {
        drawNavigationRail();
        drawText(60, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "FILE TYPE");
        drawText(222, 9, romm3ds::ui::kBottomCaptionScale, kMuted, "Tap one");
        if (!type_chooser_) return;
        for (std::size_t row = 0; row < type_chooser_->visibleCount(); ++row) {
            const std::size_t i = type_chooser_->firstVisible() + row;
            const float y = static_cast<float>(romm3ds::ui::kTypeChooserY +
                                               static_cast<int>(row) *
                                               romm3ds::ui::kTypeChooserStride);
            const bool active = i == type_chooser_->selected();
            C2D_DrawRectSolid(58, y, 0.1f, 252, romm3ds::ui::kTypeChooserHeight,
                              active ? kPanelRaised : kBackground);
            if (active) C2D_DrawRectSolid(58, y, 0.2f, 4,
                                          romm3ds::ui::kTypeChooserHeight, kAccent);
            const auto value = romm3ds::ui::kTypeChoices[i];
            char label[24];
            if (value.empty()) std::snprintf(label, sizeof(label), "All supported");
            else std::snprintf(label, sizeof(label), "%.*s", static_cast<int>(value.size()), value.data());
            drawText(70, y + 4, romm3ds::ui::kBottomBodyScale,
                     active ? kText : kMuted, label);
        }
        constexpr float scroll_y = static_cast<float>(romm3ds::ui::kTypeChooserY);
        constexpr float scroll_height = static_cast<float>(
            romm3ds::ui::TypeChooserState::kVisibleRows * romm3ds::ui::kTypeChooserStride - 3);
        const float thumb_height = scroll_height *
            static_cast<float>(romm3ds::ui::TypeChooserState::kVisibleRows) /
            static_cast<float>(romm3ds::ui::TypeChooserState::count());
        const std::size_t max_first = romm3ds::ui::TypeChooserState::count() -
                                      romm3ds::ui::TypeChooserState::kVisibleRows;
        const float thumb_y = scroll_y + (scroll_height - thumb_height) *
            static_cast<float>(type_chooser_->firstVisible()) / static_cast<float>(max_first);
        C2D_DrawRectSolid(306, scroll_y, 0.3f, 3, scroll_height, kPanel);
        C2D_DrawRectSolid(306, thumb_y, 0.4f, 3, thumb_height, kAccent);
        drawText(60, 220, romm3ds::ui::kBottomCaptionScale, kMuted,
                 "A apply   B cancel   Slide to scroll");
    }

    void renderTitlePickerBottom() {
        drawText(12, 7, romm3ds::ui::kBottomHeadingScale, kAccent, "CHECKPOINT TITLE PICKER");
        drawText(12, 28, romm3ds::ui::kBottomCaptionScale, kMuted,
                 "Installed SD applications with one matching Checkpoint folder");
        if (!title_picker_ || title_picker_->empty()) {
            drawWrappedText(20, 67, 15, 0.34f, kMuted,
                "No automatic matches. Press X only if you need the manual title-ID fallback.",
                39, 4);
        } else {
            for (std::size_t row = 0; row < title_picker_->visibleCount(); ++row) {
                const auto* choice = title_picker_->visible(row);
                if (!choice) continue;
                const bool active = title_picker_->firstVisible() + row == title_picker_->selected();
                const float y = 42.0f + static_cast<float>(row) * 21.0f;
                C2D_DrawRectSolid(12, y, 0.1f, 296, 19, active ? kPanelRaised : kPanel);
                if (active) C2D_DrawRectSolid(12, y, 0.2f, 4, 19, kAccent);
                char name[31];
                std::snprintf(name, sizeof(name), "%.27s%s", choice->display_name.c_str(),
                              choice->display_name.size() > 27 ? "..." : "");
                drawText(21, y + 2, romm3ds::ui::kBottomBodyScale, active ? kText : kMuted, name);
                char title_id[22];
                std::snprintf(title_id, sizeof(title_id), "%016llX",
                              static_cast<unsigned long long>(choice->title_id));
                drawText(202, y + 3, romm3ds::ui::kBottomCaptionScale, active ? kWarm : kMuted, title_id);
            }
        }
        const bool can_confirm = title_picker_ && !title_picker_->empty();
        C2D_DrawRectSolid(12, 198, 0.1f, 143, 30, kPanelRaised);
        C2D_DrawRectSolid(165, 198, 0.1f, 143, 30, kPanel);
        drawText(25, 205, romm3ds::ui::kBottomBodyScale, can_confirm ? kAccent : kMuted, "CONFIRM ID");
        drawText(207, 205, romm3ds::ui::kBottomBodyScale, kMuted, "CANCEL");
        drawText(12, 184, romm3ds::ui::kBottomCaptionScale, kMuted,
                 "A confirm   B cancel   X manual");
    }

    void renderSaveDataBottom() {
        drawText(12, 5, romm3ds::ui::kBottomHeadingScale, kAccent, "SAVE DATA");
        if (entries_.empty()) {
            drawText(12, 38, romm3ds::ui::kBottomBodyScale, kMuted, "No game selected");
            return;
        }
        drawWrappedText(12, 29, romm3ds::ui::kBottomLineHeight,
                        romm3ds::ui::kBottomBodyScale, kText,
                        entries_[selected_].title, 39, 1);
        const bool checkpoint = save_provider_ == SaveProvider::Checkpoint;
        drawText(12, 45, romm3ds::ui::kBottomLabelScale, kMuted,
                 checkpoint ? "CHECKPOINT PROVIDER  (Y change)" :
                 save_provider_ == SaveProvider::RomAdjacentSav ?
                              "ROM-ADJACENT .SAV  (exact ROM binding)" :
                 entries_[selected_].action == romm3ds::CatalogAction::InstallCia ?
                              "CHECKPOINT NOT BOUND  (Y choose title)" :
                              "UNSUPPORTED: no exact local .sav binding");
        const std::size_t local_count = selectableLocalSaveCount();
        const bool have_local = local_count != 0;
        C2D_DrawRectSolid(12, romm3ds::ui::kSaveLocalY, 0.1f, 296,
                          romm3ds::ui::kSaveCardHeight,
                          save_control_ == SavePageControl::Upload ? kPanelRaised : kPanel);
        drawText(20, romm3ds::ui::kSaveLocalY + 4,
                 romm3ds::ui::kBottomLabelScale, kWarm, "LOCAL BACKUP");
        char local_line[72];
        std::snprintf(local_line, sizeof(local_line), "%lu/%lu  %.38s",
                      static_cast<unsigned long>(have_local ? save_selection_.localIndex() + 1 : 0),
                      static_cast<unsigned long>(local_count),
                      have_local ? (checkpoint ? checkpoint_backups_[save_selection_.localIndex()].name.c_str() :
                                                local_saves_[save_selection_.localIndex()].file_name.c_str()) :
                                   "None");
        drawText(20, romm3ds::ui::kSaveLocalY + 22,
                 romm3ds::ui::kBottomBodyScale, have_local ? kText : kMuted, local_line);
        C2D_DrawRectSolid(12, romm3ds::ui::kSaveRemoteY, 0.1f, 296,
                          romm3ds::ui::kSaveCardHeight,
                          save_control_ == SavePageControl::Download ? kPanelRaised : kPanel);
        drawText(20, romm3ds::ui::kSaveRemoteY + 4,
                 romm3ds::ui::kBottomLabelScale, kWarm, "ROMM VERSION");
        char remote_line[72];
        std::snprintf(remote_line, sizeof(remote_line), "%lu/%lu  %.14s  %.22s",
                      static_cast<unsigned long>(remote_saves_.empty() ? 0 :
                                                 save_selection_.remoteIndex() + 1),
                      static_cast<unsigned long>(remote_saves_.size()),
                      remote_saves_.empty() ? "" :
                          remote_saves_[save_selection_.remoteIndex()].updated_at.c_str(),
                      remote_saves_.empty() ? "None" :
                          remote_saves_[save_selection_.remoteIndex()].file_name.c_str());
        drawText(20, romm3ds::ui::kSaveRemoteY + 22,
                 romm3ds::ui::kBottomBodyScale,
                 remote_saves_.empty() ? kMuted : kText, remote_line);

        const auto button = [this](float x, float y, float width, SavePageControl control,
                                   const char* label, bool enabled) {
            const bool focused = save_control_ == control;
            C2D_DrawRectSolid(x, y, 0.1f, width, romm3ds::ui::kSaveActionHeight,
                              focused ? kPanelRaised : kPanel);
            C2D_DrawRectSolid(x, y, 0.2f, 4, romm3ds::ui::kSaveActionHeight,
                              focused ? kAccent : kPanel);
            drawText(x + 12, y + 7, 0.38f, enabled ? kAccent : kMuted, label);
        };
        drawWrappedText(12, romm3ds::ui::kSaveStatusY, 13,
                        romm3ds::ui::kBottomStatusScale,
                        has_result_ ? kDanger : kMuted, status_, 48, 1);
        button(12, romm3ds::ui::kSavePrimaryActionsY, 143,
               SavePageControl::Download, "DOWNLOAD", !remote_saves_.empty());
        button(165, romm3ds::ui::kSavePrimaryActionsY, 143,
               SavePageControl::Upload, "UPLOAD", have_local);
        button(12, romm3ds::ui::kSaveSecondaryActionsY, 143,
               SavePageControl::Refresh, "REFRESH", http_ready_);
        button(165, romm3ds::ui::kSaveSecondaryActionsY, 143,
               SavePageControl::Back, "BACK", true);
    }

    void renderOverwritePromptBottom() {
        C2D_TargetClear(bottom_, kBackground);
        C2D_SceneBegin(bottom_);
        drawText(16, 10, romm3ds::ui::kBottomHeadingScale, kDanger, "OVERWRITE INSTALLED TITLE?");
        char title_id[32];
        std::snprintf(title_id, sizeof(title_id), "Title ID  %016llX",
                      static_cast<unsigned long long>(overwrite_title_id_));
        drawText(16, 48, romm3ds::ui::kBottomBodyScale, kText, title_id);
        drawWrappedText(16, 78, 15, romm3ds::ui::kBottomBodyScale, kWarm,
                        "The complete staged CIA was validated. A will replace this exact installed title. B cancels before AM overwrite starts.",
                        39, 5);
        C2D_DrawRectSolid(16, 176, 0.1f, 136, 42, kPanelRaised);
        C2D_DrawRectSolid(168, 176, 0.1f, 136, 42, kPanel);
        drawText(42, 188, romm3ds::ui::kBottomBodyScale, kDanger, "A  OVERWRITE");
        drawText(202, 188, romm3ds::ui::kBottomBodyScale, kAccent, "B  CANCEL");
    }

    bool confirmCiaOverwrite(std::uint64_t title_id) {
        overwrite_title_id_ = title_id;
        overwrite_prompt_ = true;
        while (aptMainLoop()) {
            hidScanInput();
            const u32 pressed = hidKeysDown();
            render();
            if (pressed & KEY_A) { overwrite_prompt_ = false; return true; }
            if (pressed & (KEY_B | KEY_START)) { overwrite_prompt_ = false; return false; }
        }
        overwrite_prompt_ = false;
        return false;
    }

    void renderBottom() {
        if (overwrite_prompt_) {
            renderOverwritePromptBottom();
            return;
        }
        C2D_TargetClear(bottom_, kBackground);
        C2D_SceneBegin(bottom_);
        if (screen_ == Screen::TypeChooser) {
            renderTypeChooserBottom();
            return;
        }
        if (screen_ == Screen::TitlePicker) {
            renderTitlePickerBottom();
            return;
        }
        if (screen_ == Screen::SaveData) {
            renderSaveDataBottom();
            return;
        }
        drawNavigationRail();
        switch (bottom_navigation_.page()) {
            case romm3ds::ui::BottomPage::Details: renderDetailsBottom(); break;
            case romm3ds::ui::BottomPage::Filters: renderFiltersBottom(); break;
            case romm3ds::ui::BottomPage::Sort: renderSortBottom(); break;
            case romm3ds::ui::BottomPage::Settings: renderSettingsBottom(); break;
        }
    }

    void renderTransferTop() {
        const std::string_view phase_title = romm3ds::ui::transferPhaseTitle(transfer_phase_);
        const std::string_view phase_detail = romm3ds::ui::transferPhaseDetail(transfer_phase_);
        drawText(16, 12, 0.40f, kMuted, phase_title.data());
        drawText(16, 40, 0.38f, kMuted, phase_detail.data());
        const float fraction = progress_total_ == 0 ? 0.0f :
            std::min(1.0f, static_cast<float>(progress_current_) / static_cast<float>(progress_total_));
        C2D_DrawRectSolid(16, 104, 0.1f, 368, 8, kPanel);
        C2D_DrawRectSolid(16, 104, 0.2f, 368.0f * fraction, 8, kAccent);
        char progress[96];
        std::snprintf(progress, sizeof(progress), "%llu / %lu bytes   B cancel",
                      static_cast<unsigned long long>(progress_current_),
                      static_cast<unsigned long>(progress_total_));
        drawText(16, 126, 0.38f, kMuted, progress);
    }

    void render() {
        C2D_TextBufClear(text_buffer_);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top_, installing_ ? kBlack : kBackground);
        C2D_SceneBegin(top_);
        if (installing_) renderTransferTop();
        else renderLibraryTop();
        if (installing_) {
            C2D_TargetClear(bottom_, kBlack);
            C2D_SceneBegin(bottom_);
        } else {
            renderBottom();
        }
        C3D_FrameEnd(0);
    }

    bool beginHttp(HttpRequest& request, const char* url, const char* purpose,
                   bool authenticated = true) {
        u32 status_code = 0;
        const Result result = request.begin(url, authenticated ? settings_.token : std::string{},
                                            status_code);
        if (R_FAILED(result)) {
            setResultStatus(purpose, result);
            return false;
        }
        if (status_code != 200) {
            const char* category = "HTTP error";
            if (status_code == 401) category = "authentication failed";
            else if (status_code == 403) category = "access forbidden";
            else if (status_code == 404) category = "not found";
            else if (status_code >= 500) category = "RomM server error";
            else if (status_code >= 300 && status_code < 400) category = "redirect rejected";
            std::snprintf(status_, sizeof(status_), "%s: %s (HTTP %lu)", purpose, category,
                          static_cast<unsigned long>(status_code));
            has_result_ = true;
            return false;
        }
        return true;
    }

    bool loadImageCache(const std::string& path, std::vector<std::uint8_t>& encoded) {
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) return false;
        encoded.clear();
        std::unique_ptr<std::uint8_t[]> chunk(new (std::nothrow) std::uint8_t[4096]);
        if (!chunk) { std::fclose(file); return false; }
        while (true) {
            const std::size_t count = std::fread(chunk.get(), 1, 4096, file);
            if (encoded.size() + count > romm3ds::ui::kMaxCoverDownloadBytes) {
                std::fclose(file); encoded.clear(); std::remove(path.c_str()); return false;
            }
            encoded.insert(encoded.end(), chunk.get(), chunk.get() + count);
            if (count < 4096) {
                const bool ok = std::feof(file) != 0 && std::ferror(file) == 0;
                std::fclose(file);
                if (!ok) encoded.clear();
                return ok && !encoded.empty();
            }
        }
    }

    void pruneCoverCache() {
        struct Item { std::string path; std::uint64_t size; std::time_t modified; };
        std::vector<Item> items;
        DIR* directory = opendir(romm3ds::config::kCoverCacheDirectory);
        if (!directory) return;
        while (dirent* entry = readdir(directory)) {
            if (entry->d_name[0] == '.') continue;
            const std::string path = std::string(romm3ds::config::kCoverCacheDirectory) + "/" + entry->d_name;
            struct stat info {};
            if (::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
                items.push_back(Item{path, static_cast<std::uint64_t>(info.st_size), info.st_mtime});
            }
        }
        closedir(directory);
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.modified < b.modified; });
        std::uint64_t bytes = 0;
        for (const Item& item : items) bytes += item.size;
        std::size_t first = 0;
        while (items.size() - first > romm3ds::kMaxCoverCacheFiles ||
               bytes > romm3ds::kMaxCoverCacheBytes) {
            bytes -= items[first].size;
            std::remove(items[first].path.c_str());
            ++first;
        }
    }

    void saveImageCache(const std::string& path, const std::vector<std::uint8_t>& encoded) {
        mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/PocketShelf", 0777);
        mkdir("sdmc:/3ds/PocketShelf/cache", 0777);
        mkdir(romm3ds::config::kCoverCacheDirectory, 0777);
        FILE* file = std::fopen((path + ".tmp").c_str(), "wb");
        if (!file) return;
        const bool wrote = std::fwrite(encoded.data(), 1, encoded.size(), file) == encoded.size();
        const bool flushed = wrote && std::fflush(file) == 0;
        const bool closed = std::fclose(file) == 0;
        const bool ok = wrote && flushed && closed;
        if (ok) std::rename((path + ".tmp").c_str(), path.c_str());
        else std::remove((path + ".tmp").c_str());
        pruneCoverCache();
    }

    void fetchCover(const romm3ds::CatalogEntry& entry) {
        const bool screenshot = bottom_navigation_.page() == romm3ds::ui::BottomPage::Details &&
            bottom_navigation_.detailsView() == romm3ds::ui::DetailsView::Screenshots;
        cover_status_ = screenshot ? "Screenshot: none available" :
            romm3ds::ui::coverStatusText(romm3ds::ui::CoverStatus::NoPath);
        std::string resource_url;
        if (screenshot) {
            if (screenshot_selection_.empty() ||
                screenshot_selection_.index() >= entry.screenshots.size()) return;
            resource_url = entry.screenshots[screenshot_selection_.index()];
        } else {
            resource_url = entry.cover_url;
        }
        if (resource_url.empty()) return;

        const std::string cache_path = screenshot ? romm3ds::screenshotCachePath(
            romm3ds::config::kCoverCacheDirectory, entry.rom_id,
            screenshot_selection_.index(), resource_url) :
            romm3ds::coverCachePath(romm3ds::config::kCoverCacheDirectory, entry.rom_id);
        const std::string resource_key = (screenshot ? "s:" : "c:") + resource_url;
        if (cover_resource_key_ == resource_key) return;
        cover_.clear();
        cover_resource_key_.clear();

        std::vector<std::uint8_t> encoded;
        if (!cache_path.empty() && loadImageCache(cache_path, encoded)) {
            if (cover_.load(encoded)) {
                cover_resource_key_ = resource_key;
                cover_status_ = screenshot ? "Screenshot ready (cache)" : "Cover ready (cache)";
                return;
            }
            std::remove(cache_path.c_str());
            encoded.clear();
        }

        cover_status_ = screenshot ? "Screenshot: requesting" : "Cover: requesting";
        render();
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) { cover_status_ = "Cover: out of memory"; return; }
        HttpRequest request;
        u32 status_code = 0;
        const Result begin_result = request.begin(resource_url.c_str(), settings_.token, status_code);
        if (R_FAILED(begin_result)) {
            char failure[64];
            std::snprintf(failure, sizeof(failure), "Cover: request failed 0x%08lX",
                          static_cast<unsigned long>(static_cast<u32>(begin_result)));
            cover_status_ = failure;
            return;
        }
        if (status_code != 200) {
            cover_status_ = romm3ds::ui::coverStatusText(
                romm3ds::ui::CoverStatus::HttpError, status_code);
            return;
        }
        char content_type[48]{};
        const bool has_content_type = R_SUCCEEDED(
            request.responseHeader("Content-Type", content_type, sizeof(content_type)));
        u32 already = 0;
        u32 content_size = 0;
        const Result state_result = request.downloadState(already, content_size);
        if (R_FAILED(state_result)) {
            cover_status_ = "Cover: size state failed";
            return;
        }
        if (content_size > romm3ds::ui::kMaxCoverDownloadBytes) {
            cover_status_ = "Cover: response too large";
            return;
        }
        if (content_size) encoded.reserve(content_size);
        Result result = 0;
        do {
            if (!aptMainLoop()) { cover_status_ = "Cover: interrupted"; return; }
            u32 downloaded = 0;
            result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (downloaded != 0) {
                if (encoded.size() + downloaded > romm3ds::ui::kMaxCoverDownloadBytes) {
                    cover_status_ = "Cover: response too large";
                    return;
                }
                encoded.insert(encoded.end(), buffer.get(), buffer.get() + downloaded);
            }
            if (R_FAILED(result) && !isPending(result)) {
                char failure[64];
                std::snprintf(failure, sizeof(failure), "Cover: download failed 0x%08lX",
                              static_cast<unsigned long>(static_cast<u32>(result)));
                cover_status_ = failure;
                return;
            }
        } while (isPending(result));
        request.markComplete();
        if (content_size && encoded.size() != content_size) {
            cover_status_ = "Cover: incomplete response";
            return;
        }
        const auto body_status = romm3ds::ui::classifyCoverBody(std::string_view(
            reinterpret_cast<const char*>(encoded.data()), encoded.size()));
        if (body_status != romm3ds::ui::CoverStatus::ReadyToDecode) {
            cover_status_ = romm3ds::ui::coverStatusText(body_status, 0, encoded.size());
            return;
        }
        if (!cover_.load(encoded)) {
            cover_status_ = romm3ds::ui::coverStatusText(
                romm3ds::ui::CoverStatus::DecodeFailed, 0, encoded.size());
            return;
        }
        if (!cache_path.empty()) saveImageCache(cache_path, encoded);
        cover_resource_key_ = resource_key;
        const char* label = screenshot ? "Screenshot ready" : "Cover ready";
        cover_status_ = has_content_type && content_type[0] != '\0' ?
            std::string(label) + ": " + content_type : label;
    }

    void fetchLibrary() {
        if (settings_.token.empty()) { setStatus("Enter a RomM token in Settings"); return; }
        setStatus("Refreshing RomM catalogue pages...");
        render();
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) { setStatus("Not enough memory for network buffer"); return; }

        std::vector<romm3ds::CatalogEntry> refreshed;
        std::size_t offset = 0;
        std::uint64_t total = 0;
        bool truncated = false;
        while (true) {
            const std::string url = romm3ds::rommCatalogueUrl(
                settings_.base_url, romm3ds::kRommPageSize, offset, search_, true,
                romm_filters_);
            HttpRequest request;
            if (!beginHttp(request, url.c_str(), "RomM catalogue request failed")) return;
            u32 already = 0, content_size = 0;
            if (R_FAILED(request.downloadState(already, content_size))) {
                setStatus("RomM catalogue size state failed"); return;
            }
            if (content_size > romm3ds::kMaxRommPageBytes) {
                setStatus("RomM catalogue page exceeds 768 KiB"); return;
            }
            std::string json;
            if (content_size) json.reserve(content_size);
            Result result = 0;
            do {
                if (!aptMainLoop()) { setStatus("Catalogue refresh interrupted"); return; }
                u32 downloaded = 0;
                result = request.download(buffer.get(), kTransferBufferSize, downloaded);
                if (json.size() + downloaded > romm3ds::kMaxRommPageBytes) {
                    setStatus("RomM catalogue page exceeds 768 KiB"); return;
                }
                json.append(reinterpret_cast<const char*>(buffer.get()), downloaded);
                if (R_FAILED(result) && !isPending(result)) {
                    setResultStatus("RomM catalogue download failed", result); return;
                }
                if (isPending(result)) render();
            } while (isPending(result));
            request.markComplete();
            if (content_size && json.size() != content_size) {
                setStatus("RomM catalogue page was incomplete"); return;
            }

            romm3ds::RommPage page;
            romm3ds::RommParseError parse_error = romm3ds::RommParseError::None;
            if (!romm3ds::parseRommPage(json, page, parse_error)) {
                std::snprintf(status_, sizeof(status_), "RomM catalogue: %s",
                              romm3ds::rommParseErrorMessage(parse_error));
                has_result_ = true;
                return;
            }
            if (page.offset != offset || page.limit == 0) {
                setStatus("RomM returned inconsistent pagination"); return;
            }
            total = page.total;
            for (auto& entry : page.entries) {
                if (!type_filter_.empty() && entry.type != type_filter_) continue;
                entry.download_url = romm3ds::rommFileContentUrl(
                    settings_.base_url, entry.file_id, entry.file_name);
                entry.cover_url = romm3ds::rommCoverUrl(settings_.base_url, entry.cover_url);
                entry.screenshots = romm3ds::rommScreenshotUrls(
                    settings_.base_url, entry.rom_id, entry.screenshots);
                if (refreshed.size() >= romm3ds::kMaxCatalogEntries) { truncated = true; break; }
                refreshed.push_back(std::move(entry));
            }
            if (truncated || page.item_count == 0) break;
            offset += page.item_count;
            if (offset >= total) break;
        }

        entries_ = std::move(refreshed);
        selected_ = 0;
        bottom_navigation_.openDetailsView(romm3ds::ui::DetailsView::Info);
        screenshot_selection_.setCount(entries_.empty() ? 0 : entries_[0].screenshots.size());
        details_scroll_.reset();
        sort_applied_ = false;
        cover_.clear();
        cover_resource_key_.clear();
        const bool cached = romm3ds::saveCatalogCache(romm3ds::config::kCatalogCachePath, entries_);
        if (entries_.empty()) setStatus("RomM catalogue contains no matching supported files");
        else if (truncated) setStatus("Catalogue capped at 512 files and cached");
        else setStatus(cached ? "RomM catalogue refreshed and cached" :
                                "RomM catalogue refreshed; cache write failed");
        if (!entries_.empty()) fetchCover(entries_[0]);
    }

    void transfer(const romm3ds::CatalogEntry& entry) {
        if (entry.action == romm3ds::CatalogAction::InstallCia) install(entry);
        else downloadFile(entry);
    }

    void downloadFile(const romm3ds::CatalogEntry& entry) {
        if (!romm3ds::isSafeDownloadFilename(entry.file_name)) { setStatus("Unsafe download filename rejected"); return; }
        FS_Archive archive{};
        if (entry.size != 0 && R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
            u64 free_bytes = 0; const Result free_result = FSUSER_GetFreeBytes(&free_bytes, archive); FSUSER_CloseArchive(archive);
            if (R_SUCCEEDED(free_result) && free_bytes < entry.size) { setStatus("Not enough free SD space"); return; }
        }
        mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/PocketShelf", 0777);
        mkdir("sdmc:/3ds/PocketShelf/downloads", 0777); mkdir("sdmc:/roms", 0777);
        const std::string directory = romm3ds::downloadDirectoryForType(entry.type);
        mkdir(directory.c_str(), 0777);
        const std::string path = directory + "/" + entry.file_name;
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) { setStatus("Could not create SD download file"); return; }
        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) { std::fclose(file); std::remove(path.c_str()); setStatus("Not enough memory for download buffer"); return; }
        HttpRequest request;
        if (!beginHttp(request, entry.download_url.c_str(), "ROM request failed")) { std::fclose(file); std::remove(path.c_str()); return; }
        u32 already = 0; if (R_FAILED(request.downloadState(already, progress_total_))) { std::fclose(file); std::remove(path.c_str()); setStatus("ROM Content-Length state failed"); return; }
        transfer_phase_ = romm3ds::ui::TransferPhase::Generic;
        installing_ = true; progress_current_ = 0; bool complete = false, cancelled = false; Result result = 0;
        while (true) {
            if (!aptMainLoop()) break;
            hidScanInput(); if (hidKeysDown() & KEY_B) { cancelled = true; break; }
            u32 downloaded = 0; result = request.download(buffer.get(), kTransferBufferSize, downloaded);
            if (downloaded && std::fwrite(buffer.get(), 1, downloaded, file) != downloaded) break;
            progress_current_ += downloaded; render();
            if (R_FAILED(result) && !isPending(result)) break;
            if (!isPending(result)) { request.markComplete(); complete = true; break; }
        }
        const bool closed = std::fclose(file) == 0; installing_ = false;
        if (progress_total_ && progress_current_ != progress_total_) complete = false;
        if (!complete || !closed || cancelled) {
            std::remove(path.c_str());
            setStatus(cancelled ? "Download cancelled; partial file removed" : "Download failed; partial file removed");
            return;
        }
        setStatus("ROM downloaded to SD card");
    }

    void install(const romm3ds::CatalogEntry& entry) {
        setStatus("Downloading CIA to safe staging...");
        render();

        std::unique_ptr<u8[]> buffer(new (std::nothrow) u8[kTransferBufferSize]);
        if (!buffer) {
            setStatus("Not enough memory for install buffer");
            return;
        }

        mkdir("sdmc:/3ds", 0777);
        mkdir("sdmc:/3ds/PocketShelf", 0777);
        const std::string staging_path = "sdmc:/3ds/PocketShelf/.cia-download.tmp";
        constexpr char kStagingArchivePath[] = "/3ds/PocketShelf/.cia-download.tmp";
        std::remove(staging_path.c_str());
        FILE* staging = std::fopen(staging_path.c_str(), "wb");
        if (!staging) {
            setStatus("Could not create CIA staging file");
            return;
        }

        HttpRequest request;
        if (!beginHttp(request, entry.download_url.c_str(), "CIA request failed")) {
            std::fclose(staging);
            std::remove(staging_path.c_str());
            return;
        }

        Result result = 0;
        bool cancelled = false;
        bool interrupted = false;
        bool download_complete = false;
        bool failure_reported = false;
        std::array<u8, 0x20> cia_prefix{};
        std::size_t cia_prefix_size = 0;
        u64 actual_downloaded = 0;
        u64 cia_expected_size = 0;
        std::optional<romm3ds::CiaStreamAccounting> accounting;
        transfer_phase_ = romm3ds::ui::TransferPhase::CiaNetworkStaging;
        installing_ = true;
        progress_current_ = 0;
        progress_total_ = entry.size > UINT32_MAX ? 0 : static_cast<u32>(entry.size);

        while (true) {
            if (!aptMainLoop()) {
                interrupted = true;
                break;
            }
            hidScanInput();
            if (hidKeysDown() & KEY_B) {
                cancelled = true;
                break;
            }

            u32 downloaded_this_call = 0;
            result = request.download(buffer.get(), kTransferBufferSize, downloaded_this_call);
            const bool terminal_call = !isPending(result);
            const bool had_accounting = accounting.has_value();

            if (downloaded_this_call > UINT64_MAX - actual_downloaded) {
                setStatus("CIA download byte count overflowed");
                failure_reported = true;
                break;
            }
            actual_downloaded += downloaded_this_call;
            progress_current_ = actual_downloaded;

            if (downloaded_this_call != 0 && cia_prefix_size < cia_prefix.size()) {
                const std::size_t copied = std::min<std::size_t>(
                    downloaded_this_call, cia_prefix.size() - cia_prefix_size);
                std::memcpy(cia_prefix.data() + cia_prefix_size, buffer.get(), copied);
                cia_prefix_size += copied;
                if (cia_prefix_size == cia_prefix.size()) {
                    romm3ds::CiaHeaderInfo cia_info{};
                    if (romm3ds::inspectCiaHeader(cia_prefix.data(), cia_prefix.size(), cia_info) !=
                        romm3ds::CiaHeaderStatus::Valid) {
                        setStatus("CIA download is not a valid CIA file");
                        failure_reported = true;
                        break;
                    }
                    if (cia_info.total_size > UINT32_MAX) {
                        setStatus("CIA is too large for the 3DS HTTP service");
                        failure_reported = true;
                        break;
                    }
                    cia_expected_size = cia_info.total_size;
                    progress_total_ = static_cast<u32>(cia_expected_size);
                    accounting.emplace(cia_expected_size);
                }
            }

            if (downloaded_this_call != 0 &&
                std::fwrite(buffer.get(), 1, downloaded_this_call, staging) != downloaded_this_call) {
                setStatus("Could not write the CIA staging file");
                failure_reported = true;
                break;
            }

            romm3ds::CiaStreamSizeStatus size_status =
                romm3ds::CiaStreamSizeStatus::InProgress;
            if (accounting) {
                size_status = accounting->accept(had_accounting ? downloaded_this_call :
                                                                 actual_downloaded,
                                                 terminal_call);
                if (size_status == romm3ds::CiaStreamSizeStatus::Oversized) {
                    setStatus("CIA download exceeds the size declared by its header");
                    failure_reported = true;
                    break;
                }
            }

            render();
            if (R_FAILED(result) && !isPending(result)) {
                setResultStatus("CIA download failed", result);
                failure_reported = true;
                break;
            }
            if (terminal_call) {
                if (!accounting) {
                    setStatus("CIA download ended before a complete header was received");
                    failure_reported = true;
                    break;
                }
                if (size_status != romm3ds::CiaStreamSizeStatus::Complete) {
                    setStatus("CIA download ended before the size declared by its header");
                    failure_reported = true;
                    break;
                }
                request.markComplete();
                download_complete = true;
                break;
            }
        }

        const bool staged_flushed = std::fflush(staging) == 0;
        const bool staged_closed = std::fclose(staging) == 0;
        installing_ = false;
        if (cancelled || interrupted || !download_complete || !staged_flushed || !staged_closed) {
            std::remove(staging_path.c_str());
            if (cancelled) setStatus("Install cancelled; staged CIA removed");
            else if (interrupted) setStatus("Install interrupted; staged CIA removed");
            else if (!failure_reported) setStatus("CIA staging failed; partial file removed");
            return;
        }

        Handle staged_handle = 0;
        result = FSUSER_OpenFileDirectly(&staged_handle, ARCHIVE_SDMC,
                                         fsMakePath(PATH_EMPTY, ""),
                                         fsMakePath(PATH_ASCII, kStagingArchivePath),
                                         FS_OPEN_READ, 0);
        if (R_FAILED(result)) {
            std::remove(staging_path.c_str());
            setResultStatus("Could not inspect staged CIA", result);
            return;
        }
        AM_TitleInfo installed_info{};
        const Result info_result = AM_GetCiaFileInfo(MEDIATYPE_SD, &installed_info, staged_handle);
        const Result close_result = FSFILE_Close(staged_handle);
        if (R_FAILED(info_result) || R_FAILED(close_result)) {
            std::remove(staging_path.c_str());
            setResultStatus("Could not read staged CIA title information",
                            R_FAILED(info_result) ? info_result : close_result);
            return;
        }

        const bool inventory_verified = refreshInstalledTitles();
        const bool title_installed = inventory_verified && installedTitle(installed_info.titleID);
        auto install_decision = romm3ds::ciaInstallDecision(
            inventory_verified, title_installed, false, true, true);
        if (install_decision == romm3ds::CiaInstallDecision::RefuseUnverified) {
            std::remove(staging_path.c_str());
            setStatus("Could not verify installed titles; install refused for safety");
            return;
        }

        bool overwrite = false;
        if (install_decision == romm3ds::CiaInstallDecision::RequireOverwriteConfirmation) {
            if (!confirmCiaOverwrite(installed_info.titleID)) {
                std::remove(staging_path.c_str());
                setStatus("Overwrite cancelled before the installed title was touched");
                return;
            }

            // Re-open the fully staged file after confirmation. The overwrite target
            // must still be the exact title ID shown to the user before AM is opened.
            Handle verified_handle = 0;
            AM_TitleInfo verified_info{};
            const Result open_verified = FSUSER_OpenFileDirectly(
                &verified_handle, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                fsMakePath(PATH_ASCII, kStagingArchivePath), FS_OPEN_READ, 0);
            Result verify_info = open_verified;
            Result verify_close = 0;
            if (R_SUCCEEDED(open_verified)) {
                verify_info = AM_GetCiaFileInfo(MEDIATYPE_SD, &verified_info, verified_handle);
                verify_close = FSFILE_Close(verified_handle);
            }
            const bool title_matches = R_SUCCEEDED(verify_info) && R_SUCCEEDED(verify_close) &&
                                       verified_info.titleID == installed_info.titleID;
            install_decision = romm3ds::ciaInstallDecision(
                inventory_verified, true, true, true, title_matches);
            if (install_decision != romm3ds::CiaInstallDecision::InstallOverwrite) {
                std::remove(staging_path.c_str());
                if (install_decision == romm3ds::CiaInstallDecision::RefuseTitleMismatch)
                    setStatus("Staged CIA title ID changed; overwrite refused");
                else
                    setResultStatus("Could not re-verify staged CIA before overwrite",
                                    R_FAILED(verify_info) ? verify_info : verify_close);
                return;
            }
            overwrite = true;
        }

        FILE* input = std::fopen(staging_path.c_str(), "rb");
        if (!input) {
            std::remove(staging_path.c_str());
            setStatus("Could not reopen staged CIA for installation");
            return;
        }

        Handle cia_handle = 0;
        result = overwrite ? AM_StartCiaInstallOverwrite(&cia_handle, MEDIATYPE_SD) :
                             AM_StartCiaInstall(MEDIATYPE_SD, &cia_handle);
        if (R_FAILED(result)) {
            std::fclose(input);
            std::remove(staging_path.c_str());
            setResultStatus(overwrite ? "Could not start explicit CIA overwrite" :
                                        "Could not start CIA install", result);
            return;
        }

        bool install_complete = true;
        cancelled = false;
        interrupted = false;
        u64 install_offset = 0;
        transfer_phase_ = romm3ds::ui::TransferPhase::CiaLocalInstall;
        installing_ = true;
        progress_current_ = 0;
        progress_total_ = static_cast<u32>(cia_expected_size);
        render();

        while (install_offset < cia_expected_size) {
            if (!aptMainLoop()) {
                interrupted = true;
                install_complete = false;
                break;
            }
            hidScanInput();
            if (hidKeysDown() & KEY_B) {
                cancelled = true;
                install_complete = false;
                break;
            }
            const u32 requested = static_cast<u32>(std::min<u64>(
                kTransferBufferSize, cia_expected_size - install_offset));
            const std::size_t read = std::fread(buffer.get(), 1, requested, input);
            if (read != requested) {
                setStatus("Staged CIA became incomplete before installation");
                install_complete = false;
                break;
            }
            const romm3ds::WriteAllOutcome write_outcome = romm3ds::writeAll(
                buffer.get(), requested, install_offset,
                [&cia_handle](u32* written, u64 offset, const void* source, u32 source_size) {
                    return static_cast<std::int32_t>(
                        FSFILE_Write(cia_handle, written, offset, source, source_size, 0));
                });
            if (write_outcome.status == romm3ds::WriteAllStatus::WriteError) {
                setResultStatus("CIA install write failed",
                                static_cast<Result>(write_outcome.result));
                install_complete = false;
                failure_reported = true;
                break;
            }
            if (write_outcome.status == romm3ds::WriteAllStatus::InvalidProgress) {
                setResultStatus("CIA install failed: short write", kInvalidWriteProgressResult);
                install_complete = false;
                failure_reported = true;
                break;
            }
            install_offset += requested;
            progress_current_ = install_offset;
            render();
        }

        const bool staged_unchanged = install_complete && std::fgetc(input) == EOF &&
                                      std::feof(input) != 0;
        const bool input_closed = std::fclose(input) == 0;
        installing_ = false;
        std::remove(staging_path.c_str());
        if (!staged_unchanged || !input_closed) install_complete = false;

        if (!install_complete) {
            const Result cancel_result = AM_CancelCIAInstall(cia_handle);
            if (R_FAILED(cancel_result)) {
                setResultStatus("Install failed; cleanup failed", cancel_result);
                return;
            }
            if (cancelled) setStatus("Install cancelled; existing titles were not touched");
            else if (interrupted) setStatus("Install interrupted; existing titles were not touched");
            else if (!failure_reported) setStatus("CIA install failed before finalization");
            return;
        }

        result = AM_FinishCiaInstall(cia_handle);
        if (R_FAILED(result)) {
            setResultStatus("Finalizing CIA failed", result);
            return;
        }
        mkdir(romm3ds::config::kInstallReceiptDirectory, 0777);
        const bool title_tracked = romm3ds::saveInstallReceiptAtomic(
            romm3ds::installReceiptPath(romm3ds::config::kInstallReceiptDirectory,
                                        entry.file_id),
            installed_info.titleID);
        refreshInstalledTitles();
        if (overwrite) {
            setStatus(title_tracked ? "Overwrite finalized; title presence is tracked" :
                                      "Overwrite finalized; title ID receipt unavailable");
        } else {
            setStatus(title_tracked ? "Installed safely; title presence is now tracked" :
                                      "Installed safely; title ID receipt unavailable");
        }
    }

    C3D_RenderTarget* top_ = nullptr;
    C3D_RenderTarget* bottom_ = nullptr;
    C2D_TextBuf text_buffer_ = nullptr;
    bool http_ready_ = false;
    bool am_ready_ = false;
    Screen screen_ = Screen::Library;
    romm3ds::Settings settings_;
    std::vector<romm3ds::CatalogEntry> entries_;
    std::size_t selected_ = 0;
    char status_[romm3ds::ui::kStatusBufferSize]{};
    bool has_result_ = false;
    bool installing_ = false;
    romm3ds::ui::TransferPhase transfer_phase_ = romm3ds::ui::TransferPhase::Generic;
    bool overwrite_prompt_ = false;
    std::uint64_t overwrite_title_id_ = 0;
    u64 progress_current_ = 0;
    u32 progress_total_ = 0;
    std::string search_;
    std::string type_filter_;
    std::string metadata_filter_text_;
    romm3ds::RommFilters romm_filters_;
    romm3ds::ui::BottomNavigationState bottom_navigation_;
    romm3ds::ui::DetailsScrollState details_scroll_;
    romm3ds::ui::ScreenshotSelectionState screenshot_selection_;
    int circle_scroll_cooldown_ = 0;
    int type_touch_x_ = 0;
    int type_touch_y_ = 0;
    bool type_touch_active_ = false;
    bool sort_ascending_ = true;
    bool sort_applied_ = false;
    std::optional<romm3ds::ui::TypeChooserState> type_chooser_;
    std::optional<romm3ds::ui::TitlePickerState> title_picker_;
    romm3ds::ui::CoverTexture cover_;
    std::string cover_resource_key_;
    std::string cover_status_ = "Cover: no RomM path";
    std::vector<romm3ds::LocalSaveFile> local_saves_;
    std::vector<romm3ds::CheckpointBackup> checkpoint_backups_;
    std::uint64_t checkpoint_title_id_ = 0;
    std::string checkpoint_title_directory_;
    SaveProvider save_provider_ = SaveProvider::Unsupported;
    std::string rom_adjacent_save_path_;
    std::vector<romm3ds::RemoteSaveFile> remote_saves_;
    romm3ds::ui::SaveSelectionState save_selection_;
    SavePageControl save_control_ = SavePageControl::Download;
    SaveConfirmation save_confirmation_ = SaveConfirmation::None;
    std::vector<u64> installed_title_ids_;
    std::vector<romm3ds::ui::InstalledTitleChoice> installed_title_choices_;
};

} // namespace

int main() {
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        gfxExit();
        return 1;
    }
    if (!C2D_Init(kMaxRenderObjects)) {
        C3D_Fini();
        gfxExit();
        return 1;
    }
    C2D_Prepare();

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    C2D_TextBuf text_buffer = C2D_TextBufNew(4096);
    if (top == nullptr || bottom == nullptr || text_buffer == nullptr) {
        if (text_buffer != nullptr) C2D_TextBufDelete(text_buffer);
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }

    const Result http_result = httpcInit(1024 * 1024);
    const bool http_ready = R_SUCCEEDED(http_result);
    const Result am_result = amInit();
    const bool am_ready = R_SUCCEEDED(am_result);

    {
        ClientApp app(top, bottom, text_buffer, http_ready, am_ready, http_result, am_result);
        app.run();
    }

    if (am_ready) amExit();
    if (http_ready) httpcExit();
    C2D_TextBufDelete(text_buffer);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
