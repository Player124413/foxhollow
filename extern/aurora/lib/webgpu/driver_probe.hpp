#pragma once

#include <string>
#include <array>
#include <cstdint>

namespace aurora::webgpu {
inline std::string driver_probe_source(unsigned mode) {
  std::string source = R"(
struct Params { a: vec4u, b: vec4u, c: vec4u, d: vec4u };
)";
  if (mode == 1) source += "var<immediate> params: Params;\n";
  else if (mode == 2) source += "@group(0) @binding(0) var<uniform> params: Params;\n";
  else source += R"(
const params = Params(vec4u(4093u,9u,32768u,0u),vec4u(64u,128u,256u,512u),
                      vec4u(1024u,2048u,4096u,8192u),vec4u(16384u,65536u,131072u,262144u));
)";
  source += R"(
struct VertexOutput { @builtin(position) pos: vec4f, @location(0) color: vec4f };
@vertex fn vs_main(@builtin(vertex_index) id: u32) -> VertexOutput {
  let positions = array<vec2f,3>(vec2f(-1.0,-1.0),vec2f(-1.0,3.0),vec2f(3.0,-1.0));
  let valid = all(params.a == vec4u(4093u,9u,32768u,0u)) &&
              all(params.b == vec4u(64u,128u,256u,512u)) &&
              all(params.c == vec4u(1024u,2048u,4096u,8192u)) &&
              all(params.d == vec4u(16384u,65536u,131072u,262144u));
  var out: VertexOutput;
  out.pos = vec4f(positions[id],0.5,1.0);
  out.color = select(vec4f(1.0,0.0,0.0,1.0),vec4f(64.0,128.0,192.0,255.0)/255.0,valid);
  return out;
}
@fragment fn fs_main(in: VertexOutput) -> @location(0) vec4f {
  let valid = all(params.a == vec4u(4093u,9u,32768u,0u)) &&
              all(params.b == vec4u(64u,128u,256u,512u)) &&
              all(params.c == vec4u(1024u,2048u,4096u,8192u)) &&
              all(params.d == vec4u(16384u,65536u,131072u,262144u));
  return select(vec4f(1.0,0.0,0.0,1.0),in.color,valid);
}
)";
  return source;
}

constexpr bool driver_probe_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return r >= 62 && r <= 66 && g >= 126 && g <= 130 && b >= 190 && b <= 194 && a >= 253;
}

struct DriverProbeResult {
  bool solid = false;
  bool immediate = false;
  bool uniform = false;
  bool depth32 = false;
  bool depth24 = false;
  bool indexed = false;
  bool completed = false;
  std::array<std::array<uint8_t,4>,6> pixels{};
};
DriverProbeResult run_driver_probe();
}
