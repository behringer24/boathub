# Writes the commit and the time of this build into a header in the build
# directory, for the boot event to name the firmware it starts.
#
# A header that changes on every build, rather than a build flag: a flag that
# changes recompiles every file, the header only the one that includes it.
# See docs/design/A-011-events.md.

Import("env")

import datetime
import os
import subprocess


def commit():
    try:
        root = env.subst("$PROJECT_DIR")
        rev = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=root, stderr=subprocess.DEVNULL
        ).decode().strip()
        dirty = subprocess.call(
            ["git", "diff", "--quiet", "HEAD", "--", "."], cwd=root, stderr=subprocess.DEVNULL
        )
        return rev + ("+" if dirty else "")
    except Exception:
        return "unknown"


build_dir = env.subst("$BUILD_DIR")
os.makedirs(build_dir, exist_ok=True)
stamp = "%s %s" % (
    commit(),
    datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
)
with open(os.path.join(build_dir, "build_stamp.h"), "w") as f:
    f.write('#define BUILD_STAMP "%s"\n' % stamp)

env.Append(CPPPATH=[build_dir])
