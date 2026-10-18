#include "../port/src/foxhollow_touch_model.hpp"
#include <cassert>
#include <limits>
int main() {
  using namespace fh::touch;
  for (auto c : defaults) assert(valid(c.x, c.y, c.radius));
  assert(!valid(-1, 0, .1f));
  assert(!valid(0, 0, std::numeric_limits<float>::quiet_NaN()));
  assert(!valid(0, 0, .001f));
  assert((stick(0, 0, .15f) == std::array<int8_t, 2>{0, 0}));
  assert((stick(.1f, 0, .15f) == std::array<int8_t, 2>{0, 0}));
  assert((stick(1, 0, .15f) == std::array<int8_t, 2>{100, 0}));
  assert((stick(0, 1, .15f) == std::array<int8_t, 2>{0, -100}));
  assert((stick(-2, 0, .15f) == std::array<int8_t, 2>{-100, 0}));
  assert((stick(0, -2, .15f) == std::array<int8_t, 2>{0, 100}));
  auto diagonal = stick(1, 1, .15f);
  assert(diagonal[0] == 71 && diagonal[1] == -71);
  assert((stick(std::numeric_limits<float>::infinity(), 0, .15f) == std::array<int8_t, 2>{0, 0}));
  for (int x = -200; x <= 200; ++x)
    for (int y = -200; y <= 200; ++y) {
      auto a = stick(x / 100.f, y / 100.f, .15f);
      assert(std::hypot(a[0], a[1]) < 101);
    }
}
