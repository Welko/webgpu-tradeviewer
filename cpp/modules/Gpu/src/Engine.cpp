#include "Gpu/Engine.hpp"

#include "Gpu/Frame.hpp"

#include "Core/Profile.hpp"

#include <utility>

namespace Gpu {

void Engine::create(const Type::Target::Any& target, Type::PowerPreference powerPreference, Callback onDone)
{
    Context::create(powerPreference, [target, onDone = std::move(onDone)](Context::Result context) {
        if (!context) {
            onDone(std::unexpected(std::move(context.error())));
            return;
        }
        auto surface = Surface::create(**context, target);
        if (!surface) {
            onDone(std::unexpected(std::move(surface.error())));
            return;
        }
        onDone(Engine(std::move(*context), std::move(*surface)));
    });
}

Engine::Engine(std::unique_ptr<Context> context, std::unique_ptr<Surface> surface) noexcept
    : mContext(std::move(context)),
      mSurface(std::move(surface))
{}

void Engine::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0 || (width == mWidth && height == mHeight)) {
        return;
    }
    mWidth = width;
    mHeight = height;
    mSurface->resize(width, height);
    mDirty = true;
}

void Engine::addPass(std::unique_ptr<Pass> pass)
{
    mPasses.push_back(std::move(pass));
    mDirty = true;
}

void Engine::render()
{
    Type::FrameStats stats;
    stats.timed = Core::Profile::Compiled && mProfiling;

    auto frame = [&] {
        const Core::Profile::Scope acquire(stats.cpuAcquireMs, stats.timed);
        return Frame::begin(*mContext, *mSurface, mNowMs);
    }();
    if (!frame) {
        return;
    }
    {
        const Core::Profile::Scope encode(stats.cpuEncodeMs, stats.timed);
        for (const auto& pass : mPasses) {
            pass->encode(*frame);
        }
    }
    {
        const Core::Profile::Scope submit(stats.cpuSubmitMs, stats.timed);
        frame->submit();
    }

    const auto& resources = mContext->resources;
    stats.frame = mStats.frame + 1;
    stats.buffers = resources.count(Resource::Kind::Buffer);
    stats.textures = resources.count(Resource::Kind::Texture);
    stats.gpuBytes = resources.totalBytes();
    mStats = stats;
    mDirty = false;
}

} // namespace Gpu
