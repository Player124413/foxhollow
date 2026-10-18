#include "foxhollow_touch_model.hpp"
#include "foxhollow_touch.h"
#include "foxhollow_cutscene.h"
#include "foxhollow_quit.h"
#include <aurora/imgui.h>
#include <aurora/event.h>
#include <dolphin/pad.h>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cfloat>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
auto controls = fh::touch::defaults;
bool editing = false;
bool menuOpen = false;
bool quitPrompt = false;
int selected = 0;
std::array<float, 4> hudBounds{};
bool enabled = true;
bool showFps = false;
bool releaseGuard = false;
bool saveFailed = false;
float opacity = .45f, deadzone = .15f;
std::string path;
struct Contact { SDL_TouchID device; SDL_FingerID finger; int control; };
std::array<Contact, 16> contacts;
int contactCount = 0;
std::array<ImVec2, 2> thumbTargets{};
std::array<fh::touch::Thumb, 2> thumbs{};
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
  snapshot();
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
    snapshot();
  } catch (const nlohmann::json::exception&) {}
}

extern "C" void fhTouchUpdate(void) {
  PADStatus status{};
  thumbTargets = {};
  auto* window = SDL_GetKeyboardFocus();
  if (!window || editing || menuOpen || quitPrompt || !enabled) {
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
      if (selected == -1 && hudBounds[2] > hudBounds[0] &&
          finger.x >= hudBounds[0] && finger.x <= hudBounds[2] &&
          finger.y >= hudBounds[1] && finger.y <= hudBounds[3]) selected = -2;
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
        if (!releaseGuard) thumbTargets[selected] = ImVec2((finger.x - c.x) * w / (c.radius * unit),
                                                         (finger.y - c.y) * h / (c.radius * unit));
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

namespace {
void release() {
  releaseGuard = true;
  contactCount = 0;
  PADClearVirtualStatus(0);
}
void cancel() {
  controls = previousControls;
  opacity = previousOpacity;
  deadzone = previousDeadzone;
  menuOpen = editing = false;
  saveFailed = false;
  release();
}
void openMenu() {
  snapshot();
  menuOpen = true;
  saveFailed = false;
  release();
}
void percentage(const char* label, float& value, float low, float high) {
  ImGui::PushID(label);
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(-FLT_MIN);
  float percent = value * 100.f;
  if (ImGui::SliderFloat("##value", &percent, low * 100.f, high * 100.f, "%.0f%%", ImGuiSliderFlags_NoInput))
    value = percent / 100.f;
  ImGui::PopID();
}
void footer(float scale) {
  if (saveFailed) ImGui::TextColored(ImVec4(1, .3f, .3f, 1), "touch.json: I/O error");
  float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.f;
  if (ImGui::Button("Save controls", ImVec2(width, 44 * scale))) {
    saveFailed = path.empty() || !save();
    if (!saveFailed) menuOpen = editing = false;
    release();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(width, 44 * scale))) cancel();
}
void drawControls(ImDrawList* draw, ImVec2 origin, ImVec2 size, bool preview) {
  float unit = std::min(size.x, size.y);
  for (size_t i = 0; i < controls.size(); ++i) {
    const auto& c = controls[i];
    ImVec2 center(origin.x + c.x * size.x, origin.y + c.y * size.y);
    float r = c.radius * unit;
    bool highlight = preview && selected == static_cast<int>(i);
    draw->AddCircleFilled(center, r, ImGui::GetColorU32(ImVec4(highlight ? .15f : .08f, .22f, .35f,
                                                          preview ? .8f : opacity)), 48);
    draw->AddCircle(center, r, highlight ? IM_COL32(90, 195, 255, 255) : IM_COL32(210, 225, 245, 180),
                    48, highlight ? 3.f : 1.f);
    if (i < 2) {
      auto& thumb = thumbs[i];
      auto target = preview ? ImVec2() : thumbTargets[i];
      thumb.follow(target.x, target.y, ImGui::GetIO().DeltaTime);
      ImVec2 knob(center.x + thumb.x * r * .56f, center.y + thumb.y * r * .56f);
      draw->AddCircle(center, r * .58f, IM_COL32(140, 175, 205, 100), 48, 1.f);
      draw->AddCircleFilled(ImVec2(knob.x, knob.y + r * .05f), r * .38f,
                            ImGui::GetColorU32(ImVec4(0, 0, 0, preview ? .5f : opacity)), 48);
      draw->AddCircleFilled(knob, r * .36f,
                            ImGui::GetColorU32(ImVec4(.24f, .48f, .67f, preview ? 1.f : std::min(1.f, opacity + .25f))), 48);
      draw->AddCircle(knob, r * .36f, IM_COL32(195, 230, 255, 230), 48, 2.f);
      center = knob;
    }
    auto text = ImGui::CalcTextSize(c.label);
    draw->AddText(ImVec2(center.x - text.x / 2, center.y - text.y / 2), IM_COL32_WHITE, c.label);
  }
}
constexpr ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoMove;
}

extern "C" int fhTouchMenuOpen(void) { return menuOpen || editing || quitPrompt; }

extern "C" void fhTouchHandleEvent(const SDL_Event* event) {
  if (!event || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat ||
      (event->key.key != SDLK_ESCAPE && event->key.key != SDLK_AC_BACK)) return;
  if (quitPrompt) {
    quitPrompt = false;
    release();
  } else if (editing) {
    editing = false;
    menuOpen = true;
    release();
  } else if (menuOpen) cancel();
  else openMenu();
}

extern "C" void fhTouchDraw(void) {
  auto& io = ImGui::GetIO();
  const ImVec2 size = io.DisplaySize;
  const float unit = std::min(size.x, size.y);
  if (unit <= 0) return;
  const float scale = std::clamp(unit / 480.f, .75f, 3.f);
  static const ImGuiStyle baseStyle = [] {
    ImGuiStyle style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14, 14);
    style.FramePadding = ImVec2(12, 10);
    style.ItemSpacing = ImVec2(10, 10);
    style.WindowRounding = 12;
    style.FrameRounding = 8;
    style.GrabRounding = 8;
    style.GrabMinSize = 24;
    style.ScrollbarSize = 26;
    return style;
  }();
  ImGui::GetStyle() = baseStyle;
  ImGui::GetStyle().ScaleAllSizes(scale);
  io.FontGlobalScale = scale * 1.25f;
  auto* background = ImGui::GetBackgroundDrawList();
  if (fhTouchMenuOpen()) background->AddRectFilled(ImVec2(0, 0), size, IM_COL32(10, 17, 28, 255));
  else if (enabled) drawControls(background, ImVec2(0, 0), size, false);
  if (showFps && !fhTouchMenuOpen()) {
    ImGui::SetNextWindowPos(ImVec2(12 * scale, 12 * scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(.65f);
    ImGui::Begin("FPS", nullptr, panelFlags | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%.1f FPS | %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.f / io.Framerate : 0.f);
    ImGui::End();
  }
  hudBounds = {};
  if (!fhTouchMenuOpen()) {
    ImGui::SetNextWindowPos(ImVec2(size.x * .55f, size.y - 12 * scale), ImGuiCond_Always, ImVec2(.5f, 1));
    ImGui::Begin("Touch toolbar", nullptr, panelFlags | ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::Button("Menu", ImVec2(104 * scale, 44 * scale))) openMenu();
    if (fhCanSkipCutscene()) {
      ImGui::SameLine();
      if (ImGui::Button("Skip scene", ImVec2(148 * scale, 44 * scale))) {
        if (fhSkipCutscene()) release();
      }
    }
    const auto pos = ImGui::GetWindowPos();
    const auto bounds = ImGui::GetWindowSize();
    hudBounds = {pos.x / size.x, pos.y / size.y, (pos.x + bounds.x) / size.x, (pos.y + bounds.y) / size.y};
    ImGui::End();
  }
  if (editing) {
    float panelWidth = std::min(320 * scale, size.x * .44f);
    ImVec2 space(size.x - panelWidth - 28 * scale, size.y - 28 * scale);
    float fit = std::min(space.x / size.x, space.y / size.y);
    ImVec2 canvas(size.x * fit, size.y * fit);
    ImVec2 origin((size.x - panelWidth - canvas.x) / 2, (size.y - canvas.y) / 2);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(size.x - panelWidth, size.y));
    ImGui::Begin("Layout preview", nullptr, panelFlags | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##layout", canvas);
    static bool dragging = false;
    if (ImGui::IsItemClicked()) {
      dragging = false;
      float nearest = FLT_MAX;
      for (size_t i = 0; i < controls.size(); ++i) {
        const auto& c = controls[i];
        float radius = std::max(c.radius * std::min(canvas.x, canvas.y), 22 * scale);
        float distance = std::hypot(io.MousePos.x - origin.x - c.x * canvas.x,
                                    io.MousePos.y - origin.y - c.y * canvas.y) / radius;
        if (distance <= 1.f && distance < nearest) {
          selected = static_cast<int>(i);
          nearest = distance;
          dragging = true;
        }
      }
    }
    if (dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
      auto& c = controls[selected];
      c.x += io.MouseDelta.x / canvas.x;
      c.y += io.MouseDelta.y / canvas.y;
      fh::touch::constrain(c, size.x / size.y);
    }
    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 corner(origin.x + canvas.x, origin.y + canvas.y);
    draw->AddRectFilled(origin, corner, IM_COL32(18, 30, 46, 255), 8 * scale);
    draw->AddRect(origin, corner, IM_COL32(60, 90, 115, 255), 8 * scale);
    drawControls(draw, origin, canvas, true);
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(size.x - panelWidth, 0));
    ImGui::SetNextWindowSize(ImVec2(panelWidth, size.y));
    ImGui::Begin("Control layout", nullptr, panelFlags);
    ImGui::TextUnformatted("Edit controls");
    if (ImGui::BeginChild("Layout fields", ImVec2(0, -(saveFailed ? 90 : 60) * scale))) {
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::BeginCombo("##Control", controls[selected].label)) {
        for (size_t i = 0; i < controls.size(); ++i)
          if (ImGui::Selectable(controls[i].label, selected == static_cast<int>(i), 0, ImVec2(0, 32 * scale)))
            selected = static_cast<int>(i);
        ImGui::EndCombo();
      }
      auto& c = controls[selected];
      percentage("Position X", c.x, 0.f, 1.f);
      percentage("Position Y", c.y, 0.f, 1.f);
      percentage("Size", c.radius, .025f, .20f);
      fh::touch::constrain(c, size.x / size.y);
      if (ImGui::Button("Reset selected", ImVec2(-FLT_MIN, 44 * scale))) c = fh::touch::defaults[selected];
      if (ImGui::Button("Settings", ImVec2(-FLT_MIN, 44 * scale))) { editing = false; menuOpen = true; }
    }
    ImGui::EndChild();
    footer(scale);
    ImGui::End();
  } else if (menuOpen) {
    ImGui::SetNextWindowPos(ImVec2(size.x / 2, size.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(440 * scale, size.x * .94f), std::min(460 * scale, size.y * .94f)));
    ImGui::Begin("Touch settings", nullptr, panelFlags);
    ImGui::TextUnformatted("Controls");
    ImGui::SameLine();
    ImGui::TextDisabled("OpenGL ES");
    if (ImGui::BeginChild("Settings fields", ImVec2(0, -(saveFailed ? 90 : 60) * scale))) {
      if (ImGui::Button("Edit controls", ImVec2(-FLT_MIN, 52 * scale))) {
        menuOpen = false;
        editing = true;
        release();
      }
      percentage("Opacity", opacity, .1f, 1.f);
      percentage("Deadzone", deadzone, 0.f, .5f);
      if (ImGui::Button("Reset controls", ImVec2(-FLT_MIN, 44 * scale))) {
        controls = fh::touch::defaults;
        opacity = .45f;
        deadzone = .15f;
      }
      if (ImGui::Button("Launcher", ImVec2(-FLT_MIN, 44 * scale))) quitPrompt = true;
    }
    ImGui::EndChild();
    footer(scale);
    ImGui::End();
  }
  if (quitPrompt && !ImGui::IsPopupOpen("Return to launcher?")) ImGui::OpenPopup("Return to launcher?");
  ImGui::SetNextWindowPos(ImVec2(size.x / 2, size.y / 2), ImGuiCond_Always, ImVec2(.5f, .5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(360 * scale, size.x * .94f), 0));
  if (ImGui::BeginPopupModal("Return to launcher?", nullptr, panelFlags | ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!quitPrompt) ImGui::CloseCurrentPopup();
    ImGui::TextUnformatted("Return to launcher?");
    float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Exit game", ImVec2(width, 48 * scale))) {
      quitPrompt = false;
      cancel();
      fhRequestQuit();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(width, 48 * scale))) {
      quitPrompt = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

extern "C" int fhTouchConfigure(void) {
  while (fhTouchMenuOpen()) {
    if (fhQuitRequested()) return 0;
    for (const AuroraEvent* event = aurora_update(); event && event->type != AURORA_NONE; ++event) {
      if (event->type == AURORA_EXIT) return 0;
      if (event->type == AURORA_SDL_EVENT) fhTouchHandleEvent(&event->sdl);
    }
    fhTouchUpdate();
    if (aurora_begin_frame()) {
      fhTouchDraw();
      aurora_end_frame();
    }
    SDL_Delay(16);
  }
  return !fhQuitRequested();
}
