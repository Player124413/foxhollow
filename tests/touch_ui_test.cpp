#include "foxhollow_touch.h"
#include "foxhollow_cutscene.h"
#include "foxhollow_quit.h"
#include "../port/src/foxhollow_touch_model.hpp"
#include "../extern/aurora/lib/input/imgui_input.hpp"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {
std::unordered_map<ImGuiID, ImRect> rectangles;
std::unordered_map<std::string, ImRect> items;
aurora::imgui::TouchPointer pointer;
int currentSlot = 0;
int clears = 0;
bool routed = false;
}
namespace aurora::imgui { void process_event(const SDL_Event&) noexcept {} }
extern "C" {
Uint64 SDL_GetTicksNS(void) { static Uint64 tick = 0; return ++tick; }
int16_t gObjSeqSlotSeqIdTable[0x58]{};
int8_t gObjSeqSlotResults[0x58]{};
int8_t gObjSeqSlotPrevResults[0x58]{};
int getCurSeqNo(void) { return currentSlot; }
int fhMovieCanSkip(void) { return 0; }
void PADClearVirtualStatus(uint32_t port) { assert(port == 0); ++clears; }
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
  rectangles[id] = bb;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
  if (rectangles.contains(id)) items[label] = rectangles.at(id);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return ""; }

namespace {
void frame() {
  for (int pass = 0; pass < 8; ++pass) {
    items.clear();
    rectangles.clear();
    aurora::input::reconcile();
    ImGui::NewFrame();
    fhTouchDraw();
    ImGui::Render();
    if (ImGui::GetCurrentContext()->InputEventsQueue.empty()) return;
  }
  assert(false);
}
ImVec2 center(const char* name) {
  assert(items.contains(name));
  return items.at(name).GetCenter();
}
bool touch(Uint32 type, ImVec2 pos, SDL_FingerID finger = 42, SDL_TouchID device = 1) {
  SDL_Event event{};
  event.type = type;
  event.tfinger.touchID = device;
  event.tfinger.fingerID = finger;
  if (!routed) return pointer.handle(event, pos);
  using namespace aurora::input;
  using Phase = InputEvent::PointerChanged::Phase;
  Phase phase = Phase::Move;
  if (type == SDL_EVENT_FINGER_DOWN) phase = Phase::Down;
  if (type == SDL_EVENT_FINGER_UP) phase = Phase::Up;
  if (type == SDL_EVENT_FINGER_CANCELED) phase = Phase::Cancel;
  InputEvent input;
  input.source = touch_source();
  input.payload = InputEvent::PointerChanged{.pointer = static_cast<uint64_t>(finger), .phase = phase,
                                            .position = {pos.x, pos.y}};
  detail::dispatch(input, &event);
  return true;
}
void click(const char* name) {
  ImVec2 pos = center(name);
  assert(touch(SDL_EVENT_FINGER_DOWN, pos)); frame();
  if (routed) for (int i = 0; i < 5; ++i) frame();
  assert(touch(SDL_EVENT_FINGER_UP, pos)); frame(); frame();
}
void edit() {
  click("Menu"); assert(fhTouchMenuOpen());
  click("Edit controls");
  assert(ImGui::FindWindowByName("Control layout")->LastFrameActive == ImGui::GetFrameCount());
}
void dragA() {
  const auto canvas = items.at("##layout");
  const auto size = canvas.GetSize();
  const auto& a = fh::touch::defaults[2];
  ImVec2 from(canvas.Min.x + a.x * size.x, canvas.Min.y + a.y * size.y);
  ImVec2 to(from.x - size.x * .08f, from.y - size.y * .08f);
  assert(touch(SDL_EVENT_FINGER_DOWN, from)); frame();
  assert(touch(SDL_EVENT_FINGER_MOTION, to)); frame();
  assert(touch(SDL_EVENT_FINGER_UP, to)); frame(); frame();
}
nlohmann::json load(const std::filesystem::path& path) {
  std::ifstream file(path / "touch.json");
  return nlohmann::json::parse(file);
}
}

