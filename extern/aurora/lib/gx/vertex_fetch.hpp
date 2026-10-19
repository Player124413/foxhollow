#pragma once

#include <string>
#include <string_view>

namespace aurora::gx {
inline std::string_view vertex_load_word_source(bool textureFetch) {
  if (textureFetch) {
    return R"(
fn load_word(p: texture_2d<u32>, word_idx: u32) -> u32 {
  let dims = textureDimensions(p);
  let texel_idx = word_idx / 4u;
  if (texel_idx < dims.x * dims.y) {
    let coord = vec2i(i32(texel_idx % dims.x), i32(texel_idx / dims.x));
    return textureLoad(p, coord, 0)[word_idx % 4u];
  }
  return 0u;
}
)";
  }
  return R"(
fn load_word(p: ptr<storage, array<u32>>, word_idx: u32) -> u32 {
  if (word_idx < arrayLength(p)) {
    return p[word_idx];
  }
  return 0u;
}
)";
}

inline void use_texture_vertex_fetch(std::string& source) {
  const auto replace = [&](std::string_view from, std::string_view to) {
    size_t pos = 0;
    while ((pos = source.find(from, pos)) != std::string::npos) {
      source.replace(pos, from.size(), to);
      pos += to.size();
    }
  };
  replace("ptr<storage, array<u32>>", "texture_2d<u32>");
  replace("var<storage, read> vbuf: array<u32>;", "var vbuf: texture_2d<u32>;");
  replace("var<storage, read> abuf: array<u32>;", "var abuf: texture_2d<u32>;");
  replace("&vbuf", "vbuf");
  replace("&abuf", "abuf");
}
}
