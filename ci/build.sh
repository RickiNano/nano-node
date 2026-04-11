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

CMAKE_LAUNCHER_FLAGS=""
if [[ ${CMAKE_COMPILER_LAUNCHER:-} ]]; then
    # Scoped to submodule/stable targets only (see nano_add_cached_subdirectory
    # in CMakeLists.txt) — nano's own sources change every commit and would just
    # churn the cache, so we don't pass CMAKE_<LANG>_COMPILER_LAUNCHER globally.
    CMAKE_LAUNCHER_FLAGS="-DNANO_COMPILER_LAUNCHER=${CMAKE_COMPILER_LAUNCHER}"
    # MSVC's default /Zi writes to a shared PDB which sccache cannot cache; force /Z7 (embedded debug info) via CMP0141 so the launcher is actually effective on Windows.
    CMAKE_LAUNCHER_FLAGS="${CMAKE_LAUNCHER_FLAGS} -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded"
fi

# DIAG(sccache): confirm the env var reached this script and show what will be
# forwarded to cmake. Also verify sccache is actually reachable on PATH.
echo "[sccache-diag] CMAKE_COMPILER_LAUNCHER='${CMAKE_COMPILER_LAUNCHER:-unset}'"
echo "[sccache-diag] CMAKE_LAUNCHER_FLAGS='${CMAKE_LAUNCHER_FLAGS}'"
echo "[sccache-diag] SCCACHE_GHA_ENABLED='${SCCACHE_GHA_ENABLED:-unset}' SCCACHE_GHA_VERSION='${SCCACHE_GHA_VERSION:-unset}'"
if command -v sccache >/dev/null 2>&1; then
    echo "[sccache-diag] sccache on PATH: $(command -v sccache)"
    sccache --version || true
else
    echo "[sccache-diag] sccache NOT on PATH"
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
-DCMAKE_BUILD_TYPE=${BUILD_TYPE:-"Debug"} \
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
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
${CMAKE_LAUNCHER_FLAGS:-} \
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

# DIAG(sccache): sample compile_commands.json to verify the launcher was baked
# into at least one submodule compile line, then show stats from the SAME
# process tree as the build (the separate "sccache stats" CI step runs in a
# fresh shell — if its numbers diverge from this, the post-step is restarting
# the server and the CI step's output is a red herring).
if [[ ${CMAKE_COMPILER_LAUNCHER:-} ]]; then
    if [[ -f compile_commands.json ]]; then
        echo "[sccache-diag] compile_commands.json entries mentioning '${CMAKE_COMPILER_LAUNCHER}':"
        grep -c "${CMAKE_COMPILER_LAUNCHER}" compile_commands.json || true
        echo "[sccache-diag] first launcher-prefixed compile line:"
        grep -m1 "${CMAKE_COMPILER_LAUNCHER}" compile_commands.json || echo "[sccache-diag] (none — launcher is not in compile_commands.json)"
    else
        echo "[sccache-diag] compile_commands.json missing — cannot verify launcher landed in compile lines"
    fi
    if command -v sccache >/dev/null 2>&1; then
        echo "[sccache-diag] sccache stats from inside build.sh (same process tree as the build):"
        sccache --show-stats || true
    fi
fi

popd
