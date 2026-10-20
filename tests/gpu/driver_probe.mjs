import fs from "node:fs";
import assert from "node:assert/strict";
const { create, globals } = await import(
  process.env.FOXHOLLOW_WEBGPU_MODULE || "webgpu"
);
Object.assign(globalThis, globals);
const gpu = create([
  "backend=opengles",
  "enable-dawn-features=allow_unsafe_apis",
]);
const a = await gpu.requestAdapter({ featureLevel: "compatibility" });
assert(a);
const d = await a.requestDevice({ requiredLimits: { maxImmediateSize: 64 } });
let error = null;
d.addEventListener("uncapturederror", (e) => {
  error = e.error;
  console.error(error);
});
const values = new Uint32Array([
  4093, 9, 32768, 0, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 65536,
  131072, 262144,
]);
const alignment = d.limits.minUniformBufferOffsetAlignment;
const uniform = d.createBuffer({
  size: alignment + 3840,
  usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
});
d.queue.writeBuffer(uniform, alignment, values);
d.queue.writeBuffer(uniform, 0, new Uint32Array([42, 43, 44, 45]));
const index = d.createBuffer({
  size: 8,
  usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
});
d.queue.writeBuffer(index, 0, new Uint16Array([0, 1, 2, 0]));
const color = d.createTexture({
  size: [4, 4, 1],
  format: "rgba8unorm",
  usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
});
const read = d.createBuffer({
  size: 6144,
  usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
});
const encoder = d.createCommandEncoder();
for (let test = 0; test < 6; ++test) {
  const mode = test === 1 || test === 2 ? test : 0;
  const code = fs.readFileSync(
    `${process.argv[2] || "build-input-tests"}/driver-probe-${mode}.wgsl`,
    "utf8",
  );
  const shader = d.createShaderModule({ code });
  assert.deepEqual((await shader.getCompilationInfo()).messages, []);
  const bgl = d.createBindGroupLayout({
    entries: [
      {
        binding: 0,
        visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
        buffer: { type: "uniform", hasDynamicOffset: true, minBindingSize: 16 },
      },
      ...(mode === 2
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
    bindGroupLayouts: mode !== 0 ? [bgl] : [],
    immediateSize: mode === 1 ? 64 : 0,
  });
  const format = test === 4 ? "depth24plus" : "depth32float";
  const hasDepth = test === 3 || test === 4;
  const pipeline = d.createRenderPipeline({
    layout,
    vertex: { module: shader, entryPoint: "vs_main" },
    fragment: {
      module: shader,
      entryPoint: "fs_main",
      targets: [{ format: "rgba8unorm" }],
    },
    ...(hasDepth
      ? {
          depthStencil: {
            format,
            depthWriteEnabled: true,
            depthCompare: "greater",
          },
        }
      : {}),
  });
  const depth = hasDepth
    ? d.createTexture({
        size: [4, 4, 1],
        format,
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
      })
    : null;
  const pass = encoder.beginRenderPass({
    colorAttachments: [
      {
        view: color.createView(),
        loadOp: "clear",
        storeOp: "store",
        clearValue: [0, 0, 0, 1],
      },
    ],
    ...(hasDepth
      ? {
          depthStencilAttachment: {
            view: depth.createView(),
            depthLoadOp: "clear",
            depthStoreOp: "store",
            depthClearValue: 0,
          },
        }
      : {}),
  });
  if (mode === 1) pass.setImmediates(0, values);
  if (mode !== 0) {
    const bg = d.createBindGroup({
      layout: bgl,
      entries: [
        { binding: 0, resource: { buffer: uniform, size: 3840 } },
        ...(mode === 2
          ? [{ binding: 1, resource: { buffer: uniform, size: 64 } }]
          : []),
      ],
    });
    pass.setBindGroup(0, bg, mode === 2 ? [0, alignment] : [0]);
  }
  pass.setPipeline(pipeline);
  pass.setViewport(0, 0, 4, 4, 0, 1);
  pass.setScissorRect(0, 0, 4, 4);
  if (test === 5) {
    pass.setIndexBuffer(index, "uint16", 0, 8);
    pass.drawIndexed(3);
  } else pass.draw(3);
  pass.end();
  encoder.copyTextureToBuffer(
    { texture: color },
    { buffer: read, offset: test * 1024, bytesPerRow: 256, rowsPerImage: 4 },
    [4, 4, 1],
  );
}
d.queue.submit([encoder.finish()]);
await read.mapAsync(GPUMapMode.READ);
const data = new Uint8Array(read.getMappedRange());
for (let test = 0; test < 6; ++test) {
  for (let y = 0; y < 4; ++y)
    for (let x = 0; x < 4; ++x)
      assert.deepEqual(
        [
          ...data.slice(
            test * 1024 + y * 256 + x * 4,
            test * 1024 + y * 256 + x * 4 + 4,
          ),
        ],
        [64, 128, 192, 255],
      );
  console.log(
    ["solid", "immediate64", "uniform64", "depth32", "depth24", "indexed"][
      test
    ] + ": 16/16",
  );
}
assert.equal(error, null);
read.unmap();
d.destroy();
