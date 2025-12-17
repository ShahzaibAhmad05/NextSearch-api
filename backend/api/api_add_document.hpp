#pragma once

#include "api_engine.hpp"
#include "third_party/httplib.h"

namespace cord19 {

// Implements the existing /add_document endpoint behavior.
void handle_add_document(Engine& engine, const httplib::Request& req, httplib::Response& res);

} // namespace cord19
