#pragma once

#include "Gpu/Context.hpp"
#include "Gpu/Handle.hpp"

#include <doctest/doctest.h>

#include <webgpu/webgpu.h>

#include <optional>
#include <utility>

/** What the tests on a real GPU device share. */
namespace DeviceTesting {

/** A context on any GPU, or none, with a failed check. */
inline std::optional<Gpu::Context> createContext()
{
    std::optional<Gpu::Context> context;
    Gpu::Handle instance(wgpuCreateInstance(nullptr));
    Gpu::Context::create(std::move(instance), nullptr, Gpu::Type::PowerPreference::Default, [&](Gpu::Context::Result result) {
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result)
        {
            context = std::move(*result);
        }
    });
    return context;
}

}  // namespace DeviceTesting
