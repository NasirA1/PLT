#include "thunder/display.hpp"

#include <algorithm>
#include <filesystem>
#include <format>

namespace thunder
{
    namespace fs = std::filesystem;

    std::string PluginDisplay::library_name(const std::string& name)
    {
        return os::library_file_name("thunder_display_" + name);
    }

    std::vector<std::string> PluginDisplay::discover(const std::vector<std::string>& search_paths)
    {
        // library_name("*") gives the platform's pattern (e.g. "libthunder_display_*.so"):
        // whatever surrounds the '*' is what surrounds a plug-in's name in its file name.
        const std::string pattern = library_name("*");
        const std::size_t star = pattern.find('*');
        const std::string prefix = pattern.substr(0, star);
        const std::string suffix = pattern.substr(star + 1);

        std::vector<std::string> names;
        for (const std::string& directory : search_paths)
        {
            if (directory.empty())
            {
                continue;
            }
            const auto entries = os::list_directory(directory);
            if (!entries)
            {
                continue;
            }
            for (const std::string& entry : *entries)
            {
                if (entry.size() > prefix.size() + suffix.size() && entry.starts_with(prefix)
                    && entry.ends_with(suffix))
                {
                    const std::string name = entry.substr(prefix.size(), entry.size() - prefix.size() - suffix.size());
                    if (name != "text" && name != "stub" && std::ranges::find(names, name) == names.end())
                    {
                        names.push_back(name);
                    }
                }
            }
        }
        std::ranges::sort(names);
        if (const auto sdl = std::ranges::find(names, "sdl"); sdl != names.end())
        {
            std::rotate(names.begin(), sdl, sdl + 1); // "sdl" first: the plug-in Thunder ships
        }
        return names;
    }

    std::expected<std::unique_ptr<PluginDisplay>, std::string> PluginDisplay::load(const std::string& name,
        const std::vector<std::string>& search_paths, std::string title)
    {
        const std::string file = library_name(name);
        std::vector<std::string> candidates;
        for (const std::string& directory : search_paths)
        {
            if (!directory.empty())
            {
                candidates.push_back(os::path_text(os::utf8_path(directory) / os::utf8_path(file)));
            }
        }
        candidates.push_back(file); // then wherever the platform's own loader looks

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
            void* symbol = library->symbol("thunder_display_init");
            if (symbol == nullptr)
            {
                return std::unexpected(
                    std::format("'{}' is not a Thunder display (it exports no thunder_display_init)", candidate));
            }

            // The constructor is private: this is the only way a display is made.
            std::unique_ptr<PluginDisplay> display(new PluginDisplay);
            display->title_ = std::move(title);
            const auto init = reinterpret_cast<ThunderDisplayInit>(symbol);
            if (init(&display->display_) == 0)
            {
                return std::unexpected(std::format("'{}' failed to start", candidate));
            }
            // init has allocated the plug-in's state, so close() now owns freeing it - even if
            // the checks below reject the plug-in and the window is never opened.
            display->started_ = display->display_.close != nullptr;
            if (display->display_.abi_version != THUNDER_DISPLAY_ABI)
            {
                return std::unexpected(std::format("'{}' was built for display interface {}, this is {}", candidate,
                    display->display_.abi_version, THUNDER_DISPLAY_ABI));
            }
            if (display->display_.open == nullptr || display->display_.present == nullptr
                || display->display_.poll == nullptr || display->display_.close == nullptr)
            {
                return std::unexpected(std::format("'{}' did not fill in the whole display interface", candidate));
            }
            display->library_ = std::move(*library);
            return display;
        }
        return std::unexpected(reasons.empty() ? std::format("'{}' not found", file) : reasons);
    }

    PluginDisplay::~PluginDisplay()
    {
        if (started_)
        {
            // Called even when no window was ever opened: close() frees what init() allocated.
            display_.close(&display_);
        }
    }

    void PluginDisplay::present(const graphics::Canvas& canvas, std::size_t keep)
    {
        if (quit_)
        {
            return;
        }
        if (!opened_)
        {
            const char* error = nullptr;
            if (display_.open(&display_, static_cast<std::int32_t>(canvas.width()),
                    static_cast<std::int32_t>(canvas.height()), title_.c_str(), &error)
                == 0)
            {
                quit_ = true; // the window could not be opened: run on without one
                return;
            }
            opened_ = true;
        }

        // The shapes are copied into the plain C structs of display.h. The strings point into
        // the canvas, which outlives the call.
        const std::vector<graphics::Shape>& shapes = canvas.shapes();
        std::vector<ThunderShape> converted;
        converted.reserve(shapes.size() - std::min(keep, shapes.size()));
        for (std::size_t index = keep; index < shapes.size(); ++index)
        {
            const graphics::Shape& shape = shapes[index];
            ThunderShape out{};
            out.kind = graphics::shape_name(shape.kind).data(); // the names are literals
            out.a = shape.a;
            out.b = shape.b;
            out.c = shape.c;
            out.d = shape.d;
            out.size = shape.size;
            out.color = shape.color.c_str();
            out.text = shape.text.c_str();
            converted.push_back(out);
        }
        ThunderFrame frame{};
        frame.number = canvas.frames() + 1;
        frame.width = static_cast<std::int32_t>(canvas.width());
        frame.height = static_cast<std::int32_t>(canvas.height());
        frame.keep = static_cast<std::int32_t>(keep);
        frame.count = static_cast<std::int32_t>(converted.size());
        frame.shapes = converted.data();
        display_.present(&display_, &frame);
        drain_events();
    }

    void PluginDisplay::drain_events()
    {
        // The events go through the same Events object the IDE's record protocol feeds, so
        // key_pressed(), key_down() and the mouse built-ins need to know nothing about this.
        for (int guard = 0; guard < 1024; ++guard)
        {
            ThunderEvent event{};
            if (display_.poll(&display_, &event) == 0)
            {
                return;
            }
            const std::string key = event.key != nullptr ? std::string(event.key) : std::string();
            switch (event.kind)
            {
            case THUNDER_EVENT_KEY_DOWN:
                if (!key.empty())
                {
                    events_.apply("key down " + key);
                }
                break;
            case THUNDER_EVENT_KEY_UP:
                if (!key.empty())
                {
                    events_.apply("key up " + key);
                }
                break;
            case THUNDER_EVENT_MOUSE:
                events_.apply(std::format("mouse {} {} {}", event.x, event.y, event.button != 0 ? 1 : 0));
                break;
            case THUNDER_EVENT_QUIT:
                quit_ = true;
                return;
            default:
                break;
            }
        }
    }
}
