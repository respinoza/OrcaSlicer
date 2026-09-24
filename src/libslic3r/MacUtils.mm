#import <Foundation/Foundation.h>
#import "MacUtils.hpp"

namespace Slic3r {

bool is_macos_support_boost_add_file_log()
{
    if (@available(macOS 12.0, *)) {
        return true;
    } else {
        return false;
    }
}

bool IsMacVersion15()
{
    if (@available(macOS 15.0, *))
    {
        return true;
    }
    else
    {
        return false;
    }
}

int is_mac_version_15()
{
    return IsMacVersion15() ? 1 : 0;
}
}; // namespace Slic3r
