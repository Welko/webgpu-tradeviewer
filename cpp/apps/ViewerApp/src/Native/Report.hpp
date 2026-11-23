#pragma once

#include "Viewer/App.hpp"

#include <cstdint>
#include <string>

/** What the native window can't show on screen yet, said in its title and the log. */
namespace Viewer::Native {

/** A UTC time to the millisecond, such as 2019-12-01 00:00:03.572. */
[[nodiscard]] std::string formatUtc(std::int64_t timeUs);

/** The picked trade or column, or else the view's range and what it holds. */
[[nodiscard]] std::string getWindowTitle(const App& app);

/** The trades' count, time span and total amount, `loaded` from a file or synthetic. */
void logTrades(const App& app, bool loaded);

/** What marker colors and sizes mean, as the web app's legend shows it. */
void logLegend(const App& app);

/** What the trades and what's derived from them hold, the GPU's share, and the whole process, to compare. */
void logMemory(const App& app);

/** The mouse and keyboard controls. */
void logControls();

}  // namespace Viewer::Native
