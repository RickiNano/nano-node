#!/bin/bash
set -euox pipefail
shopt -s nocasematch  # Enable case-insensitive matching

BUILD_TARGET=""
if [[ ${1:-} ]]; then
    BUILD_TARGET="--target $1"
fi

SRC=${SRC:-${PWD}}
OS=$(uname)

CMAKE_GENERATOR_ARGS=""
USE_NINJA=0
case "$(uname -s)" in
    CYGWIN*|MINGW32*|MSYS*|MINGW*)
        if command -v ninja >/dev/null 2>&1; then
            # rocksdb's CMakeLists declares `LANGUAGES ... ASM` unconditionally.
            # The VS generator silently routes plain ASM to MASM on Windows;
            # Ninja doesn't, so the ASM probe fails and CMAKE_ASM_COMPILE_OBJECT
            # ends up empty. No ASM sources actually compile for Windows x64
            # (rocksdb only assembles .S on PowerPC), so pointing CMAKE_ASM_COMPILER
            # at ml64 just to satisfy the probe is sufficient.
            CMAKE_GENERATOR_ARGS="-G Ninja -DCMAKE_ASM_COMPILER=ml64"
            USE_NINJA=1
        fi
        ;;
esac

CMAKE_QT_DIR=""
if [[ ${QT_DIR:-} ]]; then
    CMAKE_QT_DIR="-DQt5_DIR=${QT_DIR}"
fi

CMAKE_SANITIZER=""
if [[ ${SANITIZER:-} ]]; then
    case "${SANITIZER}" in
        ASAN)
            CMAKE_SANITIZER="-DNANO_ASAN=ON"
            ;;
        ASAN_INT)
            CMAKE_SANITIZER="-DNANO_ASAN_INT=ON"
            ;;
        TSAN)
            CMAKE_SANITIZER="-DNANO_TSAN=ON"
            ;;
        UBSAN)
            CMAKE_SANITIZER="-DNANO_UBSAN=ON"
            ;;
        LEAK)
            CMAKE_SANITIZER="-DNANO_ASAN=ON"
            ;;
        *)
            echo "Unknown sanitizer: '${SANITIZER}'"
            exit 1
            ;;
    esac
fi

BUILD_DIR="build"

mkdir -p $BUILD_DIR
pushd $BUILD_DIR

cmake \
${CMAKE_GENERATOR_ARGS} \
-DCMAKE_BUILD_TYPE=${BUILD_TYPE:-"Debug"} \
-DPORTABLE=ON \
-DACTIVE_NETWORK=nano_${NANO_NETWORK:-"live"}_network \
-DNANO_TEST=${NANO_TEST:-OFF} \
-DNANO_GUI=${NANO_GUI:-OFF} \
-DNANO_TRACING=${NANO_TRACING:-OFF} \
-DCOVERAGE=${COVERAGE:-OFF} \
-DCI_TAG=${CI_TAG:-OFF} \
-DCI_VERSION_PRE_RELEASE=${CI_VERSION_PRE_RELEASE:-OFF} \
${CMAKE_SANITIZER:-} \
${CMAKE_QT_DIR:-} \
${SRC}

number_of_processors() {
    case "$(uname -s)" in
        Linux*)
            nproc
            ;;
        Darwin*)
            sysctl -n hw.ncpu
            ;;
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo "${NUMBER_OF_PROCESSORS}"
            ;;
        *)
            echo "Unknown OS"
            exit 1
            ;;
    esac
}

parallel_build_flag() {
    if [[ "${USE_NINJA}" -eq 1 ]]; then
        # Ninja auto-parallelizes to all available cores.
        echo ""
        return
    fi
    case "$(uname -s)" in
        CYGWIN*|MINGW32*|MSYS*|MINGW*)
            echo "-- -m"
            ;;
        *)
            echo "--parallel $(number_of_processors)"
            ;;
    esac
}

cmake --build ${PWD} ${BUILD_TARGET} $(parallel_build_flag)

popd
