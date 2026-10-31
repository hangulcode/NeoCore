#!/usr/bin/env bash
# NeoCore C ABI와 런타임 정적 라이브러리를 합쳐 Unity iOS 패키지에 배포한다.
# 실행에는 macOS와 Xcode가 필요하다. device와 simulator 산출물은 같은 배포 경로를 쓴다.
set -euo pipefail

TARGET="${1:-device}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
PLUGIN_DIR="$SCRIPT_DIR/com.neocore.unity/Runtime/Plugins/iOS"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: iOS libraries can only be built on macOS with Xcode." >&2
    exit 1
fi

case "$TARGET" in
    device)
        SYSROOT="iphoneos"
        ARCHS="arm64"
        BUILD_DIR="$REPO_ROOT/Build/neocore-unity-ios"
        ;;
    simulator)
        SYSROOT="iphonesimulator"
        ARCHS="arm64;x86_64"
        BUILD_DIR="$REPO_ROOT/Build/neocore-unity-ios-sim"
        ;;
    *)
        echo "usage: $0 [device|simulator]" >&2
        exit 2
        ;;
esac

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" \
    -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_IOS_INSTALL_COMBINED=NO \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DCMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET=13.0 \
    -DNEOCORE_BUILD_UNITY_BRIDGE=ON \
    -DNEOCORE_BUILD_TESTS=OFF \
    -DNEOCORE_BUILD_EXAMPLES=OFF
cmake --build "$BUILD_DIR" --config Release --target NeoCoreUnity

# 정적 CMake 대상의 링크 의존성은 .a 안에 자동으로 들어가지 않으므로 모두 합친다.
ARCHIVES=()
for NAME in NeoCoreUnity neocore_runtime neocore_frontend neocore_type_model neocore_backend; do
    ARCHIVE="$(find "$BUILD_DIR" -type f -path '*Release*' -name "lib${NAME}.a" -print -quit)"
    if [[ -z "$ARCHIVE" ]]; then
        echo "error: lib${NAME}.a not found under $BUILD_DIR" >&2
        exit 1
    fi
    ARCHIVES+=("$ARCHIVE")
done
mkdir -p "$PLUGIN_DIR"
xcrun libtool -static -o "$PLUGIN_DIR/libNeoCoreUnity.a" "${ARCHIVES[@]}"
cp "$REPO_ROOT/THIRD_PARTY_NOTICES.txt" "$SCRIPT_DIR/com.neocore.unity/THIRD_PARTY_NOTICES.txt"
cp "$REPO_ROOT/LICENSE" "$SCRIPT_DIR/com.neocore.unity/LICENSE"
echo "deployed $PLUGIN_DIR/libNeoCoreUnity.a ($TARGET)"
