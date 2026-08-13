#pragma once

// The wire frame, in three parts: the packed header, the flag bits that
// live in it, and the reserved command ids / version constants that give
// its cmd_id field meaning. Include the specific one you need; this
// umbrella exists for code that genuinely needs all three.

#include "lsw/protocol/frame_flags.hpp"
#include "lsw/protocol/frame_header.hpp"
#include "lsw/protocol/protocol_constants.hpp"
