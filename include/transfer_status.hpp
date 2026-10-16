#pragma once

#include <string_view>

namespace romm3ds::ui {

enum class TransferPhase {
    Generic,
    CiaNetworkStaging,
    CiaLocalInstall,
};

inline constexpr bool transferPhaseUsesNetwork(TransferPhase phase) noexcept {
    return phase != TransferPhase::CiaLocalInstall;
}

inline constexpr std::string_view transferPhaseTitle(TransferPhase phase) noexcept {
    switch (phase) {
        case TransferPhase::CiaNetworkStaging:
            return "CIA 1/2: DOWNLOAD TO SD STAGING";
        case TransferPhase::CiaLocalInstall:
            return "CIA 2/2: INSTALL FROM STAGED SD FILE";
        case TransferPhase::Generic:
            return "TRANSFER IN PROGRESS - DISPLAYS DIMMED";
    }
    return "TRANSFER IN PROGRESS";
}

inline constexpr std::string_view transferPhaseDetail(TransferPhase phase) noexcept {
    switch (phase) {
        case TransferPhase::CiaNetworkStaging:
            return "RomM -> SD staging (single HTTP download)";
        case TransferPhase::CiaLocalInstall:
            return "Installing local staged SD file (no network)";
        case TransferPhase::Generic:
            return "Streaming bytes to the destination";
    }
    return {};
}

} // namespace romm3ds::ui
