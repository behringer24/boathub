// Which firmware this is: the commit it was built from, with a "+" when the
// working tree had changes, and the time of the build in UTC. Written by
// tools/build_stamp.py on every build.

#pragma once

namespace build_info {

const char *stamp();

}  // namespace build_info
