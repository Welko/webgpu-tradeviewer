#pragma once

#include <cstdint>
#include <optional>

namespace Core::Memory {

/**
 * The memory the whole process uses, to check what the app accounts for
 * against: natively the resident set (Linux only), on the web the wasm heap.
 * Nothing where it can't be told. Allocates nothing, so it still works once
 * memory has run out.
 */
[[nodiscard]] std::optional<std::uint64_t> getProcessBytes();

} // namespace Core::Memory
