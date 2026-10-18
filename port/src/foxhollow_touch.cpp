#include "foxhollow_touch_model.hpp"
#include <aurora/imgui.h>
#include <aurora/event.h>
#include <dolphin/pad.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
auto controls = fh::touch::defaults;
bool editing = false;
bool enabled = true;
bool showFps = false;
bool releaseGuard = false;
bool saveFailed = false;
float opacity = .45f, deadzone = .15f;
std::string path;
struct Contact { SDL_TouchID device; SDL_FingerID finger; int control; };
std::array<Contact, 16> contacts;
int contactCount = 0;
auto previousControls = controls;
float previousOpacity = opacity, previousDeadzone = deadzone;
void snapshot() {
  previousControls = controls;
  previousOpacity = opacity;
  previousDeadzone = deadzone;
}

bool save() {
  nlohmann::json data;
  data["opacity"] = opacity;
  data["deadzone"] = deadzone;
  data["controls"] = nlohmann::json::array();
  for (auto& c : controls) data["controls"].push_back({c.x, c.y, c.radius});
  std::ofstream file(path + ".tmp");
  file << data.dump();
  file.close();
  return file && std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
}
}

extern "C" void fhTouchInit(const char* userPath) {
  const char* touch = std::getenv("FOXHOLLOW_TOUCH");
  enabled = !touch || std::string(touch) != "0";
  const char* editor = std::getenv("FOXHOLLOW_TOUCH_EDITOR");
  editing = editor && std::string(editor) == "1";
  const char* fps = std::getenv("FOXHOLLOW_SHOW_FPS");
  showFps = fps && std::string(fps) == "1";
  if (!userPath) return;
  path = std::string(userPath) + "/touch.json";
  try {
    std::ifstream file(path);
    if (!file) return;
    auto data = nlohmann::json::parse(file);
    auto loaded = controls;
    auto& items = data.at("controls");
    if (items.size() != controls.size()) return;
    for (size_t i = 0; i < loaded.size(); ++i) {
      float x = items.at(i).at(0).get<float>();
      float y = items.at(i).at(1).get<float>();
      float r = items.at(i).at(2).get<float>();
      if (!fh::touch::valid(x, y, r)) return;
      loaded[i].x = x; loaded[i].y = y; loaded[i].radius = r;
    }
    float a = data.at("opacity").get<float>();
    float d = data.at("deadzone").get<float>();
    if (!std::isfinite(a) || !std::isfinite(d)) return;
    controls = loaded;
    opacity = std::clamp(a, .1f, 1.f);
    deadzone = std::clamp(d, 0.f, .5f);
  } catch (const nlohmann::json::exception&) {}
}

extern "C" void fhTouchUpdate(void) {
  PADStatus status{};
  auto* window = SDL_GetKeyboardFocus();
  if (!window || editing || !enabled) {
    contactCount = 0;
    releaseGuard = true;
    PADClearVirtualStatus(0);
    return;
  }
  int w, h;
  SDL_GetWindowSize(window, &w, &h);
  if (w <= 0 || h <= 0) { PADClearVirtualStatus(0); return; }
  float unit = static_cast<float>(std::min(w, h));
  auto old = contacts;
  int oldCount = contactCount;
  contactCount = 0;
  int deviceCount = 0;
  auto* devices = SDL_GetTouchDevices(&deviceCount);
  for (int d = 0; d < deviceCount; ++d) {
    int count = 0;
    auto** fingers = SDL_GetTouchFingers(devices[d], &count);
    for (int f = 0; f < count && contactCount < static_cast<int>(contacts.size()); ++f) {
      auto& finger = *fingers[f];
      int selected = -1;
      for (int i = 0; i < oldCount; ++i)
        if (old[i].device == devices[d] && old[i].finger == finger.id) selected = old[i].control;
      if (selected == -1) {
        float nearest = 1.f;
        for (size_t i = 0; i < controls.size(); ++i) {
          if (i < 2) {
            bool occupied = false;
            for (int j = 0; j < contactCount; ++j)
              occupied |= contacts[j].control == static_cast<int>(i);
            for (int j = 0; j < oldCount; ++j)
              occupied |= old[j].control == static_cast<int>(i) &&
                          (old[j].device != devices[d] || old[j].finger != finger.id);
            if (occupied) continue;
          }
          auto& c = controls[i];
          float distance = std::hypot((finger.x - c.x) * w, (finger.y - c.y) * h) / (c.radius * unit);
          if (distance < nearest) { nearest = distance; selected = static_cast<int>(i); }
        }
        if (selected < 0) selected = -2;
      }
      contacts[contactCount++] = {devices[d], finger.id, selected};
      if (selected < 0) continue;
      auto& c = controls[selected];
      if (selected < 2) {
        auto axis = fh::touch::stick((finger.x - c.x) * w / (c.radius * unit),
                                    (finger.y - c.y) * h / (c.radius * unit), deadzone);
        if (selected == 0) { status.stickX = axis[0]; status.stickY = axis[1]; }
        else { status.substickX = axis[0]; status.substickY = axis[1]; }
      } else if (std::hypot((finger.x - c.x) * w, (finger.y - c.y) * h) <= c.radius * unit * 1.4f) {
        status.button |= c.button;
      }
    }
    SDL_free(fingers);
  }
  SDL_free(devices);
  status.triggerLeft = (status.button & PAD_TRIGGER_L) ? 255 : 0;
  status.triggerRight = (status.button & PAD_TRIGGER_R) ? 255 : 0;
  status.analogA = (status.button & PAD_BUTTON_A) ? 255 : 0;
  status.analogB = (status.button & PAD_BUTTON_B) ? 255 : 0;
  if (releaseGuard) {
    if (!contactCount) releaseGuard = false;
    PADClearVirtualStatus(0);
    return;
  }
  if (contactCount) PADSetVirtualStatus(0, &status);
  else PADClearVirtualStatus(0);
}

