#include "build_info.h"

#include <build_stamp.h>

namespace build_info {

const char *stamp() { return BUILD_STAMP; }

}  // namespace build_info
