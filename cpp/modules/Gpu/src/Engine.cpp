#include "Gpu/Engine.hpp"

#include "Gpu/Frame.hpp"

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
    auto frame = Frame::begin(*mContext, *mSurface, mNowMs);
    if (!frame) {
        return;
    }
    for (const auto& pass : mPasses) {
        pass->encode(*frame);
    }
    frame->submit();
    mDirty = false;
}

} // namespace Gpu
