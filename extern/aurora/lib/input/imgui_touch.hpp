#pragma once

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <cfloat>

namespace aurora::imgui {
class TouchPointer {
  SDL_TouchID m_device = 0;
  SDL_FingerID m_finger = 0;
  bool m_active = false;

public:
  void reset() noexcept {
    m_active = false;
    ImGui::GetIO().ClearInputMouse();
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
  }

  bool handle(const SDL_Event& event, ImVec2 position) noexcept {
    if (event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
        event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED) return false;
    const auto& finger = event.tfinger;
    if (event.type == SDL_EVENT_FINGER_DOWN && !m_active) {
      m_device = finger.touchID;
      m_finger = finger.fingerID;
      m_active = true;
    }
    if (!m_active || m_device != finger.touchID || m_finger != finger.fingerID) return false;
    auto& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    if (event.type == SDL_EVENT_FINGER_CANCELED) {
      reset();
      return true;
    }
    io.AddMousePosEvent(position.x, position.y);
    if (event.type == SDL_EVENT_FINGER_DOWN) io.AddMouseButtonEvent(0, true);
    if (event.type == SDL_EVENT_FINGER_UP) {
      io.AddMouseButtonEvent(0, false);
      m_active = false;
    }
    return true;
  }
};
}
