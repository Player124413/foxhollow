#pragma once

#include "imgui_touch.hpp"
#include "router.hpp"
#include "../imgui.hpp"

namespace aurora::imgui {
inline input::EventResult input_layer_event(const input::InputEvent& event, void* userdata) {
  using Kind = input::InputSource::Kind;
  const Kind kind = event.source.kind;
  if (ImGui::GetCurrentContext() == nullptr || (kind != Kind::Keyboard && kind != Kind::Mouse && kind != Kind::Touch)) {
    return input::EventResult::Pass;
  }
  ImGuiIO& io = ImGui::GetIO();
  if (event.payload.is<input::InputEvent::Cancelled>()) {
    if (kind == Kind::Keyboard) {
      io.ClearInputKeys();
    } else if (kind == Kind::Touch) {
      static_cast<TouchPointer*>(userdata)->reset();
    } else {
      io.ClearInputMouse();
    }
    return input::EventResult::Pass;
  }
  if (const SDL_Event* raw = input::detail::current_sdl_event()) {
    if (kind == Kind::Touch) {
      if (const auto* pointer = event.payload.get_if<input::InputEvent::PointerChanged>()) {
        static_cast<TouchPointer*>(userdata)->handle(*raw, ImVec2(pointer->position.x, pointer->position.y));
      }
    } else {
      process_event(*raw);
    }
  }
  const bool capture = kind == Kind::Keyboard ? io.WantCaptureKeyboard || io.WantTextInput : io.WantCaptureMouse;
  return capture ? input::EventResult::Consume : input::EventResult::Pass;
}

inline bool input_layer_captures(const input::InputSource& source, void*) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return false;
  }
  const ImGuiIO& io = ImGui::GetIO();
  switch (source.kind) {
  case input::InputSource::Kind::Keyboard:
    return io.WantCaptureKeyboard || io.WantTextInput;
  case input::InputSource::Kind::Mouse:
    return io.WantCaptureMouse;
  default:
    return false;
  }
}
}
