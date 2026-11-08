// The host side of the display interface: loading a display plug-in and showing the canvas on
// it. The C ABI the plug-ins see is include/thunder/display.h, and docs/display.md explains
// the whole interface.
#pragma once

#include "thunder/display.h"
#include "thunder/graphics.hpp"
#include "thunder/os.hpp"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace thunder
{
    // A graphics host backed by a plug-in library (thundervm --display sdl).
    class PluginDisplay final : public graphics::GraphicsHost
    {
    public:
        ~PluginDisplay() override;
        PluginDisplay(const PluginDisplay&) = delete;
        PluginDisplay& operator=(const PluginDisplay&) = delete;

        // Loads "thunder_display_<name>" (libthunder_display_sdl.so, ...), looking in
        // 'search_paths' and then wherever the operating system looks. The window is opened by
        // the first frame, when the canvas size is known. Returns why it could not be loaded.
        static std::expected<std::unique_ptr<PluginDisplay>, std::string> load(const std::string& name,
            const std::vector<std::string>& search_paths, std::string title);

        // The file a display plug-in called 'name' has, e.g. libthunder_display_sdl.so.
        static std::string library_name(const std::string& name);

        void present(const graphics::Canvas& canvas, std::size_t keep) override;
        bool paced() const override { return true; }
        graphics::Events* events() override { return &events_; }
        // True once the user has closed the window: the program then ends normally.
        bool should_stop() const override { return quit_; }

    private:
        PluginDisplay() = default;
        void drain_events();

        os::Library library_;
        ThunderDisplay display_{};
        std::string title_;
        graphics::Events events_;
        bool started_ = false; // thunder_display_init succeeded, so close() must be called
        bool opened_ = false;  // the window itself is open
        bool quit_ = false;
    };
}
