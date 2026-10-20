#include <algorithm>
#include <vector>
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
#include <cstdlib>
#include <stdexcept>

namespace AgcDriver::DriverDetail {

double TraceMs() {
    static const auto origin = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - origin).count();
}

std::uint64_t SkippedShader() {
    static const std::uint64_t skipped = [] { const char* text = std::getenv("APS5_SKIP_SHADER"); return text ? std::strtoull(text, nullptr, 16) : 0ull; }();
    return skipped;
}

bool ShaderSkipped(std::uint64_t address) {
    static const bool skipAll = std::getenv("APS5_WOL_SKIP_WORK") != nullptr;
    if (skipAll) return true;
    static const std::vector<std::uint64_t> skipped = [] {
        std::vector<std::uint64_t> addresses;
        const char* text = std::getenv("APS5_SKIP_SHADER");
        while (text != nullptr && *text != '\0') {
            char* end = nullptr;
            const auto value = std::strtoull(text, &end, 16);
            if (end == text) break;
            if (value != 0) addresses.push_back(value);
            text = *end == ',' ? end + 1 : end;
        }
        return addresses;
    }();
    return address != 0 && std::find(skipped.begin(), skipped.end(), address) != skipped.end();
}

void require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(std::string("AGC driver: ") + reason);
    }
}

std::uint32_t readRegister(const Registers& registers, std::uint32_t offset) {
    const auto it = registers.find(offset);
    require(it != registers.end(), "required shader register has not been written");
    return it->second;
}

std::uint32_t readUserData(const Registers& shader, std::uint32_t offset) {
    const auto it = shader.find(offset);
    return it == shader.end() ? 0u : it->second;
}

}