int main(int argc, char**) {
  ImGui::CreateContext();
  ImGui::GetCurrentContext()->TestEngineHookItems = true;
  auto& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.DisplaySize = ImVec2(960, 540);
  io.DeltaTime = 1.f / 60;
  unsigned char* pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  char directory[] = "/tmp/foxhollow-touch-XXXXXX";
  assert(mkdtemp(directory));
  const std::filesystem::path path(directory);
  setenv("FOXHOLLOW_TOUCH", "1", 1);
  setenv("FOXHOLLOW_TOUCH_EDITOR", "0", 1);
  fhTouchInit(directory);
  frame(); frame();

  ImVec2 pos = center("Menu");
  assert(!touch(SDL_EVENT_FINGER_MOTION, pos));
  assert(touch(SDL_EVENT_FINGER_DOWN, pos)); frame();
  assert(io.MouseDown[0] && io.MouseSource == ImGuiMouseSource_TouchScreen);
  assert(!touch(SDL_EVENT_FINGER_DOWN, ImVec2(10, 10), 43));
  assert(!touch(SDL_EVENT_FINGER_UP, ImVec2(10, 10), 43));
  assert(!touch(SDL_EVENT_FINGER_UP, pos, 42, 2)); frame();
  assert(io.MouseDown[0] && !fhTouchMenuOpen());
  assert(touch(SDL_EVENT_FINGER_CANCELED, pos)); frame(); frame();
  assert(!io.MouseDown[0] && !fhTouchMenuOpen());
  assert(!touch(SDL_EVENT_FINGER_UP, pos));
  assert(touch(SDL_EVENT_FINGER_DOWN, pos)); frame();
  pointer.reset(); frame(); frame();
  assert(!io.MouseDown[0] && !fhTouchMenuOpen());

  assert(touch(SDL_EVENT_FINGER_DOWN, pos));
  pointer.reset(); frame(); frame();
  assert(!io.MouseDown[0] && !fhTouchMenuOpen());

  routed = argc > 1;
  if (routed) {
    using namespace aurora::input;
    detail::reset();
    auto layer = register_layer({.label = "aurora.imgui", .priority = kImGuiLayerPriority,
                                 .onEvent = aurora::imgui::input_layer_event,
                                 .capturesSource = aurora::imgui::input_layer_captures,
                                 .userdata = &pointer});
    assert(layer != kInvalidLayerId);
    pos = center("Menu");
    assert(touch(SDL_EVENT_FINGER_DOWN, pos)); frame(); frame();
    assert(io.MouseDown[0] && !fhTouchMenuOpen());
    detail::focus_lost(); frame(); frame();
    assert(!io.MouseDown[0] && !fhTouchMenuOpen());
    touch(SDL_EVENT_FINGER_UP, pos); frame();
    assert(touch(SDL_EVENT_FINGER_DOWN, ImVec2(30, 200), 70)); frame(); frame();
    assert(io.MouseDown[0] && !io.WantCaptureMouse);
    click("Menu"); assert(fhTouchMenuOpen());
    touch(SDL_EVENT_FINGER_MOTION, ImVec2(50, 200), 70); frame();
    touch(SDL_EVENT_FINGER_UP, ImVec2(50, 200), 70); frame();
    click("Cancel"); assert(!fhTouchMenuOpen());
  }
  for (ImVec2 size : {ImVec2(640, 480), ImVec2(800, 480), ImVec2(2400, 1080), ImVec2(960, 540)}) {
    io.DisplaySize = size; frame(); frame(); frame();
    click("Menu");
    assert(fhTouchMenuOpen());
    for (const char* label : {"Edit controls", "Save controls", "Cancel"}) {
      auto rect = items.at(label);
      assert(rect.Min.x >= 0 && rect.Min.y >= 0 && rect.Max.x <= size.x && rect.Max.y <= size.y);
      assert(rect.GetHeight() >= 40 * std::clamp(std::min(size.x, size.y) / 480.f, .75f, 3.f));
    }
    click("Edit controls");
    for (const char* label : {"Save controls", "Cancel"}) {
      auto rect = items.at(label);
      assert(rect.Min.x >= 0 && rect.Min.y >= 0 && rect.Max.x <= size.x && rect.Max.y <= size.y);
    }
    click("Cancel");
  }
  edit();
  dragA();
  click("Cancel");
  assert(!fhTouchMenuOpen() && !std::filesystem::exists(path / "touch.json"));
  edit();
  click("Save controls");
  auto initial = load(path);
  assert(std::abs(initial["controls"][2][0].get<float>() - fh::touch::defaults[2].x) < .001f);
  edit(); dragA(); click("Save controls");
  assert(!fhTouchMenuOpen());
  auto changed = load(path);
  assert(changed["controls"].size() == 14);
  assert(changed["controls"][2][0].get<float>() < fh::touch::defaults[2].x - .04f);
  assert(!std::filesystem::exists(path / "touch.json.tmp"));
  click("Menu"); click("Reset controls"); click("Cancel");
  assert(load(path) == changed);
  click("Menu"); click("Save controls");
  assert(load(path) == changed);

  currentSlot = 7;
  gObjSeqSlotSeqIdTable[7] = 123;
  frame(); frame();
  click("Skip scene");
  assert(fhSequenceSkipRequested(7, 123));
  assert(!fhTouchMenuOpen() && gObjSeqSlotResults[7] == 2);
  frame(); assert(!items.contains("Skip scene"));
  currentSlot = 0;

  fhTouchInit((path / "missing").c_str());
  click("Menu"); click("Save controls");
  assert(fhTouchMenuOpen());
  click("Cancel"); assert(!fhTouchMenuOpen());

  setenv("FOXHOLLOW_TOUCH", "0", 1);
  fhTouchInit(directory);
  frame(); frame();
  click("Menu"); click("Edit controls");
  assert(fhTouchMenuOpen());
  click("Cancel"); assert(!fhTouchMenuOpen());
  click("Menu"); click("Launcher"); click("Back");
  assert(fhTouchMenuOpen() && !fhQuitRequested());
  click("Launcher"); click("Exit game");
  assert(!fhTouchMenuOpen() && fhQuitRequested());
  assert(clears > 0);
  ImGui::DestroyContext();
  std::filesystem::remove_all(path);
}
