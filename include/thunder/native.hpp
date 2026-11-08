// The host side of the C++ native interface: loading native libraries and finding the
// functions they register. The C ABI the libraries see is include/thunder/native.h, and
// docs/native.md explains the whole interface.
#pragma once

#include "thunder/native.h"
#include "thunder/os.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thunder
{
    // The libraries a program imports (import native "name") and the functions in them.
    class NativeModules
    {
    public:
        NativeModules() = default;
        NativeModules(const NativeModules&) = delete;
        NativeModules& operator=(const NativeModules&) = delete;

        // Where a library called 'name' is looked for, in order:
        //   1. each directory in 'search_paths' (the program's own directory first);
        //   2. the platform's own library search (so a system-wide library is found too).
        // Returns the reason it could not be loaded.
        std::optional<std::string> load(const std::string& name, const std::vector<std::string>& search_paths,
            const ThunderApi& api);

        // The function a loaded library registered under this name, or nullptr.
        ThunderFunction find(const std::string& name) const;

        bool empty() const { return libraries_.empty(); }

    private:
        static int register_function(ThunderRegistry* registry, const char* name, ThunderFunction function);

        std::vector<os::Library> libraries_;
        std::unordered_map<std::string, ThunderFunction> functions_;
    };
}
