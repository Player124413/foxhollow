#include "../port/src/foxhollow_touch_model.hpp"
#include <cassert>
#include <limits>
int main() {
  for (float aspect : {.5f, 1.f, 4.f / 3, 16.f / 9, 20.f / 9}) {
    auto c = fh::touch::defaults[0];
    c.x = -1.f; c.y = 2.f;
    fh::touch::constrain(c, aspect);
    assert(fh::touch::valid(c.x, c.y, c.radius));
    assert(c.x >= c.radius * std::min(1.f, 1.f / aspect));
    assert(c.y <= 1.f - c.radius * std::min(1.f, aspect));
  }

  using namespace fh::touch;
  Thumb thumb;
  for (int i = 0; i < 60; ++i) thumb.follow(3, 4, 1.f / 60);
  assert(std::hypot(thumb.x, thumb.y) <= 1.00001f);
  assert(thumb.x > .59f && thumb.y > .79f);
  for (int i = 0; i < 60; ++i) thumb.follow(0, 0, 1.f / 60);
  assert(std::hypot(thumb.x, thumb.y) < .001f);
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
