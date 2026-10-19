#pragma once

#include "../gfx/linear_texture.hpp"

#include <string>
#include <string_view>

namespace aurora::gx {
inline std::string_view vertex_load_word_source(bool textureFetch) {
  static_assert(gfx::LinearTextureWidth == 256);
  if (textureFetch) {
    return R"(
fn load_texel(p: texture_2d<u32>, texel_idx: u32) -> vec4u {
  if (texel_idx < (textureDimensions(p).y << 8u)) {
    let coord = vec2i(i32(texel_idx & 255u), i32(texel_idx >> 8u));
    return textureLoad(p, coord, 0);
  }
  return vec4u(0u);
}

fn load_word(p: texture_2d<u32>, word_idx: u32) -> u32 {
  return load_texel(p, word_idx >> 2u)[word_idx & 3u];
}

fn load_words2(p: texture_2d<u32>, word_idx: u32) -> vec2u {
  let texel_idx = word_idx >> 2u;
  let a = load_texel(p, texel_idx);
  let lane = word_idx & 3u;
  switch lane {
    case 0u: { return a.xy; }
    case 1u: { return a.yz; }
    case 2u: { return a.zw; }
    default: { return vec2u(a.w, load_texel(p, texel_idx + 1u).x); }
  }
}

fn load_words3(p: texture_2d<u32>, word_idx: u32) -> vec3u {
  let texel_idx = word_idx >> 2u;
  let a = load_texel(p, texel_idx);
  let lane = word_idx & 3u;
  if (lane == 0u) { return a.xyz; }
  if (lane == 1u) { return a.yzw; }
  let b = load_texel(p, texel_idx + 1u);
  if (lane == 2u) { return vec3u(a.zw, b.x); }
  return vec3u(a.w, b.xy);
}

fn load_words4(p: texture_2d<u32>, word_idx: u32) -> vec4u {
  let texel_idx = word_idx >> 2u;
  let a = load_texel(p, texel_idx);
  let lane = word_idx & 3u;
  if (lane == 0u) {
    return a;
  }
  let b = load_texel(p, texel_idx + 1u);
  switch lane {
    case 1u: { return vec4u(a.yzw, b.x); }
    case 2u: { return vec4u(a.zw, b.xy); }
    default: { return vec4u(a.w, b.xyz); }
  }
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

inline std::string_view vertex_float_fast_path(bool textureFetch, unsigned components) {
  if (!textureFetch) {
    return {};
  }
  if (components == 2) {
    return R"(
  if ((byte_off & 3u) == 0u) {
    let words = load_words2(p, byte_off >> 2u);
    return bitcast<vec2f>(vec2u(bswap32(words.x, le), bswap32(words.y, le)));
  }
)";
  }
  if (components == 3) {
    return R"(
  if ((byte_off & 3u) == 0u) {
    let words = load_words3(p, byte_off >> 2u);
    return bitcast<vec3f>(vec3u(bswap32(words.x, le), bswap32(words.y, le), bswap32(words.z, le)));
  }
)";
  }
  return R"(
  if ((byte_off & 3u) == 0u) {
    let words = load_words4(p, byte_off >> 2u);
    return bitcast<vec4f>(vec4u(bswap32(words.x, le), bswap32(words.y, le), bswap32(words.z, le), bswap32(words.w, le)));
  }
)";
}

inline std::string_view vertex_short_pair_source(bool textureFetch) {
  if (textureFetch) {
    return R"(
  let packed = load_u32_raw(p, byte_off);
  return vec2u(bswap16(packed & 65535u, le), bswap16(packed >> 16u, le));
)";
  }
  return R"(
  return vec2u(load_u16(p, byte_off, le), load_u16(p, byte_off + 2u, le));
)";
}

inline std::string_view vertex_short_fast_path(bool textureFetch, unsigned components) {
  if (!textureFetch || components != 3) return {};
  return R"(
  if ((byte_off & 1u) == 0u) {
    let words = load_words2(p, byte_off >> 2u);
    if ((byte_off & 2u) == 0u) {
      return vec3u(bswap16(words.x & 65535u, le), bswap16(words.x >> 16u, le),
                   bswap16(words.y & 65535u, le));
    }
    return vec3u(bswap16(words.x >> 16u, le), bswap16(words.y & 65535u, le),
                 bswap16(words.y >> 16u, le));
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
