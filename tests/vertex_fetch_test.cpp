#include "../extern/aurora/lib/gfx/linear_texture.hpp"
#include "../extern/aurora/lib/gx/vertex_fetch.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace aurora::gfx;

static void replace_all(std::string& text, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

static void check_data(uint32_t size) {
  std::vector<uint8_t> staging(size);
  std::vector<uint8_t> texture(size);
  for (uint32_t i = 0; i < size; ++i) {
    staging[i] = static_cast<uint8_t>((i * 73u) ^ (i >> 5u) ^ (i >> 13u));
  }
  uint32_t copied = 0;
  for (uint32_t highWater : {1u, 4u, 15u, 16u, 17u, 4095u, 4096u, 4097u, 8192u, size - 1u, size}) {
    const auto range = linear_texture_copy_range(copied, highWater);
    assert(range.offset == range.y * LinearTextureBytesPerRow);
    assert(range.offset % 256 == 0);
    assert(range.offset <= copied);
    const auto bytes = range.rows * LinearTextureBytesPerRow;
    assert(range.offset + bytes >= highWater);
    assert(range.offset + bytes <= size);
    std::memcpy(texture.data() + range.offset, staging.data() + range.offset, bytes);
    assert(std::equal(staging.begin(), staging.begin() + highWater, texture.begin()));
    copied = highWater;
    assert(linear_texture_copy_range(copied, copied).rows == 0);
    assert(linear_texture_copy_range(copied, 0).rows == 0);
  }
  for (uint32_t word = 0; word < size / 4; ++word) {
    const uint32_t texel = word / 4;
    const uint32_t x = texel & 255u;
    const uint32_t y = texel >> 8u;
    const uint32_t component = word & 3u;
    assert(x == texel % LinearTextureWidth);
    assert(y == texel / LinearTextureWidth);
    const uint32_t byte = (y * LinearTextureWidth + x) * LinearTextureBytesPerTexel + component * 4;
    assert(byte == word * 4);
    assert(std::memcmp(texture.data() + byte, staging.data() + word * 4, 4) == 0);
    std::array<uint32_t, 4> a{}, b{}, packed{};
    const uint32_t base = (word >> 2u) * 16u;
    std::memcpy(a.data(), texture.data() + base, 16);
    if (base + 16 < size) {
      std::memcpy(b.data(), texture.data() + base + 16, 16);
    }
    switch (component) {
    case 0: packed = a; break;
    case 1: packed = {a[1], a[2], a[3], b[0]}; break;
    case 2: packed = {a[2], a[3], b[0], b[1]}; break;
    default: packed = {a[3], b[0], b[1], b[2]}; break;
    }
    std::array<uint32_t, 2> pair{};
    std::array<uint32_t, 3> triple{};
    switch (component) {
    case 0: pair = {a[0], a[1]}; triple = {a[0], a[1], a[2]}; break;
    case 1: pair = {a[1], a[2]}; triple = {a[1], a[2], a[3]}; break;
    case 2: pair = {a[2], a[3]}; triple = {a[2], a[3], b[0]}; break;
    default: pair = {a[3], b[0]}; triple = {a[3], b[0], b[1]}; break;
    }
    assert(std::equal(pair.begin(), pair.end(), packed.begin()));
    assert(std::equal(triple.begin(), triple.end(), packed.begin()));
    for (uint32_t sub : {0u, 2u}) {
      for (uint32_t c = 0; c < 3; ++c) {
        const uint32_t shift = sub + c * 2;
        const uint32_t actual = (pair[shift / 4] >> ((shift % 4) * 8)) & 65535u;
        uint16_t expected = 0;
        if ((word * 4 + shift + 2) <= size) {
          std::memcpy(&expected, staging.data() + word * 4 + shift, 2);
        }
        assert(actual == expected);
      }
    }
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t expected = 0;
      if (word + i < size / 4) {
        std::memcpy(&expected, staging.data() + (word + i) * 4, 4);
      }
      assert(packed[i] == expected);
    }
  }
}

