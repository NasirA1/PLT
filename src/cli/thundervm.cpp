// thundervm - the Thunder virtual machine.
//
// Loads a bytecode file (.thc) produced by thunderc, verifies it and executes it.

#include "thunder/cli/cli_common.hpp"

#include "thunder/bytecode.hpp"
#include "thunder/graphics.hpp"
#include "thunder/json.hpp"
#include "thunder/os.hpp"
#include "thunder/package.hpp"
#include "thunder/report.hpp"
#include "thunder/vm.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace thunder;

    constexpr std::string_view usage = R"(Usage: thundervm [options] <program.thc> [arguments...]

Runs a Thunder bytecode file produced by thunderc. The program reads its input (input()) from
standard input; the arguments after the program's name are what args() returns (import os).

Options:
  --trace               print every instruction and the operand stack (to standard error)
  --trace-json <file>   write a JSON execution trace for step-by-step debugging
  --trace-limit <n>     maximum number of steps recorded in the JSON trace (default: 10000)
  --result-json <file>  write the outcome (status, error, statistics) as JSON
  --max-steps <n>       stop with an error after n instructions (default: unlimited)
  --max-depth <n>       maximum depth of nested calls (default: 10000)
  --stats               print execution statistics (to standard error)
  --canvas              show graphics in a host such as the IDE: frames are written to standard
                        output as records, key and mouse events are read from standard input
                        (see docs/graphics.md), and frame() waits for the next frame
  --svg <file>          save the final picture of a graphics program as an SVG image
  --max-frames <n>      stop the program after n frames have been shown
  --seed <n>            start random_int from this seed (the same numbers on every run)
  --native-path <dir>   also look there for the libraries of "import native"; may be repeated
                        (they are first looked for next to the program, then in this program's
                        own directory, then wherever the operating system looks)
  --no-native           refuse to load native libraries (a program that imports one stops with
                        runtime error R028)
  --disassemble         print the program listing instead of running it
  --version             print the version
  -h, --help            show this help

Exit status: 0 success (or the code given to exit()), 1 runtime error, 2 usage error or
invalid bytecode file.
)";

    int fail_usage(const std::string& message)
    {
        std::println(std::cerr, "thundervm: {}", message);
        std::println(std::cerr, "Try 'thundervm --help' for more information.");
        return cli::exit_usage;
    }

    // Forwards VM events to several observers.
    class ObserverList final : public VmObserver
    {
    public:
        void add(VmObserver* observer) { observers_.push_back(observer); }
        bool empty() const { return observers_.empty(); }

        void before_instruction(const Vm& vm, const bytecode::Instruction& instruction) override
        {
            for (VmObserver* observer : observers_)
            {
                observer->before_instruction(vm, instruction);
            }
        }

        void on_output(std::string_view text) override
        {
            for (VmObserver* observer : observers_)
            {
                observer->on_output(text);
            }
        }

    private:
        std::vector<VmObserver*> observers_;
    };
}

namespace
{
    struct Options
    {
        std::string trace_json_path;
        std::string result_json_path;
        std::uint64_t trace_limit = 10'000;
        bool text_trace = false;
        bool stats = false;
        bool disassemble = false;
        bool canvas = false;
        std::string svg_path;
        std::vector<std::string> native_paths;
        bool no_native = false;
        bool help = false;
        bool version = false;
        VmOptions vm;
    };

    std::vector<cli::OptionSpec> option_table(Options& options)
    {
        const auto flag = [](bool& target)
        {
            return [&target](const std::string&) -> std::optional<std::string>
            {
                target = true;
                return std::nullopt;
            };
        };
        const auto path = [](std::string& target)
        {
            return [&target](const std::string& value) -> std::optional<std::string>
            {
                target = value;
                return std::nullopt;
            };
        };
        const auto count = [](const char* name, auto setter, bool positive)
        {
            return [name, setter, positive](const std::string& value) -> std::optional<std::string>
            {
                const auto number = cli::parse_count(value);
                if (!number || (positive && *number == 0))
                {
                    return std::string(name) + (positive ? " expects a positive number" : " expects a number");
                }
                setter(*number);
                return std::nullopt;
            };
        };
        return {
            {"--help", "-h", false, flag(options.help)},
            {"--version", "", false, flag(options.version)},
            {"--trace", "", false, flag(options.text_trace)},
            {"--trace-json", "", true, path(options.trace_json_path)},
            {"--result-json", "", true, path(options.result_json_path)},
            {"--trace-limit", "", true, count("--trace-limit", [&](std::uint64_t n) { options.trace_limit = n; }, false)},
            {"--max-steps", "", true, count("--max-steps", [&](std::uint64_t n) { options.vm.max_steps = n; }, false)},
            {"--max-depth", "", true,
                count("--max-depth", [&](std::uint64_t n) { options.vm.max_call_depth = static_cast<std::size_t>(n); }, true)},
            {"--stats", "", false, flag(options.stats)},
            {"--canvas", "", false, flag(options.canvas)},
            {"--svg", "", true, path(options.svg_path)},
            {"--max-frames", "", true, count("--max-frames", [&](std::uint64_t n) { options.vm.max_frames = n; }, true)},
            {"--seed", "", true, count("--seed", [&](std::uint64_t n) { options.vm.random_seed = n; }, false)},
            {"--disassemble", "", false, flag(options.disassemble)},
            {"--no-native", "", false, flag(options.no_native)},
            {"--native-path", "", true, [&](const std::string& value) -> std::optional<std::string>
                {
                    options.native_paths.push_back(value);
                    return std::nullopt;
                }},
        };
    }

