#pragma once

// Central header file for WebServ project
#include "config.hpp"
#include "server.hpp"
#include "http.hpp"
#include "socket.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "exceptions.hpp"

// Add any additional global declarations or includes here if necessary
#include <iostream>
#include <stdlib.h>
#include <string.h>

// add any defines here
#define TIME_OUT_MS 5000 // 5 seconds timeout for poll