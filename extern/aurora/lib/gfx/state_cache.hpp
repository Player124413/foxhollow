#pragma once

namespace aurora::gfx {
template <typename T>
class StateCache {
  T m_value{};
  bool m_valid = false;

public:
  bool update(const T& value) {
    if (m_valid && m_value == value) {
      return false;
    }
    m_value = value;
    m_valid = true;
    return true;
  }

  void invalidate() { m_valid = false; }
};
}
