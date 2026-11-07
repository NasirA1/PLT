// The virtual machine's graphics built-ins (module "graphics") and random numbers.
//
// Drawing adds shapes to the canvas (graphics.hpp). frame() and show() present it to the graphics
// host - the IDE, through thundervm --canvas - and frame() then waits until the next frame is due,
// so an animation runs at the same speed on every computer. Without a host (headless runs and
// tests) nothing waits, and the final picture can be saved as SVG.

#include "thunder/vm.hpp"

#include <limits>
#include <thread>

namespace thunder
{
    namespace
    {
        // SplitMix64: small, fast and the same on every platform (std::uniform_int_distribution is
        // not), so a seeded program draws the same "random" numbers everywhere.
        std::uint64_t next_random(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }
    }

    std::int64_t Vm::random_between(std::int64_t low, std::int64_t high)
    {
        if (low > high)
        {
            fail(DiagId::EmptyRandomRange, low, high);
        }
        // Unsigned arithmetic: high - low + 1 may not fit in an int64 (it wraps to 0 for the full range).
        const std::uint64_t span = static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low) + 1;
        std::uint64_t value = next_random(random_state_);
        if (span != 0)
        {
            // Reject the few values that would make small results more likely than large ones.
            const std::uint64_t threshold = (0 - span) % span;
            while (value < threshold)
            {
                value = next_random(random_state_);
            }
            value %= span;
        }
        return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) + value);
    }

    std::string Vm::color_argument(const Value& value)
    {
        if (!value.is_string())
        {
            fail(DiagId::InvalidBytecode, "a color must be a string");
        }
        const auto color = graphics::parse_color(value.as_string());
        if (!color)
        {
            fail(DiagId::UnknownColor, value.as_string());
        }
        return *color;
    }

    void Vm::present(bool wait)
    {
        output_.flush(); // text written before the frame comes before it
        const std::size_t keep = canvas_.kept();
        if (host_ != nullptr)
        {
            host_->present(canvas_, keep);
        }
        canvas_.presented();
        steps_at_frame_ = stats_.steps; // the step limit counts the instructions between frames
        if (options_.max_frames != 0 && canvas_.frames() >= options_.max_frames)
        {
            throw FrameLimit{};
        }
        if (wait && host_ != nullptr && host_->paced())
        {
            const auto period = std::chrono::nanoseconds(1'000'000'000 / canvas_.frame_rate);
            const auto now = std::chrono::steady_clock::now();
            next_frame_ += period;
            if (next_frame_ < now)
            {
                next_frame_ = now; // the program fell behind: do not try to catch up
            }
            else
            {
                std::this_thread::sleep_until(next_frame_);
            }
        }
    }

    bool Vm::graphics_builtin(BuiltinId id, std::vector<Value>& args)
    {
        const auto number = [&](std::size_t i)
        {
            if (i >= args.size() || !args[i].is_int())
            {
                fail(DiagId::InvalidBytecode, "a graphics built-in expects an int");
            }
            return args[i].as_int();
        };
        // A setting such as the canvas width must lie in a range (R022).
        const auto within = [&](std::size_t i, const char* what, std::int64_t low, std::int64_t high)
        {
            const std::int64_t value = number(i);
            if (value < low || value > high)
            {
                fail(DiagId::ValueOutOfRange, language_->term(what), language_->spelling(id), low, high, value);
            }
            return value;
        };
        const auto add = [&](graphics::Shape shape)
        {
            if (!canvas_.add(std::move(shape)))
            {
                fail(DiagId::TooManyShapes, graphics::max_shapes);
            }
        };
        const auto events = [&]() -> graphics::Events*
        { return host_ != nullptr ? host_->events() : nullptr; };
        using graphics::Shape;
        using graphics::ShapeKind;

        switch (id)
        {
        case BuiltinId::RandomInt:
            push(Value::from_int(random_between(number(0), number(1))));
            return true;
        case BuiltinId::SeedRandom:
            random_state_ = static_cast<std::uint64_t>(number(0));
            return true;
        case BuiltinId::Canvas:
        {
            const std::int64_t width = within(0, "width", 1, graphics::max_size);
            const std::int64_t height = within(1, "height", 1, graphics::max_size);
            canvas_.resize(width, height);
            return true;
        }
        case BuiltinId::Clear:
            canvas_.clear(color_argument(args[0]));
            return true;
        case BuiltinId::Pixel:
            add(Shape{ShapeKind::Pixel, number(0), number(1), 0, 0, color_argument(args[2]), {}, 1});
            return true;
        case BuiltinId::Line:
            add(Shape{ShapeKind::Line, number(0), number(1), number(2), number(3), color_argument(args[4]), {},
                canvas_.line_width});
            return true;
        case BuiltinId::Rect:
        case BuiltinId::RectOutline:
        {
            // A negative width or height draws the rectangle to the left of / above (x, y).
            std::int64_t x = number(0);
            std::int64_t y = number(1);
            std::int64_t width = number(2);
            std::int64_t height = number(3);
            if (width < 0 && width > std::numeric_limits<std::int64_t>::min())
            {
                x += width;
                width = -width;
            }
            if (height < 0 && height > std::numeric_limits<std::int64_t>::min())
            {
                y += height;
                height = -height;
            }
            add(Shape{id == BuiltinId::Rect ? ShapeKind::Rect : ShapeKind::RectOutline, x, y, width, height,
                color_argument(args[4]), {}, canvas_.line_width});
            return true;
        }
        case BuiltinId::Circle:
        case BuiltinId::CircleOutline:
        {
            const std::int64_t radius = within(2, "radius", 0, 1'000'000);
            add(Shape{id == BuiltinId::Circle ? ShapeKind::Circle : ShapeKind::CircleOutline, number(0), number(1), radius,
                0, color_argument(args[3]), {}, canvas_.line_width});
            return true;
        }
        case BuiltinId::Text:
            if (!args[2].is_string())
            {
                fail(DiagId::InvalidBytecode, "text expects a string");
            }
            add(Shape{ShapeKind::Text, number(0), number(1), 0, 0, color_argument(args[3]), args[2].as_string(),
                canvas_.text_size});
            return true;
        case BuiltinId::TextSize:
            canvas_.text_size = within(0, "text size", 1, 400);
            return true;
        case BuiltinId::LineWidth:
            canvas_.line_width = within(0, "line width", 1, 100);
            return true;
        case BuiltinId::Rgb:
            push(Value::from_string(graphics::rgb(number(0), number(1), number(2))));
            return true;
        case BuiltinId::Frame:
        case BuiltinId::Show:
            present(id == BuiltinId::Frame);
            return true;
        case BuiltinId::FrameRate:
            canvas_.frame_rate = within(0, "frame rate", 1, 120);
            return true;
        case BuiltinId::KeyPressed:
            push(Value::from_string(events() != nullptr ? events()->next_key() : std::string()));
            return true;
        case BuiltinId::KeyDown:
            if (!args[0].is_string())
            {
                fail(DiagId::InvalidBytecode, "key_down expects a string");
            }
            push(Value::from_bool(events() != nullptr && events()->is_down(args[0].as_string())));
            return true;
        case BuiltinId::MouseX:
            push(Value::from_int(events() != nullptr ? events()->mouse_x() : 0));
            return true;
        case BuiltinId::MouseY:
            push(Value::from_int(events() != nullptr ? events()->mouse_y() : 0));
            return true;
        case BuiltinId::MouseDown:
            push(Value::from_bool(events() != nullptr && events()->mouse_down()));
            return true;
        default:
            return false;
        }
    }
}
