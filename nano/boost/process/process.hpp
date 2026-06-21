#pragma once

// Umbrella header. Boost.Process v1 (the historic `boost/process/v1.hpp`) was
// removed in Boost 1.88; the only facility nano uses is child-process spawning,
// which is now provided by the v2-backed wrapper below.
#include <nano/boost/process/child.hpp>
