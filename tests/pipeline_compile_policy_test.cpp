#include "../extern/aurora/lib/gfx/pipeline_compile_policy.hpp"
#include <cassert>
#include <initializer_list>

int main() {
  using namespace aurora::gfx;
  assert(render_thread_pipeline_compilation(true, true));
  assert(!render_thread_pipeline_compilation(false, true));
  assert(!render_thread_pipeline_compilation(true, false));
  assert(!render_thread_pipeline_compilation(false, false));
  for (bool blocking : {false, true}) {
    assert(!compile_pipeline_inline(false, true, blocking, 0, 5));
    assert(!compile_pipeline_inline(true, false, blocking, 0, 5));
  }
  assert(compile_pipeline_inline(false, false, false, 0, 5));
  assert(!compile_pipeline_inline(false, false, false, 5, 5));
  assert(compile_pipeline_inline(false, false, true, 5, 5));
}
