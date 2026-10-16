#include "manifest_parser.hpp"

namespace romm3ds {

bool ManifestParser::fail(ParseError error) {
    error_ = error;
    error_line_ = line_number_;
    return false;
}

bool ManifestParser::parseLine() {
    if (!line_.empty() && line_.back() == '\r') {
        line_.pop_back();
    }
    const auto first = line_.find_first_not_of(" \t");
    if (first == std::string::npos || line_[first] == '#') {
        line_.clear();
        return true;
    }
    const auto separator = line_.find('|');
    if (separator == std::string::npos) return fail(ParseError::MalformedLine);
    if (separator == 0) return fail(ParseError::EmptyTitle);
    if (separator > kMaxTitleBytes) return fail(ParseError::TitleTooLong);

    const std::size_t url_size = line_.size() - separator - 1;
    if (url_size == 0) return fail(ParseError::EmptyUrl);
    if (url_size > kMaxUrlBytes) return fail(ParseError::UrlTooLong);
    if (line_.compare(separator + 1, 7, "http://") != 0) {
        return fail(ParseError::UnsupportedScheme);
    }
    if (entries_.size() >= kMaxManifestEntries) return fail(ParseError::TooManyEntries);

    entries_.push_back({line_.substr(0, separator), line_.substr(separator + 1)});
    line_.clear();
    return true;
}

bool ManifestParser::feed(std::string_view chunk) {
    if (error_ != ParseError::None || finished_) {
        return false;
    }
    for (const char ch : chunk) {
        if (ch == '\0') return fail(ParseError::EmbeddedNul);
        if (ch == '\n') {
            if (!parseLine()) return false;
            ++line_number_;
        } else {
            if (line_.size() >= kMaxLineBytes) return fail(ParseError::LineTooLong);
            line_.push_back(ch);
        }
    }
    return true;
}

bool ManifestParser::finish() {
    if (error_ != ParseError::None || finished_) return false;
    finished_ = true;
    if (!line_.empty() && !parseLine()) return false;
    return true;
}

const char* ManifestParser::errorMessage() const noexcept {
    switch (error_) {
        case ParseError::None: return "no error";
        case ParseError::EmbeddedNul: return "embedded NUL byte";
        case ParseError::LineTooLong: return "line exceeds 640 bytes";
        case ParseError::MalformedLine: return "expected Title|http://URL";
        case ParseError::EmptyTitle: return "title is empty";
        case ParseError::TitleTooLong: return "title exceeds 96 bytes";
        case ParseError::EmptyUrl: return "URL is empty";
        case ParseError::UrlTooLong: return "URL exceeds 512 bytes";
        case ParseError::UnsupportedScheme: return "URL must begin with http://";
        case ParseError::TooManyEntries: return "manifest exceeds 512 entries";
    }
    return "unknown manifest error";
}

} // namespace romm3ds
