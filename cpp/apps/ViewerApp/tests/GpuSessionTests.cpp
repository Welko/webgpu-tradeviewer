#include "Viewer/GpuSession.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace {

using Gpu::Market::Engine;

const Engine::Params Params{ .target = Gpu::Type::Target::Offscreen{ .width = 16, .height = 16 } };

/** A session on a new engine, or none, with a failed check. */
std::unique_ptr<Viewer::GpuSession> createSession()
{
    std::unique_ptr<Viewer::GpuSession> session;
    Engine::create(Params, [&](Engine::CreatedResult result) {
        CHECK_MESSAGE(result, (result ? "" : result.error()));
        if (result)
        {
            session = std::make_unique<Viewer::GpuSession>(std::move(*result), Params,
                                                           [](Engine&) -> std::expected<void, std::string> { return {}; });
        }
    });
    return session;
}

/** Loses the session's engine's device, and waits until it's noticed. wgpu-native notices on the next call to the device. */
void lose(Viewer::GpuSession& session)
{
    auto* engine = session.getEngine();
    CHECK(engine != nullptr);
    if (!engine)
    {
        return;
    }
    engine->loseDevice();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!engine->getHealth().lost && std::chrono::steady_clock::now() < deadline)
    {
        engine->render();
        engine->processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(engine->getHealth().lost);
}

}  // namespace

TEST_CASE("The GPU comes back when lost now and then, and not when it keeps being lost")
{
    const auto session = createSession();
    if (!session)
    {
        return;
    }
    // Far apart, however many times.
    double nowMs = 0.0;
    for (std::size_t restarts = 1; restarts <= 5; ++restarts)
    {
        lose(*session);
        nowMs += 30'000.0;
        session->restartIfLost(nowMs);
        CHECK(session->getEngine() != nullptr);
        CHECK(session->getHealth().restarts == restarts);
        CHECK_FALSE(session->getHealth().failure);
    }

    // Close together: three restarts in the minute, and no fourth.
    nowMs += 120'000.0;
    for (int restart = 0; restart < 3; ++restart)
    {
        lose(*session);
        nowMs += 1'000.0;
        session->restartIfLost(nowMs);
        CHECK(session->getEngine() != nullptr);
    }
    lose(*session);
    nowMs += 1'000.0;
    session->restartIfLost(nowMs);
    CHECK(session->getEngine() == nullptr);
    const auto health = session->getHealth();
    CHECK(health.restarts == 8);
    CHECK_FALSE(health.restarting);
    CHECK(health.failure == "The GPU was lost 4 times in a minute, so the viewer stopped restarting it");

    // It stays stopped, without complaint.
    session->restartIfLost(nowMs + 600'000.0);
    CHECK(session->getEngine() == nullptr);
}
