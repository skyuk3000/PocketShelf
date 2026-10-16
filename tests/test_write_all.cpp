#include "write_all.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using romm3ds::WriteAllStatus;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void test_retries_partial_writes_with_advanced_offsets() {
    const std::uint8_t data[10]{};
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> sizes;
    unsigned call = 0;
    const auto outcome = romm3ds::writeAll(
        data, sizeof(data), 100,
        [&](std::uint32_t* written, std::uint64_t offset, const void*, std::uint32_t size) {
            offsets.push_back(offset);
            sizes.push_back(size);
            *written = call++ == 0 ? 4 : size;
            return std::int32_t{0};
        });

    require(outcome.status == WriteAllStatus::Complete, "partial writes should complete");
    require(outcome.result == 0, "completed write should preserve success result");
    require(offsets == std::vector<std::uint64_t>({100, 104}), "offset should advance");
    require(sizes == std::vector<std::uint32_t>({10, 6}), "remaining size should shrink");
}

void test_rejects_success_without_progress() {
    const std::uint8_t data[3]{};
    const auto outcome = romm3ds::writeAll(
        data, sizeof(data), 0,
        [](std::uint32_t* written, std::uint64_t, const void*, std::uint32_t) {
            *written = 0;
            return std::int32_t{0};
        });
    require(outcome.status == WriteAllStatus::InvalidProgress, "zero progress should fail");
}

void test_rejects_written_larger_than_remaining() {
    const std::uint8_t data[3]{};
    const auto outcome = romm3ds::writeAll(
        data, sizeof(data), 0,
        [](std::uint32_t* written, std::uint64_t, const void*, std::uint32_t size) {
            *written = size + 1;
            return std::int32_t{0};
        });
    require(outcome.status == WriteAllStatus::InvalidProgress, "over-reported progress should fail");
}

void test_preserves_underlying_write_error() {
    constexpr std::int32_t error = -12345;
    const std::uint8_t data[3]{};
    const auto outcome = romm3ds::writeAll(
        data, sizeof(data), 0,
        [](std::uint32_t* written, std::uint64_t, const void*, std::uint32_t) {
            *written = 0;
            return error;
        });
    require(outcome.status == WriteAllStatus::WriteError, "write error should be identified");
    require(outcome.result == error, "underlying write error should be preserved");
}

void test_accepts_nonzero_success_result() {
    const std::uint8_t data[3]{};
    const auto outcome = romm3ds::writeAll(
        data, sizeof(data), 0,
        [](std::uint32_t* written, std::uint64_t, const void*, std::uint32_t size) {
            *written = size;
            return std::int32_t{0x1234};
        });
    require(outcome.status == WriteAllStatus::Complete,
            "a nonnegative Result must remain a successful write");
    require(outcome.result == 0x1234, "successful Result value should be preserved");
}
} // namespace

int main() {
    test_retries_partial_writes_with_advanced_offsets();
    test_rejects_success_without_progress();
    test_rejects_written_larger_than_remaining();
    test_preserves_underlying_write_error();
    test_accepts_nonzero_success_result();
    std::cout << "All writeAll tests passed\n";
    return 0;
}
