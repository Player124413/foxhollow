#include "../extern/aurora/lib/gfx/state_cache.hpp"

#include <cassert>
#include <cstdint>

int main() {
  aurora::gfx::StateCache<uintptr_t> bindings;
  assert(bindings.update(1));
  assert(!bindings.update(1));
  assert(bindings.update(2));
  assert(!bindings.update(2));
  bindings.invalidate();
  assert(bindings.update(2));
  assert(bindings.update(0));
  assert(!bindings.update(0));
  bindings.invalidate();
  assert(bindings.update(0));

  unsigned commands = 0;
  for (unsigned pass = 0; pass < 8; ++pass) {
    bindings.invalidate();
    for (unsigned draw = 0; draw < 100; ++draw) {
      commands += bindings.update(42);
    }
  }
  assert(commands == 8);
  bindings.invalidate();
  assert(bindings.update(42));
  assert(bindings.update(99));
  bindings.invalidate();
  assert(bindings.update(42));
}
