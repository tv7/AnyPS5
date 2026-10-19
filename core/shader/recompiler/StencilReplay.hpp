#ifndef CORE_SHADER_RECOMPILER_STENCILREPLAY_HPP
#define CORE_SHADER_RECOMPILER_STENCILREPLAY_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <vector>

namespace ShaderRecompiler {

[[nodiscard]] std::vector<std::uint32_t> StencilReferenceValues(const IrProgram& program);
[[nodiscard]] std::vector<std::uint32_t> StencilReferenceValues(const IrProgram& program, std::uint32_t shift);
[[nodiscard]] bool StencilReplaySafe(const IrProgram& program);
[[nodiscard]] bool StencilReferenceDefinedForKeptInvocations(const IrProgram& program);
// Why a pixel shader's stencil reference export cannot be replayed (nullptr: it can).
[[nodiscard]] const char* StencilReplayRefusal(const IrProgram& program, std::uint32_t shift, bool earlyZ, bool orderedPixelShader);
// APS5_STENCIL_EXPORT_DROP: a stencil reference export that cannot be replayed is dropped (the draw
// keeps the pipeline's reference) instead of refusing the shader.
[[nodiscard]] bool StencilExportDropEnabled();

}

#endif
