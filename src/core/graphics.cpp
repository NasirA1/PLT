#include "thunder/graphics.hpp"

#include "thunder/json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <format>
#include <utility>

namespace thunder::graphics
{
    namespace
    {
        struct NamedColor
        {
            std::string_view name;
            std::string_view hex;
        };

        // The 16 basic web colors plus a few that students ask for, and their Pashto names.
        constexpr std::array<NamedColor, 37> named_colors{{
            {"aqua", "#00ffff"}, {"black", "#000000"}, {"blue", "#0000ff"}, {"brown", "#8b4513"},
            {"cyan", "#00ffff"}, {"fuchsia", "#ff00ff"}, {"gold", "#ffd700"}, {"gray", "#808080"},
            {"green", "#008000"}, {"grey", "#808080"}, {"lime", "#00ff00"}, {"magenta", "#ff00ff"},
            {"maroon", "#800000"}, {"navy", "#000080"}, {"olive", "#808000"}, {"orange", "#ffa500"},
            {"pink", "#ffc0cb"}, {"purple", "#800080"}, {"red", "#ff0000"}, {"silver", "#c0c0c0"},
            {"sky", "#87ceeb"}, {"teal", "#008080"}, {"white", "#ffffff"}, {"yellow", "#ffff00"},
            // Pashto (drafts, like the rest of the Pashto vocabulary)
            {"تور", "#000000"}, {"سپین", "#ffffff"}, {"سور", "#ff0000"}, {"زرغون", "#008000"},
            {"آبي", "#0000ff"}, {"ژېړ", "#ffff00"}, {"نارنجي", "#ffa500"}, {"ګلابي", "#ffc0cb"},
            {"خړ", "#808080"}, {"نسواري", "#8b4513"}, {"بنفش", "#800080"}, {"اسماني", "#87ceeb"},
            {"طلايي", "#ffd700"},
        }};

        char lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool is_hex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }

