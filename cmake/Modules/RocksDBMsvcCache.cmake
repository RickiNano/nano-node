# Included via CMAKE_PROJECT_rocksdb_INCLUDE, so this runs inside RocksDB's own
# directory scope right after its project() call.
#
# RocksDB unconditionally appends /Zi and /d2Zi+ to CMAKE_CXX_FLAGS for MSVC, and
# both defeat compiler caching. /Zi points every translation unit at a single
# PDB shared by the whole target, which cannot be cached per compilation, and the
# undocumented /d2Zi+ is misparsed by sccache badly enough that it rejects the
# command line as having "multiple input files". Together they account for a
# third of the object files in a Windows build.
#
# The flags are added well after this file is included, so schedule the cleanup
# to run once RocksDB has finished with its directory. Debug info is not lost:
# /Z7 embeds it in the object files instead.

if(NOT MSVC)
  return()
endif()

if(NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  return()
endif()

# cmake_language(DEFER) requires CMake 3.19; without it RocksDB simply stays
# uncached rather than failing to build.
if(CMAKE_VERSION VERSION_LESS 3.19)
  message(
    STATUS "RocksDB: CMake < 3.19, leaving MSVC debug flags uncacheable")
  return()
endif()

function(nano_strip_rocksdb_uncacheable_flags)
  foreach(flags_var CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    string(REPLACE "/d2Zi+" "" ${flags_var} "${${flags_var}}")
    string(REPLACE "/Zi" "" ${flags_var} "${${flags_var}}")
    set(${flags_var} "${${flags_var}}" PARENT_SCOPE)
  endforeach()
endfunction()

cmake_language(DEFER CALL nano_strip_rocksdb_uncacheable_flags)
