#pragma once

// Umbrella for the blocking frame I/O trio: the result enum, the decoded
// frame, and the stream that moves them over an fd.

#include "lsw/protocol/frame_stream.hpp"
#include "lsw/protocol/io_result.hpp"
#include "lsw/protocol/wire_frame.hpp"
