#!/bin/bash
set -euox pipefail
shopt -s nocasematch  # Enable case-insensitive matching

BUILD_TARGET=""
if [[ ${1:-} ]]; then
    BUILD_TARGET="--target $1"
fi

SRC=${SRC:-${PWD}}
OS=$(uname)

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

# sccache cannot cache MSVC objects that share a PDB (/Zi), so embed debug info in them (/Z7)
CMAKE_MSVC_DEBUG_INFO=""
case "${OS}" in
    CYGWIN*|MINGW32*|MSYS*|MINGW*)
        if [[ ${CMAKE_CXX_COMPILER_LAUNCHER:-} ]]; then
            MSVC_DEBUG_INFO_INCLUDE=$(cygpath -m "$(dirname "$BASH_SOURCE")/cmake/msvc-embedded-debug-info.cmake")
            CMAKE_MSVC_DEBUG_INFO="-DCMAKE_POLICY_DEFAULT_CMP0141=NEW"
            CMAKE_MSVC_DEBUG_INFO+=" -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=\$<\$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
            CMAKE_MSVC_DEBUG_INFO+=" -DCMAKE_PROJECT_INCLUDE=${MSVC_DEBUG_INFO_INCLUDE}"
        fi
        ;;
esac

BUILD_DIR="build"

mkdir -p $BUILD_DIR
pushd $BUILD_DIR

cmake \
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
${CMAKE_MSVC_DEBUG_INFO:-} \
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

cmake --build ${PWD} ${BUILD_TARGET} --parallel $(number_of_processors)

popd
