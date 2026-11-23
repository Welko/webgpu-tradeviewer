#pragma once

#include "Viewer/App.hpp"

#include "Gpu/Type.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace Viewer::Native {

/** The app's window, with GLFW started for as long as it's open. */
class Window
{
public:
    struct SizePx
    {
        std::uint32_t width;
        std::uint32_t height;
    };

    /**
     * A window titled "Trade Viewer", or why it couldn't open. A `fixed` one
     * is exactly `size` in physical pixels, even on a scaled screen, and
     * can't be resized.
     */
    [[nodiscard]] static std::expected<Window, std::string> open(SizePx size, bool fixed);

    Window(Window&& other) noexcept;
    Window& operator=(Window&&) = delete;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    /** What the engine draws to, or nothing on a window system it doesn't know. */
    [[nodiscard]] std::optional<Gpu::Type::Target::Any> getSurfaceTarget() const;

    /** The drawable area. */
    [[nodiscard]] SizePx getSizePx() const;

    /** Physical pixels per window coordinate. */
    [[nodiscard]] float getContentScale() const;

    /** From now on, the mouse, the keys and scale changes go to `app`, which takes the window's scale now. */
    void connect(App& app);

    [[nodiscard]] bool shouldClose() const;

    /** Whether the window system asked for a repaint, since takeRepaintRequest() last said so. */
    [[nodiscard]] bool hasRepaintRequest() const;
    [[nodiscard]] bool takeRepaintRequest();

    void setTitle(const std::string& title);

    // Events, for every window.

    static void pollEvents();
    static void waitForEvents();
    static void waitForEvents(double timeoutS);

    /** Ends waitForEvents() early, from any thread. */
    static void wake();

    /** Milliseconds since GLFW started. */
    [[nodiscard]] static double getTimeMs();

private:
    /** What the input callbacks need. */
    struct State;

    explicit Window(GLFWwindow* window) noexcept;

    GLFWwindow* mWindow;
    std::unique_ptr<State> mState;
};

}  // namespace Viewer::Native
