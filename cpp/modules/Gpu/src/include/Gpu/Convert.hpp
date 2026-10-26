#pragma once

#include "Gpu/Type.hpp"

#include "Core/Log.hpp"

#include <webgpu/webgpu.h>

#include <string_view>

namespace Gpu::Convert {

namespace FromWgpu {

/**
 * Converts a WGPUStringView into a std::string_view safely, handling
 * null pointers, WGPU_STRLEN null-termination, and explicit lengths.
 */
inline std::string_view stringView(WGPUStringView view) noexcept
{
    // Case 1: Handle explicit zero length or null data with zero length
    if (view.length == 0) {
        return std::string_view{};
    }

    // Case 2: Handle WGPU_STRLEN (null-terminated string)
    if (view.length == WGPU_STRLEN) {
        if (view.data == nullptr) {
            return std::string_view{};
        }
        return std::string_view(view.data);
    }

    // Case 3: Handle null pointer with non-zero length (invalid per spec, fallback safely)
    if (view.data == nullptr) {
        return std::string_view{};
    }

    // Case 4: Explicitly sized string view
    return std::string_view(view.data, view.length);
}

inline std::string_view backend(WGPUBackendType type)
{
    switch (type)
    {
    case WGPUBackendType_Undefined: return "Undefined";
    case WGPUBackendType_Null: return "Null";
    case WGPUBackendType_WebGPU: return "WebGPU";
    case WGPUBackendType_Vulkan: return "Vulkan";
    case WGPUBackendType_D3D11: return "D3D11";
    case WGPUBackendType_D3D12: return "D3D12";
    case WGPUBackendType_OpenGL: return "OpenGL";
    case WGPUBackendType_OpenGLES: return "OpenGLES";
    case WGPUBackendType_Metal: return "Metal";
    case WGPUBackendType_Force32: return "Force32";
    }
    Core::Log::error("Unknown WGPUBackendType {}", static_cast<std::underlying_type_t<WGPUBackendType>>(type));
    return "Unknown";
}

inline std::string_view adapter(WGPUAdapterType type)
{
    switch (type)
    {
    case WGPUAdapterType_Unknown: return "Unknown";
    case WGPUAdapterType_DiscreteGPU: return "DiscreteGPU";
    case WGPUAdapterType_IntegratedGPU: return "IntegratedGPU";
    case WGPUAdapterType_CPU: return "CPU";
    case WGPUAdapterType_Force32: return "Force32";
    }
    Core::Log::error("Unknown WGPUAdapterType {}", static_cast<std::underlying_type_t<WGPUAdapterType>>(type));
    return "Unknown";
}

} // namespace FromWgpu

namespace ToWgpu {

/** The view borrows `view`'s characters: keep them alive while it is used. */
inline WGPUStringView stringView(std::string_view view) noexcept
{
    return WGPUStringView{ .data = view.data(), .length = view.size() };
}

inline WGPUPowerPreference powerPreference(Type::PowerPreference preference) noexcept
{
    switch (preference)
    {
    case Type::PowerPreference::LowPower: return WGPUPowerPreference_LowPower;
    case Type::PowerPreference::HighPerformance: return WGPUPowerPreference_HighPerformance;
    case Type::PowerPreference::Default: break;
    }
    return WGPUPowerPreference_Undefined;
}

} // namespace ToWgpu

} // namespace Gpu::Convert