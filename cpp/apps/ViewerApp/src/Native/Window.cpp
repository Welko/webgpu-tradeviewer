#include "Native/Window.hpp"

#include "Native/Report.hpp"

#include "Core/Log.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cmath>
#include <utility>

namespace Viewer::Native {

namespace {

// Zooming per step of the mouse wheel.
constexpr double ZoomPerWheelStep = 1.25;

/** Window coordinates in physical pixels, which differ on scaled screens. */
double toPx(GLFWwindow* window, double cssPx)
{
    int windowWidthCssPx = 0;
    int windowWidthPx = 0;
    glfwGetWindowSize(window, &windowWidthCssPx, nullptr);
    glfwGetFramebufferSize(window, &windowWidthPx, nullptr);
    return windowWidthCssPx > 0 ? cssPx * windowWidthPx / windowWidthCssPx : cssPx;
}

}  // namespace

struct Window::State
{
    App* app = nullptr;
    bool repaint = false;  // the window system asked for a repaint
    bool dragging = false;
    double lastXCssPx = 0.0;  // window coordinates
    bool summariesAllowed = true;

    static State& of(GLFWwindow* window) { return *static_cast<State*>(glfwGetWindowUserPointer(window)); }

    /** R: back to 12:00–13:00, D: the whole day, M: marker sizes by area or log, S: summaries on or off. */
    void press(int key)
    {
        switch (key)
        {
        case GLFW_KEY_R:
            if (const auto initialView = app->getInitialView())
            {
                app->setView(*initialView);
            }
            break;
        case GLFW_KEY_D:
            if (!app->getTrades().empty())
            {
                app->setView(Core::getUtcDay(app->getTrades().timeUs.front()));
            }
            break;
        case GLFW_KEY_M: {
            using Core::Plot::MarkerScale;
            app->setMarkerScale(app->getMarkerSizing().scale == MarkerScale::Area ? MarkerScale::Log : MarkerScale::Area);
            logLegend(*app);
            break;
        }
        case GLFW_KEY_S:
            summariesAllowed = !summariesAllowed;
            app->setSummariesAllowed(summariesAllowed);
            Core::Log::info("{}", summariesAllowed ? "Crowded views show column summaries" : "Every trade is drawn, however crowded");
            break;
        default:
            break;
        }
    }
};

std::expected<Window, std::string> Window::open(SizePx size, bool fixed)
{
    if (!glfwInit())
    {
        return std::unexpected("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (fixed)
    {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    }
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(size.width), static_cast<int>(size.height), "Trade Viewer", nullptr, nullptr);
    if (window == nullptr)
    {
        glfwTerminate();
        return std::unexpected("Failed to create a window");
    }
    return Window(window);
}

Window::Window(GLFWwindow* window) noexcept
    : mWindow(window),
      mState(std::make_unique<State>())
{
    glfwSetWindowUserPointer(mWindow, mState.get());
    glfwSetWindowRefreshCallback(mWindow, [](GLFWwindow* refreshed) { State::of(refreshed).repaint = true; });
}

Window::Window(Window&& other) noexcept
    : mWindow(std::exchange(other.mWindow, nullptr)),
      mState(std::move(other.mState))
{}

Window::~Window()
{
    if (mWindow != nullptr)
    {
        glfwDestroyWindow(mWindow);
        glfwTerminate();
    }
}

std::optional<Gpu::Type::Target::Any> Window::getSurfaceTarget() const
{
    // Linux's window systems, as GLFW was built with them.
    switch (glfwGetPlatform())
    {
#ifdef __linux__
#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    case GLFW_PLATFORM_WAYLAND:
        return Gpu::Type::Target::Wayland{ .display = glfwGetWaylandDisplay(), .surface = glfwGetWaylandWindow(mWindow) };
#endif
#ifdef GLFW_EXPOSE_NATIVE_X11
    case GLFW_PLATFORM_X11:
        return Gpu::Type::Target::Xlib{ .display = glfwGetX11Display(), .window = glfwGetX11Window(mWindow) };
#endif
#else
#error "getSurfaceTarget() knows Linux's window systems only"
#endif
    default:
        return std::nullopt;
    }
}

Window::SizePx Window::getSizePx() const
{
    int widthPx = 0;
    int heightPx = 0;
    glfwGetFramebufferSize(mWindow, &widthPx, &heightPx);
    return { .width = static_cast<std::uint32_t>(widthPx), .height = static_cast<std::uint32_t>(heightPx) };
}

float Window::getContentScale() const
{
    float scale = 1.0f;
    glfwGetWindowContentScale(mWindow, &scale, nullptr);
    return scale;
}

void Window::connect(App& app)
{
    mState->app = &app;

    glfwSetScrollCallback(mWindow, [](GLFWwindow* scrolled, double /*xSteps*/, double steps) {
        double xCssPx = 0.0;
        glfwGetCursorPos(scrolled, &xCssPx, nullptr);
        // Up zooms in.
        State::of(scrolled).app->zoomAt(toPx(scrolled, xCssPx), std::pow(ZoomPerWheelStep, -steps));
    });

    glfwSetMouseButtonCallback(mWindow, [](GLFWwindow* clicked, int button, int action, int /*mods*/) {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            auto& state = State::of(clicked);
            state.dragging = action == GLFW_PRESS;
            glfwGetCursorPos(clicked, &state.lastXCssPx, nullptr);
        }
    });

    glfwSetCursorPosCallback(mWindow, [](GLFWwindow* moved, double xCssPx, double yCssPx) {
        auto& state = State::of(moved);
        if (state.dragging)
        {
            state.app->panBy(toPx(moved, xCssPx - state.lastXCssPx));
            state.lastXCssPx = xCssPx;
        }
        state.app->setPointer(toPx(moved, xCssPx), toPx(moved, yCssPx));
    });

    glfwSetCursorEnterCallback(mWindow, [](GLFWwindow* crossed, int entered) {
        if (entered == GLFW_FALSE)
        {
            State::of(crossed).app->clearPointer();
        }
    });

    glfwSetKeyCallback(mWindow, [](GLFWwindow* pressed, int key, int /*scancode*/, int action, int /*mods*/) {
        if (action == GLFW_PRESS)
        {
            State::of(pressed).press(key);
        }
    });

    glfwSetWindowContentScaleCallback(
        mWindow, [](GLFWwindow* scaled, float xScale, float /*yScale*/) { State::of(scaled).app->setPixelRatio(xScale); });

    app.setPixelRatio(getContentScale());
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(mWindow) == GLFW_TRUE;
}

bool Window::hasRepaintRequest() const
{
    return mState->repaint;
}

bool Window::takeRepaintRequest()
{
    return std::exchange(mState->repaint, false);
}

void Window::setTitle(const std::string& title)
{
    glfwSetWindowTitle(mWindow, title.c_str());
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::waitForEvents()
{
    glfwWaitEvents();
}

void Window::waitForEvents(double timeoutS)
{
    glfwWaitEventsTimeout(timeoutS);
}

void Window::wake()
{
    glfwPostEmptyEvent();
}

double Window::getTimeMs()
{
    return glfwGetTime() * 1000.0;
}

}  // namespace Viewer::Native
