#include "prx/libSceAgcDriver/Graphics/include/MemoryCensus.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace AgcDriver::Graphics::MemoryCensus {

namespace {

struct Site {
    std::uint64_t liveBytes = 0;
    std::uint64_t liveBlocks = 0;
    std::uint64_t allocations = 0;
    std::uint64_t allocatedBytes = 0;
};

struct SiteKey {
    std::uint32_t type;
    std::uintptr_t site;
    const char* label;
    bool operator==(const SiteKey& other) const { return type == other.type && site == other.site && label == other.label; }
};

struct SiteKeyHash {
    std::size_t operator()(const SiteKey& key) const { return std::hash<std::uintptr_t>()(key.site) ^ std::hash<const void*>()(key.label) ^ (static_cast<std::size_t>(key.type) << 1); }
};

struct Block {
    VkDeviceSize bytes;
    SiteKey key;
};

thread_local const char* currentLabel = nullptr;

struct State {
    std::mutex mutex;
    std::unordered_map<VkDeviceMemory, Block> blocks;
    std::unordered_map<SiteKey, Site, SiteKeyHash> sites;
    VkPhysicalDeviceMemoryProperties memory{};
    PFN_vkAllocateMemory allocate = nullptr;
    PFN_vkFreeMemory free = nullptr;
    std::uint64_t failures = 0;
    std::jthread reporter;
};

State& state() {
    static State instance;
    return instance;
}

unsigned interval() {
    static const unsigned seconds = [] {
        const char* value = std::getenv("APS5_VRAM_CENSUS");
        if (value == nullptr) return 0u;
        const auto parsed = std::strtoul(value, nullptr, 10);
        return parsed == 0 ? 10u : static_cast<unsigned>(parsed);
    }();
    return seconds;
}

void describe(std::uintptr_t address, char* text, std::size_t size) {
#ifdef _WIN32
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module)) {
        GetModuleFileNameA(module, name, sizeof(name));
        const char* base = std::strrchr(name, '\\');
        std::snprintf(text, size, "%s+0x%llx", base ? base + 1 : name, static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
        return;
    }
#endif
    std::snprintf(text, size, "0x%llx", static_cast<unsigned long long>(address));
}

void report() {
    auto& census = state();
    struct Row {
        SiteKey key;
        Site site;
    };
    std::vector<Row> rows;
    std::vector<std::uint64_t> heapLive(VK_MAX_MEMORY_HEAPS, 0);
    std::uint64_t failures = 0;
    {
        std::lock_guard lock(census.mutex);
        for (const auto& [key, site] : census.sites) {
            rows.push_back({key, site});
            heapLive[census.memory.memoryTypes[key.type].heapIndex] += site.liveBytes;
        }
        failures = census.failures;
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.site.liveBytes > b.site.liveBytes; });
    std::fprintf(stderr, "[vram] census:");
    for (std::uint32_t heap = 0; heap < census.memory.memoryHeapCount; ++heap) {
        const bool local = (census.memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        std::fprintf(stderr, " heap %u (%s) %llu MiB live;", heap, local ? "device-local" : "host", static_cast<unsigned long long>(heapLive[heap] >> 20u));
    }
    std::fprintf(stderr, " %llu failed allocations\n", static_cast<unsigned long long>(failures));
    for (std::size_t i = 0; i < rows.size() && i < 16; ++i) {
        const auto& [key, site] = rows[i];
        char where[320];
        describe(key.site, where, sizeof(where));
        const auto heap = census.memory.memoryTypes[key.type].heapIndex;
        std::fprintf(stderr, "[vram]   %s%s%s type %u heap %u: %llu MiB live in %llu blocks; %llu allocations, %llu MiB allocated since start\n", where, key.label ? " " : "", key.label ? key.label : "", key.type, heap, static_cast<unsigned long long>(site.liveBytes >> 20u), static_cast<unsigned long long>(site.liveBlocks), static_cast<unsigned long long>(site.allocations), static_cast<unsigned long long>(site.allocatedBytes >> 20u));
    }
    std::fflush(stderr);
}

VKAPI_ATTR VkResult VKAPI_CALL CountedAllocate(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* callbacks, VkDeviceMemory* memory) {
    const auto site = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    auto& census = state();
    const auto result = census.allocate(device, info, callbacks, memory);
    std::lock_guard lock(census.mutex);
    if (result != VK_SUCCESS) {
        ++census.failures;
        return result;
    }
    const SiteKey key{info->memoryTypeIndex, site, currentLabel};
    census.blocks[*memory] = {info->allocationSize, key};
    auto& entry = census.sites[key];
    entry.liveBytes += info->allocationSize;
    ++entry.liveBlocks;
    ++entry.allocations;
    entry.allocatedBytes += info->allocationSize;
    return result;
}

VKAPI_ATTR void VKAPI_CALL CountedFree(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* callbacks) {
    auto& census = state();
    {
        std::lock_guard lock(census.mutex);
        if (const auto found = census.blocks.find(memory); found != census.blocks.end()) {
            auto& entry = census.sites[found->second.key];
            entry.liveBytes -= found->second.bytes;
            --entry.liveBlocks;
            census.blocks.erase(found);
        }
    }
    census.free(device, memory, callbacks);
}

}

ScopedLabel::ScopedLabel(const char* label) : previous(currentLabel) {
    currentLabel = label;
}

ScopedLabel::~ScopedLabel() {
    currentLabel = previous;
}

PFN_vkVoidFunction Wrap(const char* name, PFN_vkVoidFunction function, const VkPhysicalDeviceMemoryProperties& memory) {
    if (function == nullptr || interval() == 0) return function;
    const bool allocate = std::strcmp(name, "vkAllocateMemory") == 0;
    if (!allocate && std::strcmp(name, "vkFreeMemory") != 0) return function;
    auto& census = state();
    {
        std::lock_guard lock(census.mutex);
        if (allocate) census.allocate = reinterpret_cast<PFN_vkAllocateMemory>(function);
        else census.free = reinterpret_cast<PFN_vkFreeMemory>(function);
        if (census.memory.memoryTypeCount == 0) census.memory = memory;
        if (!census.reporter.joinable()) {
            census.reporter = std::jthread([](std::stop_token token) {
                auto next = std::chrono::steady_clock::now();
                while (!token.stop_requested()) {
                    next += std::chrono::seconds(interval());
                    while (!token.stop_requested() && std::chrono::steady_clock::now() < next) std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    if (!token.stop_requested()) report();
                }
            });
        }
    }
    return allocate ? reinterpret_cast<PFN_vkVoidFunction>(&CountedAllocate) : reinterpret_cast<PFN_vkVoidFunction>(&CountedFree);
}

}
