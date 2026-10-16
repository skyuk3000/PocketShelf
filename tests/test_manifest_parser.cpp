#include "manifest_parser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using romm3ds::ManifestEntry;
using romm3ds::ManifestParser;
using romm3ds::ParseError;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_parses_one_entry() {
    ManifestParser parser;
    require(parser.feed("A Game|http://192.168.1.2/game.cia\n"), "feed should succeed");
    require(parser.finish(), "finish should succeed");
    require(parser.entries().size() == 1, "one entry expected");
    require(parser.entries()[0].title == "A Game", "title should match");
    require(parser.entries()[0].url == "http://192.168.1.2/game.cia", "URL should match");
}

void test_tolerates_chunks_crlf_blanks_and_comments() {
    ManifestParser parser;
    require(parser.feed("# generated manifest\r\n\r\nFirst|http://host/f"), "first chunk");
    require(parser.feed("irst.cia\r\n  # another comment\nSecond|http://host/second.cia"), "second chunk");
    require(parser.finish(), "unterminated final line should parse");
    require(parser.entries().size() == 2, "comments and blanks should be skipped");
    require(parser.entries()[0].url == "http://host/first.cia", "split URL should join");
    require(parser.entries()[1].title == "Second", "final title should match");
}
void expectError(std::string_view input, ParseError expected, const char* message) {
    ManifestParser parser;
    const bool fed = parser.feed(input);
    const bool finished = fed && parser.finish();
    require(!finished, message);
    require(parser.error() == expected, message);
    require(parser.errorLine() == 1, "first-line error should report line 1");
    require(std::string(parser.errorMessage()) != "manifest parse error", "error should be useful");
}

void test_rejects_invalid_records_and_bounds() {
    expectError("missing separator\n", ParseError::MalformedLine, "separator is required");
    expectError("|http://host/a.cia\n", ParseError::EmptyTitle, "title is required");
    expectError(std::string(97, 'T') + "|http://host/a.cia\n", ParseError::TitleTooLong, "title bound");
    expectError("Title|\n", ParseError::EmptyUrl, "URL is required");
    expectError("Title|http://" + std::string(506, 'u') + "\n", ParseError::UrlTooLong, "URL bound");
    expectError("Title|https://host/a.cia\n", ParseError::UnsupportedScheme, "HTTPS is outside contract");
    expectError(std::string("Title|http://ok\0evil\n", 21), ParseError::EmbeddedNul, "NUL must be rejected");
    expectError(std::string(641, 'x'), ParseError::LineTooLong, "line bound");
}

void test_rejects_more_than_entry_cap() {
    ManifestParser parser;
    for (std::size_t i = 0; i < romm3ds::kMaxManifestEntries; ++i) {
        require(parser.feed("T|http://host/a.cia\n"), "entries through cap should parse");
    }
    require(!parser.feed("Overflow|http://host/b.cia\n"), "entry after cap should fail");
    require(parser.error() == ParseError::TooManyEntries, "cap error expected");
    require(parser.entries().size() == romm3ds::kMaxManifestEntries, "stored entries stay bounded");
}
} // namespace

int main() {
    test_parses_one_entry();
    test_tolerates_chunks_crlf_blanks_and_comments();
    test_rejects_invalid_records_and_bounds();
    test_rejects_more_than_entry_cap();
    std::cout << "All manifest parser tests passed\n";
    return 0;
}
