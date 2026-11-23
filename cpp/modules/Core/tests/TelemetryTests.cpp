#include "Core/Telemetry.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace {

using Core::Telemetry::Client;
using Core::Telemetry::RecordingSink;

/** A client sending to a recording sink, and the sink. */
std::pair<std::unique_ptr<Client>, RecordingSink*> createEnabled()
{
    auto client = std::make_unique<Client>("native", "abc1234", "0.1.0");
    auto sink = std::make_unique<RecordingSink>();
    auto* recording = sink.get();
    client->setSink(std::move(sink));
    return { std::move(client), recording };
}

}  // namespace

TEST_CASE("Without a sink, which means no consent, nothing is sent")
{
    Client client("native", "abc1234", "0.1.0");
    CHECK_FALSE(client.isEnabled());
    client.send("data_loaded", {});
    CHECK(client.getDropped() == 0);
}

TEST_CASE("Every event carries the schema, the session, the platform and the build")
{
    if (!Core::Telemetry::Compiled)
    {
        return;
    }
    auto [client, sink] = createEnabled();
    client->send("data_loaded", { { .name = "rows", .value = std::int64_t{ 5 } } });
    CHECK(sink->events.size() == 1);
    if (sink->events.empty())
    {
        return;
    }
    const auto& event = sink->events.front();
    CHECK(event.name == "data_loaded");
    CHECK(*event.find("schema") == Core::Telemetry::Value(Client::Schema));
    CHECK(*event.find("platform") == Core::Telemetry::Value(std::string("native")));
    CHECK(*event.find("build") == Core::Telemetry::Value(std::string("abc1234")));
    CHECK(*event.find("rows") == Core::Telemetry::Value(std::int64_t{ 5 }));
    const auto* session = event.find("session");
    CHECK(session != nullptr);
    if (session)
    {
        CHECK(std::get<std::string>(*session).size() == 16);
    }
}

TEST_CASE("A session stops at its cap, counting what it held back")
{
    if (!Core::Telemetry::Compiled)
    {
        return;
    }
    auto [client, sink] = createEnabled();
    for (std::size_t i = 0; i < Client::MaxEvents + 7; ++i)
    {
        client->send("view_summary", {});
    }
    CHECK(sink->events.size() == Client::MaxEvents);
    CHECK(client->getDropped() == 7);
}

TEST_CASE("Taking consent back stops sending at once, and giving it again starts a new session")
{
    if (!Core::Telemetry::Compiled)
    {
        return;
    }
    // The client owns its sink, and destroys it on revoking: this outlives it.
    struct Counting final : Core::Telemetry::Sink
    {
        explicit Counting(std::size_t& count)
            : count(count)
        {}

        void send(const Core::Telemetry::Event& /*event*/) override { ++count; }

        std::size_t& count;
    };
    std::size_t sent = 0;
    auto [client, sink] = createEnabled();
    client->send("view_summary", {});
    const auto first = std::get<std::string>(*sink->events.front().find("session"));
    client->setSink(std::make_unique<Counting>(sent));
    client->setSink(nullptr);
    CHECK_FALSE(client->isEnabled());
    client->send("view_summary", {});
    CHECK(sent == 0);

    auto again = std::make_unique<RecordingSink>();
    auto* recording = again.get();
    client->setSink(std::move(again));
    client->send("view_summary", {});
    CHECK(recording->events.size() == 1);
    CHECK(std::get<std::string>(*recording->events.front().find("session")) != first);
}
