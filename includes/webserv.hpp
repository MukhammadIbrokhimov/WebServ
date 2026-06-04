#pragma once

// my catch-all header. every .cpp just includes this one so I don't have to
// remember which sub-header a thing lives in.
#include "config.hpp"
#include "lexer.hpp"
#include "server.hpp"
#include "http.hpp"
#include "socket.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "exceptions.hpp"

// std stuff I end up needing almost everywhere
#include <iostream>
#include <stdlib.h>
#include <string.h>

// poll() timeout. 5s is a compromise: long enough that we're not busy-spinning
// when nothing's happening, short enough that after Ctrl-C the loop notices the
// shutdown flag within a few seconds instead of hanging.
#define TIME_OUT_MS 5000