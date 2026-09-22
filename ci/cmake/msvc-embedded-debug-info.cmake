# Injected into every project() via CMAKE_PROJECT_INCLUDE (see ci/build.sh).
#
# sccache cannot cache MSVC compilations that write to a shared PDB (/Zi or
# /ZI), so CI builds embed debug info in the objects (/Z7) instead. The
# debug-info format itself comes from CMAKE_MSVC_DEBUG_INFORMATION_FORMAT; this
# fixes up flags that projects hardcode (RocksDB does both):
#
# * /Zi and /ZI are removed
# * /d2Zi+ is replaced by its documented spelling /Zo, since sccache takes
#   unknown slash arguments for input files and refuses to cache
#
# It runs at the end of each project's directory so later appends are caught.

function(nano_fix_msvc_debug_flags)
  foreach(lang C CXX)
    foreach(suffix "" _DEBUG _RELWITHDEBINFO)
      set(var CMAKE_${lang}_FLAGS${suffix})
      set(value " ${${var}} ")
      set(previous "")
      # A match consumes the separator, so adjacent flags take another pass
      while(NOT value STREQUAL previous)
        set(previous "${value}")
        string(REGEX REPLACE " [/-]Z[iI] " " " value "${value}")
        string(REGEX REPLACE " [/-]d2Zi\\+ " " /Zo " value "${value}")
      endwhile()
      string(STRIP "${value}" value)
      set(${var}
          "${value}"
          PARENT_SCOPE)
    endforeach()
  endforeach()
endfunction()

cmake_language(DEFER CALL nano_fix_msvc_debug_flags)
