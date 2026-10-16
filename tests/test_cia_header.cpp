#include "cia_header.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void put16(std::array<std::uint8_t, 0x20>& header, std::size_t offset, std::uint16_t value) {
    header[offset] = static_cast<std::uint8_t>(value);
    header[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::array<std::uint8_t, 0x20>& header, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) header[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void put64(std::array<std::uint8_t, 0x20>& header, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) header[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::array<std::uint8_t, 0x20> validHeader() {
    std::array<std::uint8_t, 0x20> header{};
    put32(header, 0x00, 0x2020);
    put16(header, 0x04, 0);
    put16(header, 0x06, 0);
    put32(header, 0x08, 0xA00);
    put32(header, 0x0C, 0x350);
    put32(header, 0x10, 0xB04);
    put32(header, 0x14, 0);
    put64(header, 0x18, 0x12340);
    return header;
}

void test_accepts_valid_header_and_computes_stream_size() {
    const auto header = validHeader();
    romm3ds::CiaHeaderInfo info{};
    require(romm3ds::inspectCiaHeader(header.data(), header.size(), info) ==
                romm3ds::CiaHeaderStatus::Valid,
            "valid CIA header should be accepted");
    require(info.total_size == 0x15C40, "CIA stream size should include aligned sections");
}

void test_requires_complete_fixed_header() {
    const auto header = validHeader();
    romm3ds::CiaHeaderInfo info{};
    require(romm3ds::inspectCiaHeader(header.data(), 0x1F, info) ==
                romm3ds::CiaHeaderStatus::NeedMore,
            "short prefix should request more bytes");
}

void test_rejects_non_cia_and_empty_content() {
    auto header = validHeader();
    romm3ds::CiaHeaderInfo info{};
    put32(header, 0x00, 0x7B226572);
    require(romm3ds::inspectCiaHeader(header.data(), header.size(), info) ==
                romm3ds::CiaHeaderStatus::Invalid,
            "JSON or HTML response must not reach AM");

    header = validHeader();
    put64(header, 0x18, 0);
    require(romm3ds::inspectCiaHeader(header.data(), header.size(), info) ==
                romm3ds::CiaHeaderStatus::Invalid,
            "CIA with no content must be rejected");
}

void test_rejects_overflowing_layout() {
    auto header = validHeader();
    put64(header, 0x18, UINT64_MAX);
    romm3ds::CiaHeaderInfo info{};
    require(romm3ds::inspectCiaHeader(header.data(), header.size(), info) ==
                romm3ds::CiaHeaderStatus::Invalid,
            "overflowing CIA layout must be rejected");
}

void test_validates_received_bytes_against_the_cia_header_not_http_size_state() {
    constexpr std::uint64_t expected = 0x15C40;
    require(romm3ds::classifyCiaStreamSize(expected, expected - 1, false) ==
                romm3ds::CiaStreamSizeStatus::InProgress,
            "partial CIA stream remains in progress");
    require(romm3ds::classifyCiaStreamSize(expected, expected, true) ==
                romm3ds::CiaStreamSizeStatus::Complete,
            "exact CIA header size completes without relying on Content-Length");
    require(romm3ds::classifyCiaStreamSize(expected, expected - 1, true) ==
                romm3ds::CiaStreamSizeStatus::Truncated,
            "completed short CIA stream is rejected");
    require(romm3ds::classifyCiaStreamSize(expected, expected + 1, false) ==
                romm3ds::CiaStreamSizeStatus::Oversized,
            "bytes beyond the CIA header size are rejected immediately");
}

void test_stream_accounting_adds_actual_bytes_from_pending_and_final_calls() {
    constexpr std::uint64_t expected = 0x10000;
    romm3ds::CiaStreamAccounting accounting(expected);
    require(accounting.accept(0x8000, false) == romm3ds::CiaStreamSizeStatus::InProgress,
            "pending call accounts for its actual bytes");
    require(accounting.accept(0x7000, false) == romm3ds::CiaStreamSizeStatus::InProgress,
            "multiple pending calls accumulate rather than replacing the byte count");
    require(accounting.accept(0x1000, true) == romm3ds::CiaStreamSizeStatus::Complete,
            "bytes returned by the final HTTP call are included before completion validation");
    require(accounting.received() == expected,
            "stream accounting exposes actual bytes received, not the CIA header total");
}

void test_stream_accounting_detects_zero_byte_final_truncation_and_overflow() {
    romm3ds::CiaStreamAccounting truncated(100);
    truncated.accept(99, false);
    require(truncated.accept(0, true) == romm3ds::CiaStreamSizeStatus::Truncated,
            "a successful zero-byte final call still validates the accumulated byte count");

    romm3ds::CiaStreamAccounting oversized(100);
    require(oversized.accept(101, false) == romm3ds::CiaStreamSizeStatus::Oversized,
            "a chunk beyond the CIA header total is rejected immediately");

    romm3ds::CiaStreamAccounting overflow(UINT64_MAX);
    overflow.accept(UINT64_MAX, false);
    require(overflow.accept(1, false) == romm3ds::CiaStreamSizeStatus::Oversized,
            "received-byte integer overflow is treated as oversized");
}

void test_install_safety_requires_explicit_verified_overwrite() {
    require(romm3ds::ciaInstallDecision(true, false, false, true, true) ==
                romm3ds::CiaInstallDecision::InstallNew,
            "a verified absent title may use the non-overwrite install path");
    require(romm3ds::ciaInstallDecision(true, true, false, true, true) ==
                romm3ds::CiaInstallDecision::RequireOverwriteConfirmation,
            "an installed title requires an explicit overwrite confirmation");
    require(romm3ds::ciaInstallDecision(true, true, true, true, true) ==
                romm3ds::CiaInstallDecision::InstallOverwrite,
            "a completely validated matching CIA may be explicitly overwritten");
    require(romm3ds::ciaInstallDecision(false, false, true, true, true) ==
                romm3ds::CiaInstallDecision::RefuseUnverified,
            "failure to inventory installed titles fails closed");
    require(romm3ds::ciaInstallDecision(true, true, true, false, true) ==
                romm3ds::CiaInstallDecision::RefuseUnverified,
            "overwrite is refused until the complete staged CIA is validated");
    require(romm3ds::ciaInstallDecision(true, true, true, true, false) ==
                romm3ds::CiaInstallDecision::RefuseTitleMismatch,
            "overwrite is refused when the re-inspected title ID changed");
}
} // namespace

int main() {
    test_accepts_valid_header_and_computes_stream_size();
    test_requires_complete_fixed_header();
    test_rejects_non_cia_and_empty_content();
    test_rejects_overflowing_layout();
    test_validates_received_bytes_against_the_cia_header_not_http_size_state();
    test_stream_accounting_adds_actual_bytes_from_pending_and_final_calls();
    test_stream_accounting_detects_zero_byte_final_truncation_and_overflow();
    test_install_safety_requires_explicit_verified_overwrite();
    std::cout << "All CIA header tests passed\n";
    return 0;
}
