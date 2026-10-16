#include "library_state.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

void test_detects_downloaded_payloads() {
    char root_template[] = "/tmp/pocketshelf-library-test-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "temporary root created");
    const std::string path = std::string(root) + "/game.gba";
    std::ofstream(path, std::ios::binary) << "payload";
    const auto present = romm3ds::inspectLocalPayload(path);
    require(present.present && present.size == 7, "downloaded payload is detected and sized");
    require(!romm3ds::inspectLocalPayload(path + ".missing").present,
            "missing payload is reported absent");
    std::remove(path.c_str());
    ::rmdir(root);
}

void test_install_receipts_are_strict_and_atomic() {
    char root_template[] = "/tmp/pocketshelf-receipt-test-XXXXXX";
    char* root = mkdtemp(root_template);
    require(root != nullptr, "receipt temporary root created");
    const std::string path = romm3ds::installReceiptPath(root, 701);
    require(path == std::string(root) + "/701.titleid", "receipt path uses file id");
    require(romm3ds::saveInstallReceiptAtomic(path, 0x0004000000055D00ULL),
            "install receipt saves atomically");
    std::uint64_t title_id = 0;
    require(romm3ds::loadInstallReceipt(path, title_id), "install receipt loads");
    require(title_id == 0x0004000000055D00ULL, "title id round trips exactly");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "not-a-title\n";
    require(!romm3ds::loadInstallReceipt(path, title_id), "malformed receipt is rejected");
    std::remove(path.c_str());
    ::rmdir(root);
}
}

int main() {
    test_detects_downloaded_payloads();
    test_install_receipts_are_strict_and_atomic();
    std::cout << "All library state tests passed\n";
    return 0;
}
