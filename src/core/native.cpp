#include "thunder/native.hpp"

#include <filesystem>
#include <format>

namespace thunder
{
    namespace fs = std::filesystem;

    int NativeModules::register_function(ThunderRegistry* registry, const char* name, ThunderFunction function)
    {
        if (registry == nullptr || registry->opaque == nullptr || name == nullptr || function == nullptr)
        {
            return 0;
        }
        auto* modules = static_cast<NativeModules*>(registry->opaque);
        // A name registered twice keeps the first module's function, so one library cannot
        // quietly take over another's.
        return modules->functions_.emplace(name, function).second ? 1 : 0;
    }

    std::optional<std::string> NativeModules::load(const std::string& name, const std::vector<std::string>& search_paths,
        const ThunderApi& api)
    {
        const std::string file = os::library_file_name(name);
        std::vector<std::string> candidates;
        for (const std::string& directory : search_paths)
        {
            if (!directory.empty())
            {
                candidates.push_back(os::path_text(os::utf8_path(directory) / os::utf8_path(file)));
            }
        }
        candidates.push_back(file); // let the platform's loader look in its own places

        std::string reasons;
        for (const std::string& candidate : candidates)
        {
            auto library = os::Library::open(candidate);
            if (!library)
            {
                if (!reasons.empty())
                {
                    reasons += "; ";
                }
                reasons += library.error();
                continue;
            }

            void* symbol = library->symbol("thunder_module_init");
            if (symbol == nullptr)
            {
                return std::format("'{}' is not a Thunder native module (it exports no thunder_module_init)",
                    candidate);
            }
            // The cast is the whole point of a plain C entry point: the signature is fixed by
            // native.h, so both sides agree even across compilers.
            auto init = reinterpret_cast<ThunderModuleInit>(symbol);

            ThunderRegistry registry{};
            registry.abi_version = THUNDER_NATIVE_ABI;
            registry.opaque = this;
            registry.add = &NativeModules::register_function;
            const int ok = init(&registry, &api);
            if (ok == 0)
            {
                return std::format("'{}' failed to start (thunder_module_init returned 0)", candidate);
            }
            libraries_.push_back(std::move(*library));
            return std::nullopt;
        }
        return reasons.empty() ? std::format("'{}' not found", file) : reasons;
    }

    ThunderFunction NativeModules::find(const std::string& name) const
    {
        const auto found = functions_.find(name);
        return found == functions_.end() ? nullptr : found->second;
    }
}
