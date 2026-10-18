#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
abi="${1:-arm64-v8a}"
case "$abi" in
  arm64-v8a) preset=android-arm64; triple=aarch64-linux-android; rust_target=aarch64-linux-android ;;
  x86_64) preset=android-x86_64; triple=x86_64-linux-android; rust_target=x86_64-linux-android ;;
  *) printf '%s\n' FOXHOLLOW_ANDROID_ABI_64_REQUIRED >&2; exit 2 ;;
esac
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
: "${ANDROID_HOME:?ANDROID_HOME}"
export ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-29.0.14206865}"
export ANDROID_NDK_ROOT="$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION"
export ANDROID_NDK_HOME="$ANDROID_NDK_ROOT"
[[ -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]] || { printf '%s\n' ANDROID_NDK_ROOT >&2; exit 2; }
[[ -f "$ANDROID_HOME/platforms/android-35/android.jar" ]] || { printf '%s\n' ANDROID_PLATFORM_35 >&2; exit 2; }
for program in cmake ninja rustup cargo python3; do command -v "$program" >/dev/null; done
if [[ -n "${JAVA_HOME:-}" ]]; then export PATH="$JAVA_HOME/bin:$PATH"; fi
command -v java >/dev/null
command -v javac >/dev/null
[[ -x "$ANDROID_HOME/build-tools/35.0.0/zipalign" ]] || { printf '%s\n' ANDROID_BUILD_TOOLS_35 >&2; exit 2; }
case "$(uname -s)" in
  Linux) host=linux-x86_64 ;;
  Darwin) host=darwin-x86_64 ;;
  *) printf '%s\n' FOXHOLLOW_ANDROID_BUILD_HOST_UNSUPPORTED >&2; exit 2 ;;
esac
bin="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$host/bin"
[[ -x "$bin/${triple}28-clang" ]] || { printf '%s\n' ANDROID_NDK_CLANG >&2; exit 2; }
export PATH="$bin:$PATH"
"$bin/${triple}28-clang++" --version
printf '#include <thread>\n#include <stop_token>\nint main() { std::jthread thread; return thread.joinable(); }\n' | "$bin/${triple}28-clang++" -x c++ -std=c++20 -fsyntax-only -
rustup target add "$rust_target"
suffix="${rust_target//-/_}"
export "CC_${suffix}=$bin/${triple}28-clang"
export "CXX_${suffix}=$bin/${triple}28-clang++"
export "AR_${suffix}=$bin/llvm-ar"
upper="$(printf '%s' "$suffix" | tr '[:lower:]' '[:upper:]')"
export "CARGO_TARGET_${upper}_LINKER=$bin/${triple}28-clang"
export "CARGO_TARGET_${upper}_RUSTFLAGS=-C relocation-model=pic"
cd "$root"
cmake --preset "$preset" "${@:2}"
cmake --build --preset "$preset" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
python3 android/verify-native.py "build-$preset/libmain.so" "$abi" "$bin/llvm-readelf"
cd android
./gradlew --no-daemon -PnativeBuildDir="$root/build-$preset" :app:assembleDebug :app:assembleDebugAndroidTest :app:testDebugUnitTest :app:lintDebug
"$ANDROID_HOME/build-tools/35.0.0/zipalign" -c -P 16 4 "$root/android/app/build/outputs/apk/debug/app-$abi-debug.apk"