    // Where "import native" looks, in order: next to the program, next to this executable
    // (where a packaged program keeps its libraries), then the --native-path directories.
    std::vector<std::string> native_search_paths(const std::string& program_path,
        const std::vector<std::string>& extra)
    {
        std::vector<std::string> paths;
        const auto add = [&paths](const std::string& directory)
        {
            if (!directory.empty() && std::ranges::find(paths, directory) == paths.end())
            {
                paths.push_back(directory);
            }
        };
        add(os::path_text(os::utf8_path(program_path).parent_path()));
        add(os::path_text(os::utf8_path(os::executable_path()).parent_path()));
        for (const std::string& directory : extra)
        {
            add(directory);
        }
        // THUNDER_NATIVE_PATH works like PATH: directories separated by ';' (Windows) or ':'.
        const std::string variable = os::environment_variable("THUNDER_NATIVE_PATH");
        const char separator = os::platform_name() == "windows" ? ';' : ':';
        std::size_t start = 0;
        while (start <= variable.size() && !variable.empty())
        {
            const std::size_t end = variable.find(separator, start);
            add(variable.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        return paths;
    }

    std::string result_json(const ExecResult& result)
    {
        JsonWriter json;
        json.begin_object();
        json.field("ok", result.ok);
        json.field("exit_code", static_cast<std::int64_t>(result.exit_code));
        json.field("frame_limit_reached", result.frame_limit_reached);
        json.key("error");
        if (result.error)
        {
            write_runtime_error_json(json, *result.error);
        }
        else
        {
            json.null();
        }
        json.key("stats");
        write_stats_json(json, result.stats);
        json.end_object();
        return json.str();
    }
}

namespace
{
    // Runs 'program'. 'input' names where it came from (for messages and the native search
    // path) and 'program_arguments' is what args() returns.
    int execute_program(const bytecode::Program& program, Options& options, const std::string& input,
        std::vector<std::string> program_arguments)
    {
        if (options.disassemble)
        {
            std::print("{}", bytecode::disassemble_text(program));
            return cli::exit_ok;
        }

        ObserverList observers;
        TextTracer text_tracer(std::cerr);
        std::unique_ptr<TraceRecorder> recorder;
        if (options.text_trace)
        {
            observers.add(&text_tracer);
        }
        if (!options.trace_json_path.empty())
        {
            recorder = std::make_unique<TraceRecorder>(options.trace_limit);
            observers.add(recorder.get());
        }

        // "import native" looks next to the program first, then in this executable's directory
        // (a packaged program carries its libraries there), then where --native-path says.
        options.vm.allow_native = !options.no_native;
        options.vm.native_paths = native_search_paths(input, options.native_paths);

        Vm vm(program, std::cout, options.vm, &std::cin);
        vm.set_arguments(std::move(program_arguments));

        // --canvas: frames go to standard output, and standard input carries both the program's
        // input lines and key/mouse events (read on a background thread, so a game never waits).
        std::shared_ptr<graphics::Events> events;
        std::unique_ptr<graphics::EventReader> reader;
        std::unique_ptr<graphics::StreamHost> host;
        if (options.canvas)
        {
            events = std::make_shared<graphics::Events>();
            // (Not std::getline(std::cin): a thread blocked there would stop the program ending.)
            reader = std::make_unique<graphics::EventReader>(os::read_standard_input_line, events);
            host = std::make_unique<graphics::StreamHost>(std::cout, events.get());
            vm.set_line_source(reader.get());
            vm.set_graphics_host(host.get());
        }
        const ExecResult result = vm.run(observers.empty() ? nullptr : &observers);

        if (recorder && !cli::write_file(options.trace_json_path, recorder->finish(result)))
        {
            return fail_usage("cannot write '" + options.trace_json_path + "'");
        }
        if (!options.svg_path.empty() && !cli::write_file(options.svg_path, graphics::to_svg(vm.canvas())))
        {
            return fail_usage("cannot write '" + options.svg_path + "'");
        }
        if (!options.result_json_path.empty() && !cli::write_file(options.result_json_path, result_json(result)))
        {
            return fail_usage("cannot write '" + options.result_json_path + "'");
        }

        if (result.error)
        {
            std::cout.flush();
            std::println(std::cerr, "{}", format_runtime_error(*result.error, program.source_name, vm.language()));
        }
        if (options.stats)
        {
            std::println(std::cerr, "[thundervm] steps={} calls={} max_stack_depth={} max_call_depth={}",
                result.stats.steps, result.stats.calls, result.stats.max_stack_depth, result.stats.max_call_depth);
        }
        return result.ok ? result.exit_code : cli::exit_program_error;
    }

    constexpr std::string_view packaged_usage = R"(This is a Thunder program packaged as a native executable.

Everything on the command line is passed to the program (args(), module os). A few options of
the virtual machine itself are still available, each spelled with a "--thunder-" prefix:

  --thunder-help        show this help
  --thunder-version     print the version of the virtual machine inside it
  --thunder-svg <file>  save the final picture of a graphics program as an SVG image
  --thunder-canvas      write frames as records on standard output (for a host such as the IDE)
  --thunder-stats       print execution statistics
)";

    // A packaged program: the executable carries its own bytecode (see docs/packaging.md).
    int run_packaged(const std::vector<std::uint8_t>& bytes, const std::vector<std::string>& arguments)
    {
        auto program = bytecode::deserialize(bytes);
        if (!program)
        {
            std::println(std::cerr, "{}: the program inside this file is damaged: {}", os::executable_path(),
                program.error());
            return cli::exit_usage;
        }

        Options options;
        std::vector<std::string> program_arguments;
        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            const std::string& argument = arguments[i];
            const auto value = [&](const char* name) -> std::optional<std::string>
            {
                if (i + 1 >= arguments.size())
                {
                    std::println(std::cerr, "{} needs a file name", name);
                    return std::nullopt;
                }
                return arguments[++i];
            };
            if (argument == "--thunder-help")
            {
                std::print("{}", packaged_usage);
                return cli::exit_ok;
            }
            if (argument == "--thunder-version")
            {
                std::println("thundervm {}", THUNDER_VERSION);
                return cli::exit_ok;
            }
            if (argument == "--thunder-canvas")
            {
                options.canvas = true;
            }
            else if (argument == "--thunder-stats")
            {
                options.stats = true;
            }
            else if (argument == "--thunder-svg")
            {
                const auto path = value("--thunder-svg");
                if (!path)
                {
                    return cli::exit_usage;
                }
                options.svg_path = *path;
            }
            else
            {
                program_arguments.push_back(argument); // everything else belongs to the program
            }
        }
        return execute_program(*program, options, os::executable_path(), std::move(program_arguments));
    }
}

