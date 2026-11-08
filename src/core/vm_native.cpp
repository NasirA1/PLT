// The virtual machine's side of the C++ native interface (import native "name").
//
// A native function is an ordinary C function in a shared library. The VM converts the
// arguments on its stack into the small ThunderValue struct of include/thunder/native.h, calls
// the function, and converts the result back. Native code can call back into Thunder through
// thunder_call, which runs the Thunder function on this same VM.
//
// Nothing is thrown across the C boundary: a native function that fails (or whose callback
// fails) leaves the error in native_failure_, and the VM raises it after the call returns.

#include "thunder/vm.hpp"

#include <cstring>

namespace
{
    using thunder::Value;

    // The C API every native module receives. One instance, shared by every program: it holds
    // no state of its own, everything comes through the env.
    thunder::Vm* vm_of(ThunderEnv* env)
    {
        return env != nullptr ? env->vm : nullptr;
    }
}

namespace thunder
{
    namespace
    {
        // A Thunder value as the native side sees it. Strings point into 'value', which must
        // outlive the call (the arguments do: they stay on the VM's argument vector).
        ThunderValue to_native(const Value& value, std::int32_t callback_slot)
        {
            ThunderValue out = thunder_void();
            switch (value.kind())
            {
            case Value::Kind::Int:
                return thunder_int(value.as_int());
            case Value::Kind::Float:
                return thunder_float(value.as_float());
            case Value::Kind::Bool:
                return thunder_bool(value.as_bool() ? 1 : 0);
            case Value::Kind::String:
                return thunder_text(value.as_string().data(), static_cast<std::int64_t>(value.as_string().size()));
            case Value::Kind::Function:
                out.kind = THUNDER_FUNCTION;
                out.function = callback_slot;
                return out;
            default:
                return out;
            }
        }
    }

    // ------------------------------------------------------------------ the C API

    namespace
    {
        ThunderValue api_call(ThunderEnv* env, ThunderValue function, const ThunderValue* args, std::int32_t count);

        void api_fail(ThunderEnv* env, const char* message)
        {
            Vm* vm = vm_of(env);
            if (vm != nullptr)
            {
                vm->native_fail(message != nullptr ? message : "the native function failed");
            }
        }

        int api_failed(const ThunderEnv* env)
        {
            const Vm* vm = env != nullptr ? env->vm : nullptr;
            return vm != nullptr && vm->native_has_failed() ? 1 : 0;
        }

        const ThunderApi& host_api()
        {
            static const ThunderApi api{THUNDER_NATIVE_ABI, &api_call, &api_fail, &api_failed};
            return api;
        }
    }

    // ------------------------------------------------------------------ loading

    void Vm::load_native_libraries()
    {
        if (program_.native_libraries.empty() && program_.natives.empty())
        {
            return;
        }
        if (!options_.allow_native)
        {
            fail(DiagId::NativeLibraryNotFound, program_.native_libraries.empty() ? std::string("?")
                                                                                  : program_.native_libraries.front(),
                "native libraries are switched off (thundervm --no-native)");
        }
        natives_ = std::make_unique<NativeModules>();
        for (const std::string& library : program_.native_libraries)
        {
            if (const auto error = natives_->load(library, options_.native_paths, host_api()))
            {
                fail(DiagId::NativeLibraryNotFound, library, *error);
            }
        }
        native_functions_.clear();
        for (const bytecode::NativeFunction& native : program_.natives)
        {
            ThunderFunction function = natives_->find(native.name);
            if (function == nullptr)
            {
                fail(DiagId::NativeFunctionNotFound, native.name);
            }
            native_functions_.push_back(function);
        }
    }

    // ------------------------------------------------------------------ calling out

