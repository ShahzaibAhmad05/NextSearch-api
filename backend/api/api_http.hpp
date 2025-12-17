#pragma once

#include "third_party/httplib.h"

namespace cord19 {

void enable_cors(httplib::Response& res);

} // namespace cord19
