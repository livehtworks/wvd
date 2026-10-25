#pragma once
#include "platform/windows/runtime_files.hpp"

namespace wvd::platform {
struct AtomicWriteTestAccess {
    using Phase = detail::AtomicWritePhase;
    static void install(std::function<bool(Phase, const std::filesystem::path &)> callback) {
        detail::AtomicWriteFaults::install(std::move(callback));
    }
    static void clear() { detail::AtomicWriteFaults::install({}); }
};
}
