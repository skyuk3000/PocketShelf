#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "catalog_parser.hpp"

namespace romm3ds::ui {

std::vector<std::string> wrapText(std::string_view text, std::size_t max_columns,
                                  std::size_t max_lines);

inline constexpr std::size_t kInfoSummaryLines = 6;

enum class InfoLineKind { Section, Field, Continuation, Body };

struct InfoLine {
    InfoLineKind kind = InfoLineKind::Body;
    std::string label;
    std::string value;
};

std::vector<InfoLine> buildInfoLines(const CatalogEntry& entry,
                                     std::string_view type,
                                     std::string_view download_state);

} // namespace romm3ds::ui
