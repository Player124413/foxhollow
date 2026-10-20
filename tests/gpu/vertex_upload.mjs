const { create, globals } = await import(
  process.env.FOXHOLLOW_WEBGPU_MODULE || "webgpu"
);
import fs from "node:fs";
import assert from "node:assert/strict";
Object.assign(globalThis, globals);
const gpu = create([
  "backend=" + (process.argv[3] || "opengles"),
  "enable-dawn-features=allow_unsafe_apis,disable_symbol_renaming,gl_allow_context_on_multi_threads",
]);
const a = await gpu.requestAdapter({ featureLevel: "compatibility" });
if (!a) throw new Error("adapter");
console.log(
  "adapter",
  a.info.vendor,
  a.info.architecture,
  a.info.device,
  a.info.description,
  "immediates",
  a.limits.maxImmediateSize,
);
const textureFetch =
  (process.argv[4] || process.env.FOXHOLLOW_VERTEX_PATH || "texture") !==
  "storage";
const uniformParameters = process.env.FOXHOLLOW_DRAW_PARAMETERS === "uniform";
const d = await a.requestDevice({
  requiredLimits: {
    maxImmediateSize: 64,
    maxStorageBuffersInVertexStage: textureFetch ? 0 : 2,
    maxStorageBuffersInFragmentStage: textureFetch ? 0 : 1,
  },
});
let error = null;
d.addEventListener("uncapturederror", (e) => {
  error = e.error;
  console.error(error);
});
const helpers = fs
  .readFileSync(
    process.argv[2] || "build-input-tests/vertex-fetch.wgsl",
    "utf8",
  )
  .split("@group(0)")[0];
const stagingLayout = fs.readFileSync(
  new URL("../../extern/aurora/lib/gfx/staging_layout.hpp", import.meta.url),
  "utf8",
);
const bufferSize = (name) =>
  Number(stagingLayout.split(name + " = ")[1].split(";")[0]);
