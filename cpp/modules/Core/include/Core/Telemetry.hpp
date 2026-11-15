#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Set by CMake from the TRADEVIEWER_TELEMETRY option; on without it.
#ifndef TRADEVIEWER_TELEMETRY
#define TRADEVIEWER_TELEMETRY 1
#endif

/**
 * Anonymous usage and performance events, sent only while the user agrees
 * (see design/telemetry.md). Knows nothing about markets or where events go:
 * a Sink takes them there.
 */
namespace Core::Telemetry {

/** Whether telemetry is compiled in. When false, Client never sends anything. */
inline constexpr bool Compiled = TRADEVIEWER_TELEMETRY != 0;

using Value = std::variant<bool, std::int64_t, double, std::string>;

struct Property
{
    std::string name;
    Value value;

    bool operator==(const Property&) const = default;
};

struct Event
{
    std::string name;
    std::vector<Property> properties;

    /** The property called `name`, or null. */
    [[nodiscard]] const Value* find(std::string_view name) const
    {
        for (const auto& property : properties) {
            if (property.name == name) {
                return &property.value;
            }
        }
        return nullptr;
    }
};

/** Where events go: an analytics service, or a test's list. */
class Sink
{
public:
    virtual ~Sink() = default;
    virtual void send(const Event& event) = 0;
};

/** Keeps every event it's sent, for tests. */
class RecordingSink final : public Sink
{
public:
    void send(const Event& event) override { events.push_back(event); }

    std::vector<Event> events;
};

/**
 * Sends events to a sink while one is set: only once the user agreed. Adds
 * what every event carries (the schema, a random id for this session, the
 * platform, the build), and stops after MaxEvents a session, counting the rest.
 */
class Client
{
public:
    static constexpr std::int64_t Schema = 1;
    static constexpr std::size_t MaxEvents = 200;

    /** `build` names the code: the commit, and the version. */
    Client(std::string platform, std::string build, std::string version)
        : mPlatform(std::move(platform)),
          mBuild(std::move(build)),
          mVersion(std::move(version))
    {}

    /** Starts a new session sending to `sink`, or stops sending with null. Does nothing when compiled out. */
    void setSink(std::unique_ptr<Sink> sink)
    {
        if (!Compiled) {
            return;
        }
        mSink = std::move(sink);
        mSent = 0;
        mDropped = 0;
        mSession = mSink ? makeSessionId() : std::string();
    }

    [[nodiscard]] bool isEnabled() const { return mSink != nullptr; }

    /** Sends `name` with `properties` and the common ones, if enabled and under the cap. */
    void send(std::string name, std::vector<Property> properties)
    {
        if (!mSink) {
            return;
        }
        if (mSent >= MaxEvents) {
            ++mDropped;
            return;
        }
        ++mSent;
        auto common = getCommonProperties();
        properties.insert(properties.begin(), std::make_move_iterator(common.begin()), std::make_move_iterator(common.end()));
        mSink->send({ .name = std::move(name), .properties = std::move(properties) });
    }

    /** What every event carries, for events sent elsewhere, such as from a page whose wasm module crashed. */
    [[nodiscard]] std::vector<Property> getCommonProperties() const
    {
        return {
            { .name = "schema", .value = Schema },
            { .name = "session", .value = mSession },
            { .name = "platform", .value = mPlatform },
            { .name = "build", .value = mBuild },
            { .name = "appVersion", .value = mVersion },
        };
    }

    /** Events not sent this session, past MaxEvents. */
    [[nodiscard]] std::size_t getDropped() const { return mDropped; }

private:
    /** 16 random hex digits: enough to tell sessions apart, and nothing else. */
    static std::string makeSessionId()
    {
        std::random_device device;
        const std::uint64_t value = (std::uint64_t{ device() } << 32) | device();
        std::string id(16, '0');
        for (std::size_t i = 0; i < id.size(); ++i) {
            id[i] = "0123456789abcdef"[(value >> (4 * i)) & 0xF];
        }
        return id;
    }

    std::string mPlatform;
    std::string mBuild;
    std::string mVersion;
    std::unique_ptr<Sink> mSink;
    std::string mSession;
    std::size_t mSent = 0;
    std::size_t mDropped = 0;
};

} // namespace Core::Telemetry