int main(int argc, char* argv[])
{
    cli::prepare_console();

    // A packaged program is this executable with a program stuck on the end: run that instead
    // of expecting a .thc on the command line.
    if (const auto embedded = package::self_payload())
    {
        return run_packaged(*embedded, std::vector<std::string>(argv + 1, argv + argc));
    }

    Options options;
    std::vector<std::string> positional;
    if (auto error = cli::parse_arguments(std::vector<std::string>(argv + 1, argv + argc), option_table(options),
            positional, true))
    {
        return fail_usage(*error);
    }
    if (options.help)
    {
        std::print("{}", usage);
        return cli::exit_ok;
    }
    if (options.version)
    {
        std::println("thundervm {}", THUNDER_VERSION);
        return cli::exit_ok;
    }
    const std::string input = positional.empty() ? std::string() : positional.front();

    if (input.empty())
    {
        return fail_usage("no program given");
    }
    const auto data = cli::read_file(input);
    if (!data)
    {
        return fail_usage("cannot read '" + input + "'");
    }

    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(data->data()), data->size());
    auto program = bytecode::deserialize(bytes);
    if (!program)
    {
        std::println(std::cerr, "thundervm: {}: {}", input, program.error());
        if (!data->empty() && (*data)[0] != 'T')
        {
            std::println(std::cerr, "(thundervm runs compiled .thc files; compile source code with thunderc first)");
        }
        return cli::exit_usage;
    }

    return execute_program(*program, options, input,
        std::vector<std::string>(positional.begin() + 1, positional.end()));
}
