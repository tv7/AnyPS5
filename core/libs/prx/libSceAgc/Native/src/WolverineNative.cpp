#include "prx/libSceAgc/Native/include/WolverineNative.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

namespace WolverineNative {
namespace {

constexpr std::uintptr_t VtableAddress = 0x149279758ull;
constexpr int SlotCount = 137;
constexpr std::uintptr_t ExpectedDrawSlot91 = 0x143f7a600ull;

std::atomic<std::uint64_t> g_counts[SlotCount];
std::atomic<int> g_samples[SlotCount];
std::uintptr_t g_original[SlotCount];
FILE* g_log = nullptr;
std::mutex g_logMutex;

void __attribute__((sysv_abi)) OnCall(std::uint64_t slot, std::uint64_t self, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4) {
    g_counts[slot].fetch_add(1, std::memory_order_relaxed);
    if (g_samples[slot].fetch_add(1, std::memory_order_relaxed) < 6) {
        std::lock_guard lock(g_logMutex);
        std::fprintf(g_log, "[wn-call] slot=%llu this=%llx a1=%llx a2=%llx a3=%llx a4=%llx tid=%lu\n",
                     (unsigned long long)slot, (unsigned long long)self, (unsigned long long)a1, (unsigned long long)a2,
                     (unsigned long long)a3, (unsigned long long)a4, GetCurrentThreadId());
        std::fflush(g_log);
    }
}

struct Emitter {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void gprMem(std::uint8_t op, int reg, std::uint32_t off) {
        u8(static_cast<std::uint8_t>(0x48 | (reg >= 8 ? 4 : 0)));
        u8(op);
        u8(static_cast<std::uint8_t>(0x84 | ((reg & 7) << 3)));
        u8(0x24);
        u32(off);
    }
    void xmmMem(std::uint8_t op, int n, std::uint32_t off) {
        u8(0xF3);
        u8(0x0F);
        u8(op);
        u8(static_cast<std::uint8_t>(0x84 | (n << 3)));
        u8(0x24);
        u32(off);
    }
    void movRR(int dst, int src) {
        u8(static_cast<std::uint8_t>(0x48 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0)));
        u8(0x89);
        u8(static_cast<std::uint8_t>(0xC0 | ((src & 7) << 3) | (dst & 7)));
    }
};

constexpr int RAX = 0, RCX = 1, RDX = 2, RSI = 6, RDI = 7, R8 = 8, R9 = 9, R10 = 10, R11 = 11;

std::vector<std::uint8_t> BuildStub(int slot, std::uintptr_t target) {
    Emitter e;
    const int gprs[] = {RDI, RSI, RDX, RCX, R8, R9, RAX, R10, R11};
    e.u8(0x55);
    e.u8(0x48); e.u8(0x89); e.u8(0xE5);
    e.u8(0x48); e.u8(0x81); e.u8(0xEC); e.u32(0xE0);
    e.u8(0x48); e.u8(0x83); e.u8(0xE4); e.u8(0xF0);
    for (int i = 0; i < 9; ++i) e.gprMem(0x89, gprs[i], i * 8);
    for (int n = 0; n < 8; ++n) e.xmmMem(0x7F, n, 0x50 + n * 16);
    e.movRR(R9, R8);
    e.movRR(R8, RCX);
    e.movRR(RCX, RDX);
    e.movRR(RDX, RSI);
    e.movRR(RSI, RDI);
    e.u8(0xBF); e.u32(static_cast<std::uint32_t>(slot));
    e.u8(0x48); e.u8(0xB8); e.u64(reinterpret_cast<std::uint64_t>(&OnCall));
    e.u8(0xFF); e.u8(0xD0);
    for (int n = 0; n < 8; ++n) e.xmmMem(0x6F, n, 0x50 + n * 16);
    for (int i = 0; i < 9; ++i) e.gprMem(0x8B, gprs[i], i * 8);
    e.u8(0x48); e.u8(0x89); e.u8(0xEC);
    e.u8(0x5D);
    e.u8(0xFF); e.u8(0x25); e.u32(0);
    e.u64(target);
    return e.b;
}

void Reporter() {
    std::uint64_t last[SlotCount] = {};
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::lock_guard lock(g_logMutex);
        std::fprintf(g_log, "[wn-10s]");
        for (int i = 0; i < SlotCount; ++i) {
            std::uint64_t now = g_counts[i].load(std::memory_order_relaxed);
            if (now != last[i]) std::fprintf(g_log, " %d:%llu", i, (unsigned long long)(now - last[i]));
            last[i] = now;
        }
        std::fprintf(g_log, "\n");
        std::fflush(g_log);
    }
}

void Install() {
    const char* mode = std::getenv("APS5_WOL_NATIVE");
    if (mode == nullptr || mode[0] == '0') return;
    auto* vtable = reinterpret_cast<std::uintptr_t*>(VtableAddress);
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(vtable, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) return;
    if (vtable[91] != ExpectedDrawSlot91) return;
    const char* path = std::getenv("APS5_WOL_NATIVE_LOG");
    g_log = path != nullptr ? std::fopen(path, "w") : stderr;
    if (g_log == nullptr) g_log = stderr;
    std::size_t total = 0;
    std::vector<std::vector<std::uint8_t>> stubs;
    for (int i = 0; i < SlotCount; ++i) {
        g_original[i] = vtable[i];
        stubs.push_back(BuildStub(i, g_original[i]));
        total += (stubs.back().size() + 15) & ~std::size_t{15};
    }
    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    DWORD old = 0;
    VirtualProtect(vtable, SlotCount * sizeof(std::uintptr_t), PAGE_READWRITE, &old);
    std::size_t pos = 0;
    for (int i = 0; i < SlotCount; ++i) {
        std::memcpy(code + pos, stubs[i].data(), stubs[i].size());
        vtable[i] = reinterpret_cast<std::uintptr_t>(code + pos);
        pos += (stubs[i].size() + 15) & ~std::size_t{15};
    }
    VirtualProtect(vtable, SlotCount * sizeof(std::uintptr_t), old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, total);
    std::fprintf(g_log, "[wn] installed %d command-list stubs\n", SlotCount);
    std::fflush(g_log);
    std::thread(Reporter).detach();
}

}

void EnsureInstalled() {
    static std::once_flag once;
    std::call_once(once, Install);
}

}