const uniformStage = bufferSize("VertexBufferSize");
const indexStage = uniformStage + bufferSize("UniformBufferSize");
const storageStage = indexStage + bufferSize("IndexBufferSize");
const stageSize = storageStage + bufferSize("StorageBufferSize");
const run = async (format, base, indexed) => {
  const arrayBase = 8190 + (base & 15);
  const positionSize = format === "f32" ? 12 : 6;
  const positionSource = indexed ? "abuf" : "vbuf";
  const positionOffset = indexed
    ? "imm.array_start0.x+raw_fetch_u16_1(vbuf,imm.vtx_start+vidx*10u,false)*" +
      positionSize +
      "u"
    : "imm.vtx_start+vidx*" + (positionSize + 8) + "u";
  const colorOffset =
    "imm.vtx_start+vidx*" +
    (indexed ? 10 : positionSize + 8) +
    "u+" +
    (indexed ? 2 : positionSize) +
    "u";
  const shaderDescriptor = {
    code:
      helpers +
      `
struct Immediate {vtx_start:u32,current_pnmtx:u32,fog_range_base:u32,_pad:u32,array_start0:vec4u,array_start1:vec4u,array_start2:vec4u};
var<immediate> imm:Immediate;
struct Uniform {render_viewport_size:vec2f,logical_viewport_size:vec2f,proj:mat4x4f,postex_mtx:array<mat3x4f,20>};
@group(0) @binding(0) var vbuf:texture_2d<u32>;
@group(0) @binding(1) var abuf:texture_2d<u32>;
@group(1) @binding(0) var<uniform> ubuf:Uniform;
struct VertexOutput {@builtin(position) pos:vec4f,@location(0) color:vec4f};
@vertex fn vs_main(@builtin(vertex_index) vidx:u32)->VertexOutput {
 var out:VertexOutput;
 let pos=${format === "f32" ? "raw_fetch_f32_3" : "fetch_s16_3"}(${positionSource},${positionOffset},${format === "f32" ? "" : "14u,"}false);
 let mv_pos=vec4f(pos,1.0)*ubuf.postex_mtx[imm.current_pnmtx];
 out.pos=vec4f(mv_pos,1.0)*ubuf.proj;
 let shorts=raw_fetch_u16_2(vbuf,${colorOffset}+4u,false);
 out.color=select(vec4f(1.0,0.0,0.0,1.0),fetch_rgba8(vbuf,${colorOffset},false),all(shorts==vec2u(4660u,43981u)));
 return out;
}
@fragment fn fs_main(in:VertexOutput)->@location(0) vec4f{return select(vec4f(1.0,0.0,0.0,1.0),in.color,imm.current_pnmtx==9u && imm.array_start0.x==${arrayBase}u);}
`,
  };
  if (!textureFetch) {
    shaderDescriptor.code = shaderDescriptor.code
      .replaceAll(
        "var vbuf:texture_2d<u32>",
        "var<storage,read> vbuf:array<u32>",
      )
      .replaceAll(
        "var abuf:texture_2d<u32>",
        "var<storage,read> abuf:array<u32>",
      )
      .replaceAll("vbuf,", "&vbuf,")
      .replaceAll("abuf,", "&abuf,");
  }
  if (uniformParameters)
    shaderDescriptor.code = shaderDescriptor.code.replace(
      "var<immediate> imm:Immediate;",
      "@group(1) @binding(1) var<uniform> imm:Immediate;",
    );
  const shader = d.createShaderModule(shaderDescriptor);
  assert.deepEqual((await shader.getCompilationInfo()).messages, []);
  const bgl0 = d.createBindGroupLayout({
    entries: [0, 1].map((binding) => ({
      binding,
      visibility: GPUShaderStage.VERTEX,
      ...(textureFetch
        ? { texture: { sampleType: "uint", viewDimension: "2d" } }
        : { buffer: { type: "read-only-storage" } }),
    })),
  });
  const bgl1 = d.createBindGroupLayout({
    entries: [
      {
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: "uniform", hasDynamicOffset: true },
      },
      ...(uniformParameters
        ? [
            {
              binding: 1,
              visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
              buffer: {
                type: "uniform",
                hasDynamicOffset: true,
                minBindingSize: 64,
              },
            },
          ]
        : []),
    ],
  });
  const layout = d.createPipelineLayout({
    bindGroupLayouts: [bgl0, bgl1],
    immediateSize: uniformParameters ? 0 : 64,
  });
  const p = d.createRenderPipeline({
    layout,
    vertex: { module: shader, entryPoint: "vs_main" },
    fragment: {
      module: shader,
      entryPoint: "fs_main",
      targets: [{ format: "rgba8unorm" }],
    },
    primitive: { topology: "triangle-list", frontFace: "cw", cullMode: "none" },
    depthStencil: {
      format: "depth24plus",
      depthWriteEnabled: true,
      depthCompare: "greater",
    },
  });
  const positions = [
    [-1, -1, -0.5],
    [-1, 1, -0.5],
    [1, -1, -0.5],
    [1, -1, -0.5],
    [-1, 1, -0.5],
    [1, 1, -0.5],
  ];
  const vertices = new ArrayBuffer(16384),
    dv = new DataView(vertices);
  const arrays = new ArrayBuffer(16384),
    av = new DataView(arrays);
  positions.forEach((pos, i) => {
    const offset = base + i * (indexed ? 10 : positionSize + 8);
    const data = indexed ? av : dv;
    const address = indexed ? arrayBase + i * positionSize : offset;
    pos.forEach((v, c) =>
      format === "f32"
        ? data.setFloat32(address + c * 4, v, false)
        : data.setInt16(address + c * 2, v * 16384, false),
    );
    if (indexed) dv.setUint16(offset, i, false);
    const color = offset + (indexed ? 2 : positionSize);
    [64, 128, 192, 255].forEach((v, c) => dv.setUint8(color + c, v));
    dv.setUint16(color + 4, 0x1234, false);
    dv.setUint16(color + 6, 0xabcd, false);
  });
  const stage = d.createBuffer({
    size: stageSize,
    usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
  });
  const uniformOffset = d.limits.minUniformBufferOffsetAlignment;
  const parameterOffset = uniformOffset + 3840;
  const uniformData = new Float32Array(
    (3840 + uniformOffset + (uniformParameters ? 64 : 0)) / 4,
  );
  const imm = new Uint32Array(16);
  imm[0] = base;
  imm[1] = 9;
  imm[4] = arrayBase;
  if (uniformParameters)
    new Uint32Array(uniformData.buffer, parameterOffset, 16).set(imm);
  const uniforms = new Float32Array(uniformData.buffer, uniformOffset);
  uniforms.set([640, 360, 640, 480]);
  uniforms.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1], 4);
  uniforms.set([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0], 20 + 9 * 12);
  const uniformBuffer = d.createBuffer({
    size: 24 * 1024 * 1024,
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
  const makeTex = () =>
    d.createTexture({
      size: [256, 1280, 1],
      format: "rgba32uint",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
  const makeStorage = (size) =>
    d.createBuffer({
      size,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
  const vt = textureFetch
      ? makeTex()
      : makeStorage(bufferSize("VertexBufferSize")),
    at = textureFetch
      ? makeTex()
      : makeStorage(bufferSize("StorageBufferSize"));
  const bg0 = d.createBindGroup({
    layout: bgl0,
    entries: [
      { binding: 0, resource: textureFetch ? vt.createView() : { buffer: vt } },
      { binding: 1, resource: textureFetch ? at.createView() : { buffer: at } },
    ],
  });
  const bg1 = d.createBindGroup({
    layout: bgl1,
    entries: [
      { binding: 0, resource: { buffer: uniformBuffer, size: 3840 } },
      ...(uniformParameters
        ? [{ binding: 1, resource: { buffer: uniformBuffer, size: 64 } }]
        : []),
    ],
  });
  const dst = d.createTexture({
    size: [64, 64, 1],
    format: "rgba8unorm",
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  const read = d.createBuffer({
    size: 64 * 64 * 4,
    usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
  });
  d.queue.writeBuffer(stage, 0, vertices);
  d.queue.writeBuffer(stage, storageStage, arrays);
  d.queue.writeBuffer(stage, uniformStage, uniformData);
  const indexBuffer = d.createBuffer({
    size: bufferSize("IndexBufferSize"),
    usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
  });
  d.queue.writeBuffer(
    stage,
    indexStage + 64,
    new Uint16Array([0, 1, 2, 3, 4, 5]),
  );
  new Uint8Array(vertices).fill(0);
  new Uint8Array(arrays).fill(0);
  uniformData.fill(0);
  const depth = d.createTexture({
    size: [64, 64, 1],
    format: "depth24plus",
    usage: GPUTextureUsage.RENDER_ATTACHMENT,
  });
  const enc = d.createCommandEncoder();
  if (textureFetch) {
    enc.copyBufferToTexture(
      { buffer: stage, offset: 0, bytesPerRow: 4096, rowsPerImage: 4 },
      { texture: vt },
      [256, 4, 1],
    );
    enc.copyBufferToTexture(
      {
        buffer: stage,
        offset: storageStage,
        bytesPerRow: 4096,
        rowsPerImage: 4,
      },
      { texture: at },
      [256, 4, 1],
    );
  } else {
    enc.copyBufferToBuffer(stage, 0, vt, 0, vertices.byteLength);
    enc.copyBufferToBuffer(stage, storageStage, at, 0, arrays.byteLength);
  }
  enc.copyBufferToBuffer(
    stage,
    uniformStage,
    uniformBuffer,
    0,
    uniformData.byteLength,
  );
  enc.copyBufferToBuffer(stage, indexStage + 64, indexBuffer, 64, 12);
  const pass = enc.beginRenderPass({
    colorAttachments: [
      {
        view: dst.createView(),
        loadOp: "clear",
        storeOp: "store",
        clearValue: [0, 0, 0, 1],
      },
    ],
    depthStencilAttachment: {
      view: depth.createView(),
      depthLoadOp: "clear",
      depthStoreOp: "store",
      depthClearValue: 0,
    },
  });
  pass.setBindGroup(0, bg0);
  pass.setBindGroup(
    1,
    bg1,
    uniformParameters ? [uniformOffset, parameterOffset] : [uniformOffset],
  );
  if (!uniformParameters) pass.setImmediates(0, imm);
  pass.setPipeline(p);
  pass.setViewport(0, 0, 64, 64, 0, 1);
  pass.setScissorRect(0, 0, 64, 64);
  pass.setIndexBuffer(indexBuffer, "uint16", 64, 12);
  pass.drawIndexed(6);
  pass.end();
  enc.copyTextureToBuffer(
    { texture: dst },
    { buffer: read, bytesPerRow: 256 },
    [64, 64, 1],
  );
  d.queue.submit([enc.finish()]);
  await read.mapAsync(GPUMapMode.READ);
  const data = new Uint8Array(read.getMappedRange());
  let colored = 0;
  for (let i = 0; i < data.length; i += 4) {
    if (
      data[i] === 64 &&
      data[i + 1] === 128 &&
      data[i + 2] === 192 &&
      data[i + 3] === 255
    )
      ++colored;
  }
  assert.equal(
    colored,
    4096,
    `${format} base=${base} indexed=${indexed}: ${colored}/4096`,
  );
  assert.equal(error, null);
  read.unmap();
  for (const resource of [
    stage,
    uniformBuffer,
    indexBuffer,
    vt,
    at,
    dst,
    read,
    depth,
  ])
    resource.destroy();
  console.log(`${format} base=${base} indexed=${indexed}: 4096/4096`);
};
try {
  for (const format of ["f32", "s16"])
    for (const base of [0, 1, 2, 3, 4, 8, 12, 14, 15, 4093, 4094, 4095, 4096])
      for (const indexed of [false, true]) await run(format, base, indexed);
} finally {
  d.destroy();
}