int main(int argc, char** argv) {
  assert(argc == 3);
  assert(needs_texture_vertex_fetch(0, 0));
  assert(needs_texture_vertex_fetch(0, 2));
  assert(needs_texture_vertex_fetch(1, 2));
  assert(needs_texture_vertex_fetch(2, 0));
  assert(!needs_texture_vertex_fetch(2, 1));
  assert(!needs_texture_vertex_fetch(16, 16));
  assert(linear_texture_copy_range(0, 0).rows == 0);
  check_data(5242880);
  check_data(8388608);

  std::ifstream input(argv[1]);
  assert(input.is_open());
  const std::string shaderFile{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  const auto start = shaderFile.find("fn bswap32(");
  assert(start != std::string::npos);
  const auto end = shaderFile.find("\n{8}\n", start);
  assert(end != std::string::npos);
  auto shader = shaderFile.substr(start, end - start);
  replace_all(shader, "{{", "{");
  replace_all(shader, "}}", "}");
  replace_all(shader, "{12}", std::string(aurora::gx::vertex_load_word_source(true)));
  replace_all(shader, "{13}", std::string(aurora::gx::vertex_float_fast_path(true, 3)));
  replace_all(shader, "{14}", std::string(aurora::gx::vertex_float_fast_path(true, 4)));
  replace_all(shader, "{15}", std::string(aurora::gx::vertex_float_fast_path(true, 2)));
  replace_all(shader, "{16}", std::string(aurora::gx::vertex_short_pair_source(true)));
  replace_all(shader, "{17}", std::string(aurora::gx::vertex_short_fast_path(true, 3)));
  const auto pairStart = shader.find("fn raw_fetch_u16_2(");
  const auto pairEnd = shader.find("fn raw_fetch_u16_3(", pairStart);
  const auto pairBody = shader.substr(pairStart, pairEnd - pairStart);
  const auto firstReturn = pairBody.find("return ");
  assert(firstReturn != std::string::npos);
  assert(pairBody.find("return ", firstReturn + 1) == std::string::npos);
  shader += R"(
@group(0) @binding(0) var<storage, read> vbuf: array<u32>;
@group(0) @binding(1) var<storage, read> abuf: array<u32>;
@vertex fn vs_main(@builtin(vertex_index) vidx: u32) -> @builtin(position) vec4f {
  let idx = raw_fetch_u16_1(&vbuf, vidx * 2u, false);
  let pos = fetch_f32_3(&abuf, idx * 12u, false);
  let color = fetch_rgb565(&vbuf, vidx * 2u, false);
  let nrm = fetch_s16_3(&abuf, idx * 6u, 14u, false);
  let uv = raw_fetch_f32_2(&abuf, idx * 8u, false);
  let shorts = raw_fetch_u16_2(&abuf, idx * 4u, false);
  return vec4f(pos + color.xyz + nrm + vec3f(uv, f32(shorts.x)), 1.0);
}
@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  return vec4f(bitcast<f32>(load_word(&abuf, u32(pos.x))));
}
)";
  aurora::gx::use_texture_vertex_fetch(shader);
  assert(shader.find("ptr<storage") == std::string::npos);
  assert(shader.find("var<storage") == std::string::npos);
  assert(shader.find("&vbuf") == std::string::npos);
  assert(shader.find("&abuf") == std::string::npos);
  assert(shader.find("arrayLength") == std::string::npos);
  assert(shader.find("{12}") == std::string::npos);
  assert(shader.find("fn fetch_rgba8(p: texture_2d<u32>") != std::string::npos);
  assert(shader.find("load_texel(p, word_idx >> 2u)[word_idx & 3u]") != std::string::npos);
  assert(shader.find("texel_idx % dims.x") == std::string::npos);
  assert(shader.find("texel_idx / dims.x") == std::string::npos);
  assert(aurora::gx::vertex_float_fast_path(false, 2).empty());
  assert(aurora::gx::vertex_short_pair_source(false).find("load_u16") != std::string_view::npos);
  assert(aurora::gx::vertex_short_fast_path(false, 3).empty());
  assert(aurora::gx::vertex_float_fast_path(false, 3).empty());
  assert(aurora::gx::vertex_float_fast_path(false, 4).empty());
  const std::string normal(aurora::gx::vertex_load_word_source(false));
  assert(normal.find("arrayLength(p)") != std::string::npos);
  assert(normal.find("ptr<storage") != std::string::npos);
  assert(normal.find("textureLoad") == std::string::npos);
  std::ofstream output(argv[2]);
  output << shader;
  assert(output.good());
}
