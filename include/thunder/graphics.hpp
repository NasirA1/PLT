// 2D graphics: the canvas a program draws on (module "graphics"), and how its frames reach a display.
//
// The canvas is a list of shapes: everything drawn since the last clear(), in order. frame() and
// show() *present* the canvas to a GraphicsHost - the IDE (through thundervm --canvas), or a test.
// Without a host, drawing still works: the final picture can be saved as SVG (thundervm --svg).
//
// The host protocol (thundervm --canvas) is line based and travels with the program's normal
// input and output:
//   * each presented frame is one line on standard output: "\x1e" + a JSON object + "\n";
//   * key and mouse events are lines on standard input: "\x1e" + "key down left", "key up left",
//     "mouse 120 45 1" (x, y, button pressed). Other input lines are what input() reads.
// \x1e is the ASCII "record separator", which ordinary text never contains.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <istream>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace thunder::graphics
{
    inline constexpr char record_separator = '\x1e';
    inline constexpr std::int64_t default_width = 400;
    inline constexpr std::int64_t default_height = 300;
    inline constexpr std::int64_t max_size = 2000;           // pixels, in each direction
    inline constexpr std::size_t max_shapes = 100'000;       // shapes since the last clear()
    inline constexpr std::int64_t default_frame_rate = 30;   // frames per second

    enum class ShapeKind : std::uint8_t
    {
        Clear,         // color
        Pixel,         // x, y, color
        Line,          // x1, y1, x2, y2, color, line width
        Rect,          // x, y, width, height, color (filled)
        RectOutline,   // x, y, width, height, color, line width
        Circle,        // x, y, radius, color (filled)
        CircleOutline, // x, y, radius, color, line width
        Text,          // x, y (top left), text, color, size
    };

    std::string_view shape_name(ShapeKind kind); // "rect", "circle_outline", ...

    // Coordinates and sizes are decimals, so a program can place a shape at sin(angle) * radius
    // without rounding first. Whole values are written back as whole numbers (see number_text).
    struct Shape
    {
        ShapeKind kind = ShapeKind::Clear;
        double a = 0, b = 0, c = 0, d = 0; // coordinates and sizes, as listed for ShapeKind
        std::string color;                 // "#rrggbb"
        std::string text;                  // for Text
        double size = 1;                   // line width, or text size
    };

    // A coordinate as text: a whole number without a fractional part ("10"), anything else with
    // up to three decimals and no trailing zeros ("10.5"). Keeps frames and pictures compact and
    // makes the output of a program that only uses whole numbers look exactly as it always did.
    std::string number_text(double value);

    // "red", "#f80", "#ff8800" (any case) -> "#ff8800"; nullopt if the text is not a color.
    std::optional<std::string> parse_color(std::string_view text);
    // The color with these components (each clamped to 0-255), e.g. rgb(255, 136, 0) -> "#ff8800".
    std::string rgb(std::int64_t red, std::int64_t green, std::int64_t blue);
    // The names parse_color accepts, sorted.
    std::vector<std::string_view> color_names();

    class Canvas
    {
    public:
        std::int64_t width() const { return width_; }
        std::int64_t height() const { return height_; }
        const std::vector<Shape>& shapes() const { return shapes_; }
        bool used() const { return used_; } // true once the program has drawn or created a canvas

        void resize(std::int64_t width, std::int64_t height); // also clears to white
        void clear(const std::string& color);
        bool add(Shape shape); // false if the canvas already holds max_shapes shapes

        // Presenting: the host already has the first 'kept()' shapes of the current list (all of
        // them, unless the canvas was cleared since the last frame).
        std::size_t kept() const { return kept_; }
        bool changed() const { return changed_; } // something to present?
        void presented();
        std::uint64_t frames() const { return frames_; }

        double line_width = 1;
        double text_size = 16;
        std::int64_t frame_rate = default_frame_rate;

    private:
        std::int64_t width_ = default_width;
        std::int64_t height_ = default_height;
        std::vector<Shape> shapes_;
        std::size_t kept_ = 0;
        bool changed_ = false;
        bool used_ = false;
        std::uint64_t frames_ = 0;
    };

    // One frame for the host protocol: {"type":"frame","n":..,"width":..,"height":..,"keep":..,"shapes":[...]}
    // with the shapes from index 'keep' on. Each shape is an array: ["rect", x, y, w, h, "#rrggbb"], ...
    std::string frame_json(const Canvas& canvas, std::size_t keep);
    // The canvas as an SVG image.
    std::string to_svg(const Canvas& canvas);
    // The canvas as readable text, one shape per line (used by the golden tests).
    std::string to_text(const Canvas& canvas);

    // Key and mouse state, fed by the host's events. Safe to use from two threads.
    class Events
    {
    public:
        // Applies one event line (without the record separator); false if it is not an event.
        bool apply(std::string_view line);

        std::string next_key();               // the oldest key press not yet taken, or ""
        bool is_down(const std::string& key) const;
        std::int64_t mouse_x() const;
        std::int64_t mouse_y() const;
        bool mouse_down() const;

    private:
        mutable std::mutex mutex_;
        std::deque<std::string> presses_;
        std::set<std::string> down_;
        std::int64_t mouse_x_ = 0;
        std::int64_t mouse_y_ = 0;
        bool mouse_down_ = false;
    };

    // Where input() lines come from when they share the input stream with events.
    class LineSource
    {
    public:
        virtual ~LineSource() = default;
        virtual std::optional<std::string> read_line() = 0; // nullopt at the end of the input
        virtual bool at_end() = 0;                          // waits until this is known
    };

    // Reads an input stream on a background thread: event lines go to 'events', the other lines
    // are what read_line() returns. A blocking read cannot be interrupted portably, so if the input
    // has not ended when the reader is destroyed, the thread is left to finish on its own (it owns
    // what it uses, except the stream, which must outlive it - as std::cin does).
    class EventReader final : public LineSource
    {
    public:
        // Reads the lines with 'next_line', which returns false at the end of the input.
        using LineReader = std::function<bool(std::string& line)>;
        EventReader(LineReader next_line, std::shared_ptr<Events> events);
        EventReader(std::istream& input, std::shared_ptr<Events> events);
        ~EventReader() override;
        EventReader(const EventReader&) = delete;
        EventReader& operator=(const EventReader&) = delete;

        std::optional<std::string> read_line() override;
        bool at_end() override;

    private:
        struct State
        {
            std::mutex mutex;
            std::condition_variable ready;
            std::deque<std::string> lines;
            bool finished = false;
            std::shared_ptr<Events> events;
        };
        static void run(const LineReader& next_line, const std::shared_ptr<State>& state);

        std::shared_ptr<State> state_;
        std::thread thread_;
    };

    // A display for the canvas.
    class GraphicsHost
    {
    public:
        virtual ~GraphicsHost() = default;
        // Shows the canvas; the host already has the shapes before index 'keep'.
        virtual void present(const Canvas& canvas, std::size_t keep) = 0;
        // True if frames are watched in real time, so frame() should wait for the next frame.
        virtual bool paced() const { return false; }
        virtual Events* events() { return nullptr; }
        // True once the host cannot show any more frames - a window the user has closed. The
        // program then ends the way it does when the frame limit is reached: normally.
        virtual bool should_stop() const { return false; }
    };

    // The host protocol on a stream (thundervm --canvas): writes each frame as a record line.
    class StreamHost final : public GraphicsHost
    {
    public:
        StreamHost(std::ostream& out, Events* events)
            : out_(out)
            , events_(events)
        {
        }
        void present(const Canvas& canvas, std::size_t keep) override;
        bool paced() const override { return true; }
        Events* events() override { return events_; }

    private:
        std::ostream& out_;
        Events* events_;
    };
}
