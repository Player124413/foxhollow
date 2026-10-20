#include "../extern/aurora/lib/webgpu/driver_probe.hpp"
#include <cassert>
#include <fstream>
#include <string>
int main(int argc, char** argv) {
  assert(argc == 2);
  using aurora::webgpu::driver_probe_pixel;
  assert(driver_probe_pixel(64,128,192,255));
  assert(driver_probe_pixel(62,130,190,253));
  assert(!driver_probe_pixel(0,0,0,255));
  assert(!driver_probe_pixel(255,0,0,255));
  assert(!driver_probe_pixel(192,128,64,255));
  assert(!driver_probe_pixel(64,128,192,0));
  for (unsigned mode = 0; mode < 3; ++mode) {
    const auto source = aurora::webgpu::driver_probe_source(mode);
    assert((source.find("var<immediate>") != std::string::npos) == (mode == 1));
    assert((source.find("var<uniform> params:") != std::string::npos) == (mode == 2));
    assert(source.find("262144u") != std::string::npos);
    std::ofstream out(std::string(argv[1]) + "/driver-probe-" + std::to_string(mode) + ".wgsl");
    out << source;
    assert(out.good());
  }
}
