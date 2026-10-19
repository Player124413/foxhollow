#pragma once

#include <cstddef>

namespace aurora::gfx {
constexpr bool render_thread_pipeline_compilation(bool android, bool openGles) {
  return android && openGles;
}

constexpr bool compile_pipeline_inline(bool workerThread, bool renderThreadOnly, bool blocking,
                                       size_t built, size_t budget) {
  return !workerThread && !renderThreadOnly && (blocking || built < budget);
}
}