        std::string escape_xml(std::string_view text)
        {
            std::string out;
            for (const char c : text)
            {
                switch (c)
                {
                case '<':
                    out += "&lt;";
                    break;
                case '>':
                    out += "&gt;";
                    break;
                case '&':
                    out += "&amp;";
                    break;
                case '"':
                    out += "&quot;";
                    break;
                default:
                    out += c;
                    break;
                }
            }
            return out;
        }
    }

    std::string_view shape_name(ShapeKind kind)
    {
        switch (kind)
        {
        case ShapeKind::Clear:
            return "clear";
        case ShapeKind::Pixel:
            return "pixel";
        case ShapeKind::Line:
            return "line";
        case ShapeKind::Rect:
            return "rect";
        case ShapeKind::RectOutline:
            return "rect_outline";
        case ShapeKind::Circle:
            return "circle";
        case ShapeKind::CircleOutline:
            return "circle_outline";
        case ShapeKind::Text:
            return "text";
        }
        return "?";
    }

    std::optional<std::string> parse_color(std::string_view text)
    {
        std::string key;
        for (const char c : text)
        {
            if (c != ' ')
            {
                key += lower(c);
            }
        }
        for (const NamedColor& named : named_colors)
        {
            if (named.name == key)
            {
                return std::string(named.hex);
            }
        }
        if (key.size() == 4 && key[0] == '#' && std::all_of(key.begin() + 1, key.end(), is_hex))
        {
            return std::string{'#', key[1], key[1], key[2], key[2], key[3], key[3]};
        }
        if (key.size() == 7 && key[0] == '#' && std::all_of(key.begin() + 1, key.end(), is_hex))
        {
            return key;
        }
        return std::nullopt;
    }

    std::string rgb(std::int64_t red, std::int64_t green, std::int64_t blue)
    {
        const auto clamp = [](std::int64_t v) { return static_cast<unsigned>(std::clamp<std::int64_t>(v, 0, 255)); };
        return std::format("#{:02x}{:02x}{:02x}", clamp(red), clamp(green), clamp(blue));
    }

    std::vector<std::string_view> color_names()
    {
        std::vector<std::string_view> names;
        for (const NamedColor& named : named_colors)
        {
            names.push_back(named.name);
        }
        return names;
    }

    // ------------------------------------------------------------------ canvas

    void Canvas::resize(std::int64_t width, std::int64_t height)
    {
        width_ = width;
        height_ = height;
        shapes_.clear();
        kept_ = 0;
        changed_ = true;
        used_ = true;
    }

    void Canvas::clear(const std::string& color)
    {
        shapes_.clear();
        shapes_.push_back(Shape{ShapeKind::Clear, 0, 0, 0, 0, color, {}, 1});
        kept_ = 0; // the host must start again
        changed_ = true;
        used_ = true;
    }

    bool Canvas::add(Shape shape)
    {
        if (shapes_.size() >= max_shapes)
        {
            return false;
        }
        shapes_.push_back(std::move(shape));
        changed_ = true;
        used_ = true;
        return true;
    }

    void Canvas::presented()
    {
        kept_ = shapes_.size();
        changed_ = false;
        ++frames_;
    }

    // ------------------------------------------------------------------ output formats

    std::string frame_json(const Canvas& canvas, std::size_t keep)
    {
        JsonWriter json;
        json.begin_object();
        json.field("type", "frame");
        json.field("n", canvas.frames() + 1);
        json.field("width", canvas.width());
        json.field("height", canvas.height());
        json.field("keep", static_cast<std::uint64_t>(keep));
        json.key("shapes").begin_array();
        const auto& shapes = canvas.shapes();
        for (std::size_t i = keep; i < shapes.size(); ++i)
        {
            const Shape& s = shapes[i];
            json.begin_array();
            json.value(shape_name(s.kind));
            switch (s.kind)
            {
            case ShapeKind::Clear:
                json.value(s.color);
                break;
            case ShapeKind::Pixel:
                json.value(s.a).value(s.b).value(s.color);
                break;
            case ShapeKind::Line:
            case ShapeKind::RectOutline:
                json.value(s.a).value(s.b).value(s.c).value(s.d).value(s.color).value(s.size);
                break;
            case ShapeKind::Rect:
                json.value(s.a).value(s.b).value(s.c).value(s.d).value(s.color);
                break;
            case ShapeKind::Circle:
                json.value(s.a).value(s.b).value(s.c).value(s.color);
                break;
            case ShapeKind::CircleOutline:
                json.value(s.a).value(s.b).value(s.c).value(s.color).value(s.size);
                break;
            case ShapeKind::Text:
                json.value(s.a).value(s.b).value(s.text).value(s.color).value(s.size);
                break;
            }
            json.end_array();
        }
        json.end_array();
        json.end_object();
        return json.str();
    }

    std::string to_svg(const Canvas& canvas)
    {
        std::string out = std::format(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\">\n"
            "<rect width=\"{0}\" height=\"{1}\" fill=\"#ffffff\"/>\n",
            canvas.width(), canvas.height());
        for (const Shape& s : canvas.shapes())
        {
            switch (s.kind)
            {
            case ShapeKind::Clear:
                out += std::format("<rect width=\"{}\" height=\"{}\" fill=\"{}\"/>\n", canvas.width(), canvas.height(), s.color);
                break;
            case ShapeKind::Pixel:
                out += std::format("<rect x=\"{}\" y=\"{}\" width=\"1\" height=\"1\" fill=\"{}\"/>\n", s.a, s.b, s.color);
                break;
            case ShapeKind::Line:
                out += std::format("<line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"{}\" stroke-width=\"{}\" "
                                   "stroke-linecap=\"round\"/>\n",
                    s.a, s.b, s.c, s.d, s.color, s.size);
                break;
            case ShapeKind::Rect:
                out += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"{}\"/>\n", s.a, s.b, s.c, s.d,
                    s.color);
                break;
            case ShapeKind::RectOutline:
                out += std::format("<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"none\" stroke=\"{}\" "
                                   "stroke-width=\"{}\"/>\n",
                    s.a, s.b, s.c, s.d, s.color, s.size);
                break;
            case ShapeKind::Circle:
                out += std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"{}\"/>\n", s.a, s.b, s.c, s.color);
                break;
            case ShapeKind::CircleOutline:
                out += std::format("<circle cx=\"{}\" cy=\"{}\" r=\"{}\" fill=\"none\" stroke=\"{}\" stroke-width=\"{}\"/>\n",
                    s.a, s.b, s.c, s.color, s.size);
                break;
            case ShapeKind::Text:
                // (x, y) is the top left corner; SVG places text by its baseline, about 4/5 of the size lower.
                // (dominant-baseline="hanging" would say this directly, but many viewers ignore it.)
                out += std::format("<text x=\"{}\" y=\"{}\" fill=\"{}\" font-size=\"{}\" font-family=\"sans-serif\">{}</text>\n",
                    s.a, s.b + s.size * 4 / 5, s.color, s.size, escape_xml(s.text));
                break;
            }
        }
        return out + "</svg>\n";
    }

    std::string to_text(const Canvas& canvas)
    {
        std::string out = std::format("canvas {} x {}, {} frame(s) shown\n", canvas.width(), canvas.height(), canvas.frames());
        for (const Shape& s : canvas.shapes())
        {
            out += std::string(shape_name(s.kind));
            switch (s.kind)
            {
            case ShapeKind::Clear:
                break;
            case ShapeKind::Pixel:
                out += std::format(" {} {}", s.a, s.b);
                break;
            case ShapeKind::Circle:
            case ShapeKind::CircleOutline:
                out += std::format(" {} {} {}", s.a, s.b, s.c);
                break;
            case ShapeKind::Text:
                out += std::format(" {} {} \"{}\"", s.a, s.b, s.text);
                break;
            default:
                out += std::format(" {} {} {} {}", s.a, s.b, s.c, s.d);
                break;
            }
            out += " " + s.color;
            if (s.kind == ShapeKind::Line || s.kind == ShapeKind::RectOutline || s.kind == ShapeKind::CircleOutline
                || s.kind == ShapeKind::Text)
            {
                out += std::format(" ({})", s.size);
            }
            out += "\n";
        }
        return out;
    }

    // ------------------------------------------------------------------ events

    namespace
    {
        std::optional<std::int64_t> number(std::string_view text)
        {
            std::int64_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                return std::nullopt;
            }
            return value;
        }

        std::vector<std::string_view> words(std::string_view line)
        {
            std::vector<std::string_view> out;
            std::size_t pos = 0;
            while (pos < line.size())
            {
                const std::size_t start = line.find_first_not_of(' ', pos);
                if (start == std::string_view::npos)
                {
                    break;
                }
                const std::size_t end = line.find(' ', start);
                out.push_back(line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
                pos = end == std::string_view::npos ? line.size() : end;
            }
            return out;
        }
    }

    bool Events::apply(std::string_view line)
    {
        const std::vector<std::string_view> parts = words(line);
        if (parts.size() == 3 && parts[0] == "key" && (parts[1] == "down" || parts[1] == "up"))
        {
            const std::lock_guard lock(mutex_);
            const std::string key(parts[2]);
            if (parts[1] == "down")
            {
                presses_.push_back(key);
                if (presses_.size() > 64)
                {
                    presses_.pop_front(); // a program that never asks must not make the queue grow forever
                }
                down_.insert(key);
            }
            else
            {
                down_.erase(key);
            }
            return true;
        }
        if (parts.size() == 4 && parts[0] == "mouse")
        {
            const auto x = number(parts[1]);
            const auto y = number(parts[2]);
            const auto pressed = number(parts[3]);
            if (!x || !y || !pressed)
            {
                return false;
            }
            const std::lock_guard lock(mutex_);
            mouse_x_ = *x;
            mouse_y_ = *y;
            mouse_down_ = *pressed != 0;
            return true;
        }
        return false;
    }

    std::string Events::next_key()
    {
        const std::lock_guard lock(mutex_);
        if (presses_.empty())
        {
            return {};
        }
        std::string key = std::move(presses_.front());
        presses_.pop_front();
        return key;
    }

    bool Events::is_down(const std::string& key) const
    {
        const std::lock_guard lock(mutex_);
        return down_.contains(key);
    }

    std::int64_t Events::mouse_x() const
    {
        const std::lock_guard lock(mutex_);
        return mouse_x_;
    }

    std::int64_t Events::mouse_y() const
    {
        const std::lock_guard lock(mutex_);
        return mouse_y_;
    }

    bool Events::mouse_down() const
    {
        const std::lock_guard lock(mutex_);
        return mouse_down_;
    }

    // ------------------------------------------------------------------ event reader

    EventReader::EventReader(LineReader next_line, std::shared_ptr<Events> events)
        : state_(std::make_shared<State>())
    {
        state_->events = std::move(events);
        thread_ = std::thread([next_line = std::move(next_line), state = state_] { run(next_line, state); });
    }

    EventReader::EventReader(std::istream& input, std::shared_ptr<Events> events)
        : EventReader([&input](std::string& line) { return static_cast<bool>(std::getline(input, line)); },
              std::move(events))
    {
    }

    EventReader::~EventReader()
    {
        // A stream that is about to end (a file, a test's string) finishes almost at once; wait a
        // moment for that, so the thread never outlives such a stream.
        std::unique_lock lock(state_->mutex);
        const bool finished = state_->ready.wait_for(lock, std::chrono::milliseconds(200), [this] { return state_->finished; });
        lock.unlock();
        if (finished)
        {
            thread_.join();
        }
        else
        {
            thread_.detach(); // still waiting for input that may never come (the terminal or the IDE)
        }
    }

    void EventReader::run(const LineReader& next_line, const std::shared_ptr<State>& state)
    {
        std::string line;
        while (next_line(line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (!line.empty() && line.front() == record_separator)
            {
                state->events->apply(std::string_view(line).substr(1));
                continue;
            }
            const std::lock_guard lock(state->mutex);
            state->lines.push_back(std::move(line));
            state->ready.notify_all();
        }
        const std::lock_guard lock(state->mutex);
        state->finished = true;
        state->ready.notify_all();
    }

    std::optional<std::string> EventReader::read_line()
    {
        std::unique_lock lock(state_->mutex);
        state_->ready.wait(lock, [this] { return !state_->lines.empty() || state_->finished; });
        if (state_->lines.empty())
        {
            return std::nullopt;
        }
        std::string line = std::move(state_->lines.front());
        state_->lines.pop_front();
        return line;
    }

    bool EventReader::at_end()
    {
        std::unique_lock lock(state_->mutex);
        state_->ready.wait(lock, [this] { return !state_->lines.empty() || state_->finished; });
        return state_->lines.empty();
    }

    // ------------------------------------------------------------------ hosts

    void StreamHost::present(const Canvas& canvas, std::size_t keep)
    {
        out_ << record_separator << frame_json(canvas, keep) << '\n';
        out_.flush();
    }
}
