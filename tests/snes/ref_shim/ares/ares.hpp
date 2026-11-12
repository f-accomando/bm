// A tiny stand-in for <ares/ares.hpp> so that tests/snes/ref_diff.cpp can compile the processor modules of the ares emulator (ISC licence) without the rest
// of the emulator. It includes only the nall primitives and strings that the modules use and the type names of ares. The ares sources stay outside this repository.
#pragma once
#include <nall/platform.hpp>
#include <nall/memory.hpp>
#include <nall/array.hpp>
#include <nall/case-range.hpp>
#include <nall/endian.hpp>
#include <nall/literals.hpp>
#include <nall/serializer.hpp>
#include <nall/set.hpp>
#include <nall/string.hpp>
#include <nall/traits.hpp>
#include <memory>
#include <vector>
#include <algorithm>

#define register $register

#include <nall/primitives.hpp>
#include <ares/types.hpp>
using namespace nall;
using namespace nall::primitives;
