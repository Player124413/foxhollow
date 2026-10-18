import pathlib
import re
import subprocess
import sys

library, abi, readelf = sys.argv[1:]
assert pathlib.Path(library).is_file(), library
headers = subprocess.check_output([readelf, '-h', library], text=True)
assert 'ELF64' in headers, headers
assert ('AArch64' if abi == 'arm64-v8a' else 'X86-64') in headers, headers
symbols = subprocess.check_output([readelf, '--dyn-syms', '--wide', library], text=True)
for symbol in ('SDL_main', 'JNI_OnLoad', 'Java_org_libsdl_app_SDLActivity_nativeRunMain'):
    assert any(line.split()[-1:] == [symbol] and 'UND' not in line.split()
               for line in symbols.splitlines()), symbol
segments = subprocess.check_output([readelf, '-l', '--wide', library], text=True)
for line in segments.splitlines():
    if line.strip().startswith('LOAD '):
        assert int(line.split()[-1], 16) >= 16384, line
needed = subprocess.check_output([readelf, '-d', library], text=True)
allowed = {'libc++_shared.so', 'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so',
           'libz.so', 'libEGL.so', 'libGLESv1_CM.so', 'libGLESv2.so', 'libGLESv3.so', 'libOpenSLES.so', 'libvulkan.so',
           'libaaudio.so', 'libjnigraphics.so', 'libmediandk.so'}
for dependency in re.findall(r'\(NEEDED\).*?\[(.*?)\]', needed):
    assert dependency in allowed, dependency
