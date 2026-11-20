// The virtual machine's graphics built-ins (module "graphics") and random numbers.
//
// Drawing adds shapes to the canvas (graphics.hpp). frame() and show() present it to the graphics
// host - the IDE, through thundervm --canvas - and frame() then waits until the next frame is due,
// so an animation runs at the same speed on every computer. Without a host (headless runs and
// tests) nothing waits, and the final picture can be saved as SVG.

#include "thunder/vm.hpp"

#include <algorithm>
#include <cmath>
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
        if (value.is_enum() && value.as_enum().layout != nullptr
            && value.as_enum().layout->builtin == static_cast<std::int32_t>(BuiltinEnumId::Color))
        {
            // A member of the built-in Color enum: its English name is a colour name whatever the
            // language of the program.
            const auto& members = builtin_enum_info(BuiltinEnumId::Color).members;
            const std::uint16_t ordinal = value.as_enum().ordinal;
            if (ordinal < members.size())
            {
                if (const auto color = graphics::parse_color(members[ordinal]))
                {
                    return *color;
                }
            }
            fail(DiagId::UnknownColor, value.to_display(locale()));
        }
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
        if (host_ != nullptr && host_->should_stop())
        {
            throw FrameLimit{}; // the window was closed: the program has nowhere left to draw
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
                wait_until(next_frame_); // only this task waits for the next frame
            }
        }
    }

    std::int64_t Vm::int_within(const std::vector<Value>& args, std::size_t i, BuiltinId id, const char* what,
        std::int64_t low, std::int64_t high)
    {
        const std::int64_t value = int_arg(args, i);
        if (value < low || value > high)
        {
            fail(DiagId::ValueOutOfRange, language_->term(what), language_->spelling(id), low, high, value);
        }
        return value;
    }

    double Vm::number_within(const std::vector<Value>& args, std::size_t i, BuiltinId id, const char* what,
        double low, double high)
    {
        const double value = number_arg(args, i);
        if (!(value >= low) || value > high) // the negation also rejects a value that is not a number
        {
            fail(DiagId::ValueOutOfRange, language_->term(what), language_->spelling(id),
                graphics::number_text(low), graphics::number_text(high), graphics::number_text(value));
        }
        return value;
    }

    void Vm::add_shape(graphics::Shape shape)
    {
        if (!canvas_.add(std::move(shape)))
        {
            fail(DiagId::TooManyShapes, graphics::max_shapes);
        }
    }

    graphics::Events* Vm::events() const
    {
        return host_ != nullptr ? host_->events() : nullptr;
    }

    bool Vm::graphics_builtin(BuiltinId id, std::vector<Value>& args)
    {
        switch (id)
        {
        case BuiltinId::RandomInt:
            push(Value::from_int(random_between(int_arg(args, 0), int_arg(args, 1))));
            return true;
        case BuiltinId::Random:
            // 53 random bits scaled into [0, 1): every double in that range is possible.
            push(Value::from_float(static_cast<double>(next_random(random_state_) >> 11) / 9007199254740992.0));
            return true;
        case BuiltinId::SeedRandom:
            random_state_ = static_cast<std::uint64_t>(int_arg(args, 0));
            return true;
        case BuiltinId::Canvas:
        {
            const std::int64_t width = int_within(args, 0, id, "width", 1, graphics::max_size);
            const std::int64_t height = int_within(args, 1, id, "height", 1, graphics::max_size);
            canvas_.resize(width, height);
            return true;
        }
        case BuiltinId::Clear:
            canvas_.clear(color_argument(args[0]));
            return true;
        case BuiltinId::TextSize:
            canvas_.text_size = number_within(args, 0, id, "text size", 1, 400);
            return true;
        case BuiltinId::LineWidth:
            canvas_.line_width = number_within(args, 0, id, "line width", 0.1, 100);
            return true;
        case BuiltinId::Rgb:
            push(Value::from_string(graphics::rgb(int_arg(args, 0), int_arg(args, 1), int_arg(args, 2))));
            return true;
        case BuiltinId::Frame:
        case BuiltinId::Show:
            present(id == BuiltinId::Frame);
            return true;
        case BuiltinId::FrameRate:
            canvas_.frame_rate = int_within(args, 0, id, "frame rate", 1, 120);
            return true;
        default:
            return drawing_builtin(id, args) || event_builtin(id, args);
        }
    }

    /** The shapes. Coordinates and sizes are decimals: the checker widens whole numbers. */
    bool Vm::drawing_builtin(BuiltinId id, std::vector<Value>& args)
    {
        using graphics::Shape;
        using graphics::ShapeKind;
        const auto decimal = [&](std::size_t i) { return number_arg(args, i); };
        switch (id)
        {
        case BuiltinId::Pixel:
            add_shape(Shape{ShapeKind::Pixel, decimal(0), decimal(1), 0, 0, color_argument(args[2]), {}, 1});
            return true;
        case BuiltinId::Line:
            add_shape(Shape{ShapeKind::Line, decimal(0), decimal(1), decimal(2), decimal(3), color_argument(args[4]), {},
                canvas_.line_width});
            return true;
        case BuiltinId::Rect:
        case BuiltinId::RectOutline:
        {
            // A negative width or height draws the rectangle to the left of / above (x, y).
            const double width = decimal(2);
            const double height = decimal(3);
            const double x = decimal(0) + std::min(width, 0.0);
            const double y = decimal(1) + std::min(height, 0.0);
            add_shape(Shape{id == BuiltinId::Rect ? ShapeKind::Rect : ShapeKind::RectOutline, x, y, std::abs(width),
                std::abs(height), color_argument(args[4]), {}, canvas_.line_width});
            return true;
        }
        case BuiltinId::Circle:
        case BuiltinId::CircleOutline:
        {
            const double radius = number_within(args, 2, id, "radius", 0, 1'000'000);
            add_shape(Shape{id == BuiltinId::Circle ? ShapeKind::Circle : ShapeKind::CircleOutline, decimal(0),
                decimal(1), radius, 0, color_argument(args[3]), {}, canvas_.line_width});
            return true;
        }
        case BuiltinId::Text:
            add_shape(Shape{ShapeKind::Text, decimal(0), decimal(1), 0, 0, color_argument(args[3]), string_arg(args, 2),
                canvas_.text_size});
            return true;
        default:
            return false;
        }
    }

    /** Keys and the mouse, from the host's events (none without a host). */
    bool Vm::event_builtin(BuiltinId id, std::vector<Value>& args)
    {
        graphics::Events* source = events();
        switch (id)
        {
        case BuiltinId::KeyPressed:
            // Keys are named in the program's language ("left", or "کیڼ" in Pashto).
            push(Value::from_string(source != nullptr ? language_->key_name(source->next_key()) : std::string()));
            return true;
        case BuiltinId::KeyDown:
            push(Value::from_bool(source != nullptr && source->is_down(language_->english_key(string_arg(args, 0)))));
            return true;
        case BuiltinId::MouseX:
            push(Value::from_int(source != nullptr ? source->mouse_x() : 0));
            return true;
        case BuiltinId::MouseY:
            push(Value::from_int(source != nullptr ? source->mouse_y() : 0));
            return true;
        case BuiltinId::MouseDown:
            push(Value::from_bool(source != nullptr && source->mouse_down()));
            return true;
        default:
            return false;
        }
    }
}