extern "C" void fhTouchDraw(void) {
  auto& io = ImGui::GetIO();
  auto size = io.DisplaySize;
  if (showFps) {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.65f);
    ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%.1f FPS | %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.f / io.Framerate : 0.f);
    ImGui::End();
  }
  if (!enabled) return;
  float unit = std::min(size.x, size.y);
  if (unit <= 0) return;
  static bool uiInitialized = false;
  if (!uiInitialized) {
    float scale = std::clamp(unit / 480.f, 1.f, 3.f);
    io.FontGlobalScale = scale;
    ImGui::GetStyle().ScaleAllSizes(scale);
    uiInitialized = true;
  }
  auto* draw = ImGui::GetBackgroundDrawList();
  for (size_t i = 0; i < controls.size(); ++i) {
    auto& c = controls[i];
    ImVec2 center(c.x * size.x, c.y * size.y);
    float r = c.radius * unit;
    draw->AddCircleFilled(center, r, ImGui::GetColorU32(ImVec4(.12f, .15f, .2f, opacity)), 32);
    draw->AddCircle(center, r, IM_COL32(230, 235, 255, 180), 32);
    auto text = ImGui::CalcTextSize(c.label);
    draw->AddText(ImVec2(center.x - text.x / 2, center.y - text.y / 2), IM_COL32_WHITE, c.label);
    if (editing) {
      ImGui::SetNextWindowPos(ImVec2(center.x - r, center.y - r));
      ImGui::SetNextWindowSize(ImVec2(r * 2, r * 2));
      ImGui::PushID(static_cast<int>(i));
      ImGui::Begin(c.label, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
      ImGui::InvisibleButton("##drag", ImVec2(r * 2 - 16, r * 2 - 16));
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        c.x = std::clamp(c.x + io.MouseDelta.x / size.x, r / size.x, 1 - r / size.x);
        c.y = std::clamp(c.y + io.MouseDelta.y / size.y, r / size.y, 1 - r / size.y);
      }
      ImGui::End();
      ImGui::PopID();
    }
  }
  ImGui::SetNextWindowPos(ImVec2(size.x / 2, size.y - 12), ImGuiCond_Always, ImVec2(.5f, 1.f));
  ImGui::Begin("Touch", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
               ImGuiWindowFlags_NoSavedSettings);
  if (ImGui::Button(editing ? "Save controls" : "Edit controls")) {
    if (!editing) snapshot();
    editing = !editing;
    releaseGuard = true;
    contactCount = 0;
    PADClearVirtualStatus(0);
    if (!editing) {
      saveFailed = path.empty() || !save();
      if (saveFailed) editing = true;
    }
  }
  static bool options = true;
  if (editing) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      controls = previousControls;
      opacity = previousOpacity;
      deadzone = previousDeadzone;
      editing = false;
      saveFailed = false;
      releaseGuard = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) options = !options;
  }
  if (saveFailed) ImGui::TextColored(ImVec4(1, .3f, .3f, 1), "touch.json: I/O error");
  if (editing && options) {
    ImGui::SliderFloat("Opacity", &opacity, .1f, 1.f);
    ImGui::SliderFloat("Deadzone", &deadzone, 0.f, .5f);
    static int selected = 0;
    if (ImGui::BeginCombo("Control", controls[selected].label)) {
      for (size_t i = 0; i < controls.size(); ++i)
        if (ImGui::Selectable(controls[i].label, selected == static_cast<int>(i))) selected = static_cast<int>(i);
      ImGui::EndCombo();
    }
    ImGui::SliderFloat("Size", &controls[selected].radius, .025f, .20f);
    if (ImGui::Button("Reset")) { controls = fh::touch::defaults; opacity = .45f; deadzone = .15f; }
  }
  ImGui::End();
}

extern "C" int fhTouchConfigure(void) {
  snapshot();
  while (editing && enabled) {
    for (const AuroraEvent* event = aurora_update(); event && event->type != AURORA_NONE; ++event) {
      if (event->type == AURORA_EXIT) return 0;
    }
    if (aurora_begin_frame()) {
      fhTouchDraw();
      aurora_end_frame();
    }
    SDL_Delay(16);
  }
  return 1;
}
