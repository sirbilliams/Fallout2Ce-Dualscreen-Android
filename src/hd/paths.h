#ifndef FALLOUT_HD_PATHS_H_
#define FALLOUT_HD_PATHS_H_

#include <string>
#include "platform_compat.h"

namespace fallout::hd {
// Configs and FRM paths can still contain DOS separators. Keep filesystem
// operations native, and route file opens through CE's case-aware helper.
inline std::string nativePath(std::string path)
{
    compat_windows_path_to_native(path.data());
    return path;
}
inline FILE* openFile(const std::string& path, const char* mode)
{
    // CE's compatibility helpers use COMPAT_MAX_PATH-sized local buffers.
    return path.size() < COMPAT_MAX_PATH ? compat_fopen(path.c_str(), mode) : nullptr;
}
} // namespace fallout::hd
#endif
