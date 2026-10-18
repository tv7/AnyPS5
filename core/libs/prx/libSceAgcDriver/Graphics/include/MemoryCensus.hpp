#ifndef AGC_GRAPHICS_MEMORY_CENSUS_HPP
#define AGC_GRAPHICS_MEMORY_CENSUS_HPP

#include <vulkan/vulkan.h>

namespace AgcDriver::Graphics::MemoryCensus {

// With APS5_VRAM_CENSUS=<seconds>, returns counting wrappers for vkAllocateMemory and
// vkFreeMemory and prints the live allocations per memory type and calling site every
// <seconds> seconds. Otherwise, and for every other function, returns `function` unchanged.
PFN_vkVoidFunction Wrap(const char* name, PFN_vkVoidFunction function, const VkPhysicalDeviceMemoryProperties& memory);

// Names the purpose of the allocations made on this thread while it lives, so shared
// allocators (BufferPool) are split by what they allocate for. `label` must outlive the census.
class ScopedLabel {
public:
    explicit ScopedLabel(const char* label);
    ~ScopedLabel();
    ScopedLabel(const ScopedLabel&) = delete;
    ScopedLabel& operator=(const ScopedLabel&) = delete;

private:
    const char* previous;
};

}

#endif