    void Vm::call_native(std::uint32_t index, VmObserver* observer)
    {
        if (index >= program_.natives.size() || index >= native_functions_.size())
        {
            fail(DiagId::InvalidBytecode, "CALL_NATIVE refers to a native function that does not exist");
        }
        const bytecode::NativeFunction& native = program_.natives[index];
        std::vector<Value> arguments = pop_values(native.arity);

        // Function arguments are kept here so a callback can find them again by slot.
        const std::size_t callbacks_before = native_callbacks_.size();
        std::vector<ThunderValue> converted;
        converted.reserve(arguments.size());
        for (const Value& argument : arguments)
        {
            std::int32_t slot = -1;
            if (argument.is_function())
            {
                slot = static_cast<std::int32_t>(native_callbacks_.size());
                native_callbacks_.push_back(argument);
            }
            converted.push_back(to_native(argument, slot));
        }

        ThunderEnv env{this};
        VmObserver* previous_observer = native_observer_;
        native_observer_ = observer;
        const ThunderValue result =
            native_functions_[index](&env, converted.data(), static_cast<std::int32_t>(converted.size()));
        native_observer_ = previous_observer;
        native_callbacks_.resize(callbacks_before);

        if (native_failure_)
        {
            const Failure failure = *native_failure_;
            native_failure_.reset();
            throw failure;
        }

        if (!native.returns_value)
        {
            return;
        }
        // The declared type decides what is pushed, so a module that returns the wrong kind
        // cannot put a value of a surprising type into a typed program.
        switch (native.result_kind)
        {
        case THUNDER_INT:
            push(Value::from_int(result.kind == THUNDER_FLOAT ? static_cast<std::int64_t>(result.number)
                                                              : result.integer));
            break;
        case THUNDER_FLOAT:
            push(Value::from_float(result.kind == THUNDER_INT ? static_cast<double>(result.integer) : result.number));
            break;
        case THUNDER_BOOL:
            push(Value::from_bool(result.integer != 0));
            break;
        case THUNDER_STRING:
        {
            // The text is copied now: the module may free it as soon as it returns.
            const char* text = result.kind == THUNDER_STRING ? result.text : nullptr;
            const auto length = result.kind == THUNDER_STRING && result.text_length > 0
                ? static_cast<std::size_t>(result.text_length)
                : std::size_t{0};
            push(Value::from_string(text != nullptr ? std::string(text, length) : std::string()));
            break;
        }
        default:
            fail(DiagId::InvalidBytecode, "a native function declared a result type that cannot be returned");
        }
    }

    // ------------------------------------------------------------------ calling back

    void Vm::native_fail(const std::string& message)
    {
        if (!native_failure_)
        {
            native_failure_ = Failure{DiagId::NativeError, language_->format(DiagId::NativeError, {message})};
        }
    }

    bool Vm::native_has_failed() const
    {
        return native_failure_.has_value();
    }

    Value Vm::native_callback(std::int32_t slot, const std::vector<Value>& arguments)
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= native_callbacks_.size())
        {
            native_fail("the value passed to thunder_call is not a Thunder function");
            return Value{};
        }
        try
        {
            return invoke(native_callbacks_[static_cast<std::size_t>(slot)], arguments, native_observer_);
        }
        catch (const Failure& failure)
        {
            // The error cannot be thrown through the native C frame: remember it instead.
            if (!native_failure_)
            {
                native_failure_ = failure;
            }
            return Value{};
        }
    }

    namespace
    {
        ThunderValue api_call(ThunderEnv* env, ThunderValue function, const ThunderValue* args, std::int32_t count)
        {
            Vm* vm = vm_of(env);
            if (vm == nullptr || function.kind != THUNDER_FUNCTION)
            {
                return thunder_void();
            }
            std::vector<Value> arguments;
            arguments.reserve(count > 0 ? static_cast<std::size_t>(count) : 0);
            for (std::int32_t i = 0; i < count && args != nullptr; ++i)
            {
                switch (args[i].kind)
                {
                case THUNDER_INT:
                    arguments.push_back(Value::from_int(args[i].integer));
                    break;
                case THUNDER_FLOAT:
                    arguments.push_back(Value::from_float(args[i].number));
                    break;
                case THUNDER_BOOL:
                    arguments.push_back(Value::from_bool(args[i].integer != 0));
                    break;
                case THUNDER_STRING:
                    arguments.push_back(Value::from_string(args[i].text != nullptr && args[i].text_length > 0
                            ? std::string(args[i].text, static_cast<std::size_t>(args[i].text_length))
                            : std::string()));
                    break;
                default:
                    arguments.push_back(Value{});
                    break;
                }
            }
            // The returned string points into the result value, so it has to outlive this call;
            // the module must use it before calling back again.
            static thread_local std::string returned_text;
            const Value result = vm->native_callback(function.function, arguments);
            switch (result.kind())
            {
            case Value::Kind::Int:
                return thunder_int(result.as_int());
            case Value::Kind::Float:
                return thunder_float(result.as_float());
            case Value::Kind::Bool:
                return thunder_bool(result.as_bool() ? 1 : 0);
            case Value::Kind::String:
                returned_text = result.as_string();
                return thunder_text(returned_text.data(), static_cast<std::int64_t>(returned_text.size()));
            default:
                return thunder_void();
            }
        }
    }
}
