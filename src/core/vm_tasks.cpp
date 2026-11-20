// The concurrency kernel: green threads ("tasks") run by the virtual machine itself.
//
// A task is its own operand stack, its own call frames and the offset it stopped at - nothing
// more - so switching from one to the next is three moves. Every task runs on the one OS thread
// the virtual machine already uses, which is what makes the whole design safe: no two pieces of
// C++ ever touch one array at the same time, so there is no undefined behaviour to be had, and
// the schedule is the same on every run unless the program asks for another one
// (--schedule-seed). Races still happen where they really happen - "count += 1" is a load, an
// add and a store, and a switch between them loses an update - but they are reproducible, which
// is exactly what a concurrency course needs.
//
// The kernel offered to programs is the module "task" (docs/concurrency.md): spawn, join,
// sleep, yield, current, name, count, and the two primitives that cannot be built in Thunder
// because they need atomicity - the lock (mutex) and the condition. Everything else - the
// semaphore, the barrier, the channel, the wait group, the latch, the read/write lock and the
// timers - is ordinary Thunder in lib/thunder/<language>/concurrency.th, written to be read.

#include "thunder/os.hpp"
#include "thunder/vm.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <thread>
#include <utility>

namespace thunder
{
    namespace
    {
        // A sentence of the deadlock report, built from a template in the program's language.
        std::string sentence(const std::string& pattern, const std::string& first, const std::string& second = {})
        {
            try
            {
                return std::vformat(pattern, std::make_format_args(first, second));
            }
            catch (const std::format_error&)
            {
                return first; // a broken translation must not hide the deadlock itself
            }
        }
    }

    // ------------------------------------------------------------------ the records

    void Vm::start_tasks()
    {
        if (!tasks_.empty())
        {
            return;
        }
        auto main_task = std::make_unique<Task>();
        main_task->id = 0;
        main_task->name = language_->term("main"); // "main", "اصلي"
        main_task->state = TaskState::Running;
        tasks_.push_back(std::move(main_task));
        current_task_ = 0;
        live_tasks_ = 1;
        schedule_state_ = options_.schedule_seed.value_or(0) * 2'862'933'555'777'941'757ULL + 3'037'000'493ULL;
        slice_ends_ = next_slice();
    }

    Task& Vm::current()
    {
        start_tasks();
        return *tasks_[current_task_];
    }

    std::uint64_t Vm::next_slice()
    {
        if (options_.slice == 0)
        {
            return std::numeric_limits<std::uint64_t>::max(); // --slice 0: switch only where a task waits
        }
        std::uint64_t length = options_.slice;
        if (options_.schedule_seed)
        {
            // A turn of 1 to 2*slice instructions, in a sequence the seed decides: the program
            // interleaves differently but still in the same way on every run with that seed.
            schedule_state_ ^= schedule_state_ << 13;
            schedule_state_ ^= schedule_state_ >> 7;
            schedule_state_ ^= schedule_state_ << 17;
            length = 1 + schedule_state_ % (2 * options_.slice);
        }
        return stats_.steps + length;
    }

    std::int64_t Vm::handle_arg(const std::vector<Value>& args, std::size_t i, std::size_t count, const char* what)
    {
        const std::int64_t handle = int_arg(args, i);
        if (handle < 0 || static_cast<std::uint64_t>(handle) >= count)
        {
            fail(DiagId::UnknownHandle, handle, language_->term(what));
        }
        return handle;
    }

    // ------------------------------------------------------------------ starting and ending

    std::uint32_t Vm::spawn_task(const Value& action)
    {
        start_tasks();
        if (!action.is_function())
        {
            fail(DiagId::InvalidBytecode, "task.spawn expects a function value");
        }
        const FunctionRef& reference = action.as_function();
        if (reference.index >= program_.functions.size())
        {
            fail(DiagId::InvalidBytecode, "task.spawn names a function that does not exist");
        }
        const bytecode::Function& function = program_.functions[reference.index];
        if (function.arity != 0)
        {
            fail(DiagId::InvalidBytecode, "task.spawn expects a function without parameters");
        }
        const std::size_t captured = reference.captures != nullptr ? reference.captures->size() : 0;
        if (captured != function.capture_count)
        {
            fail(DiagId::InvalidBytecode, "task.spawn: the function value carries the wrong number of boxes");
        }
        auto task = std::make_unique<Task>();
        task->id = static_cast<std::uint32_t>(tasks_.size());
        // "task#2" for the second task of the program: the one the program starts in is "main".
        task->name = language_->term("task") + "#" + locale().localize_digits(std::to_string(task->id + 1));
        task->state = TaskState::Ready;
        // The task begins in the middle of a call that nobody made: its stack holds the
        // function's local slots (it takes no parameters) and its one frame starts at offset 0.
        task->stack.assign(function.local_count, Value{});
        task->frames.push_back(CallFrame{reference.index, 0, 0, reference.captures});
        const std::uint32_t id = task->id;
        tasks_.push_back(std::move(task));
        ready_.push_back(id);
        ++live_tasks_;
        return id;
    }

    void Vm::finish_current_task()
    {
        Task& me = *tasks_[current_task_];
        me.state = TaskState::Done;
        me.stack.clear();
        me.frames.clear();
        --live_tasks_;
        for (const std::uint32_t joiner : me.joiners)
        {
            make_ready(joiner);
        }
        me.joiners.clear();
    }

    void Vm::make_ready(std::uint32_t id)
    {
        Task& task = *tasks_[id];
        task.state = TaskState::Ready;
        task.waiting_for.clear();
        ready_.push_back(id);
    }

    void Vm::block_current(TaskState state, std::string waiting_for)
    {
        Task& me = current();
        me.state = state;
        me.waiting_for = std::move(waiting_for);
        switch_pending_ = true;
    }

    // ------------------------------------------------------------------ the scheduler

    void Vm::restore_task(std::uint32_t id)
    {
        current_task_ = id;
        Task& task = *tasks_[id];
        task.state = TaskState::Running;
        task.waiting_for.clear();
        stack_ = std::move(task.stack);
        frames_ = std::move(task.frames);
        current_offset_ = task.offset;
        task.stack.clear();
        task.frames.clear();
        slice_ends_ = next_slice();
    }

    bool Vm::schedule()
    {
        switch_pending_ = false;
        Task& me = *tasks_[current_task_];
        if (frames_.empty())
        {
            finish_current_task(); // the task's function returned: it has run its course
        }
        else if (me.state == TaskState::Running)
        {
            me.state = TaskState::Ready; // its turn is over, but it can go on
            ready_.push_back(me.id);
        }
        if (me.state != TaskState::Done)
        {
            me.stack = std::move(stack_);
            me.frames = std::move(frames_);
            me.offset = current_offset_;
        }
        stack_.clear();
        frames_.clear();

        while (true)
        {
            if (!ready_.empty())
            {
                std::size_t pick = 0;
                if (options_.schedule_seed)
                {
                    schedule_state_ ^= schedule_state_ << 13;
                    schedule_state_ ^= schedule_state_ >> 7;
                    schedule_state_ ^= schedule_state_ << 17;
                    pick = static_cast<std::size_t>(schedule_state_ % ready_.size());
                }
                const std::uint32_t id = ready_[pick];
                ready_.erase(ready_.begin() + static_cast<std::ptrdiff_t>(pick));
                restore_task(id);
                return true;
            }
            if (live_tasks_ == 0)
            {
                return false; // every task has ended: so has the program
            }
            // Nothing can run this instant. Wake whatever is due, read the line a task asked
            // for, or - when every task only waits for time - let the machine itself wait.
            const auto now = std::chrono::steady_clock::now();
            bool woke = false;
            for (const std::unique_ptr<Task>& task : tasks_)
            {
                if (task->state == TaskState::Sleeping && task->wake <= now)
                {
                    make_ready(task->id);
                    woke = true;
                }
            }
            if (woke)
            {
                continue;
            }
            const auto waiting_for_input = std::ranges::find_if(tasks_,
                [](const std::unique_ptr<Task>& task) { return task->state == TaskState::WaitingInput; });
            if (waiting_for_input != tasks_.end())
            {
                // Nothing else to do: really read a line. Its value takes the place input()
                // would have pushed, on that task's own stack.
                output_.flush();
                (*waiting_for_input)->stack.push_back(Value::from_string(read_line()));
                make_ready((*waiting_for_input)->id);
                continue;
            }
            std::optional<std::chrono::steady_clock::time_point> earliest;
            for (const std::unique_ptr<Task>& task : tasks_)
            {
                if (task->state == TaskState::Sleeping && (!earliest || task->wake < *earliest))
                {
                    earliest = task->wake;
                }
            }
            if (earliest)
            {
                output_.flush();
                std::this_thread::sleep_until(*earliest);
                continue;
            }
            report_deadlock();
        }
    }

    void Vm::report_deadlock()
    {
        // The scheduler has already set the running task aside, so put it back: the report then
        // points at the line where that task began to wait, with its call stack.
        if (frames_.empty() && !tasks_[current_task_]->frames.empty())
        {
            Task& me = *tasks_[current_task_];
            stack_ = std::move(me.stack);
            frames_ = std::move(me.frames);
            current_offset_ = me.offset;
        }
        // Every remaining task waits for something only another task could do, and every one of
        // those waits in turn: nothing will ever happen again. Name what each one waits for.
        std::string detail;
        for (const std::unique_ptr<Task>& task : tasks_)
        {
            if (task->state == TaskState::Blocked && !task->waiting_for.empty())
            {
                if (!detail.empty())
                {
                    detail += "; ";
                }
                detail += task->waiting_for;
            }
        }
        fail(DiagId::Deadlock, detail);
    }

    // ------------------------------------------------------------------ waiting

    void Vm::wait_milliseconds(std::int64_t milliseconds)
    {
        wait_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max<std::int64_t>(milliseconds, 0)));
    }

    void Vm::wait_until(std::chrono::steady_clock::time_point when)
    {
        output_.flush();
        if (!tasks_started() || execute_depth_ != 1)
        {
            // A program without tasks waits as a whole, exactly as it always has; so does one
            // that sleeps inside a callback of map, filter, reduce or a native function, which
            // cannot be set aside (see docs/concurrency.md).
            std::this_thread::sleep_until(when);
            return;
        }
        current().wake = when;
        block_current(TaskState::Sleeping, {});
    }

    void Vm::release_lock(std::int64_t lock)
    {
        Lock& held = locks_[static_cast<std::size_t>(lock)];
        if (held.waiters.empty())
        {
            held.owner = -1;
            return;
        }
        // Hand it straight to the task that has waited longest: it wakes holding the lock, so
        // no third task can slip in between - and waiting is fair, first come first served.
        const std::uint32_t next = held.waiters.front();
        held.waiters.pop_front();
        held.owner = next;
        make_ready(next);
    }

    // ------------------------------------------------------------------ the built-ins

    bool Vm::task_builtin(BuiltinId id, std::vector<Value>& args)
    {
        // Waiting means setting this task aside, which is impossible while it runs a callback on
        // the C++ stack (map, filter, reduce, a native function): that is a clean error.
        const auto cannot_wait_here = [&]
        {
            if (execute_depth_ != 1)
            {
                fail(DiagId::WaitInCallback, language_->spelling(id));
            }
        };
        switch (id)
        {
        case BuiltinId::Fail:
            // fail("..."): the program stops itself, the way a wrong built-in call stops it -
            // with its own message, a source line and a call stack (R043).
            fail(DiagId::ProgramFailed, string_arg(args, 0));
        case BuiltinId::TaskSpawn:
            push(Value::from_int(spawn_task(args[0])));
            return true;
        case BuiltinId::TaskJoin:
        {
            start_tasks();
            const std::int64_t wanted = handle_arg(args, 0, tasks_.size(), "task");
            if (static_cast<std::uint32_t>(wanted) == current_task_)
            {
                fail(DiagId::JoinSelf);
            }
            Task& target = *tasks_[static_cast<std::size_t>(wanted)];
            if (target.state != TaskState::Done)
            {
                cannot_wait_here();
                target.joiners.push_back(current_task_);
                block_current(TaskState::Blocked,
                    sentence(language_->term("{0} waits for {1} to end"), current().name, target.name));
            }
            return true;
        }
        case BuiltinId::TaskDone:
        {
            start_tasks();
            const std::int64_t wanted = handle_arg(args, 0, tasks_.size(), "task");
            push(Value::from_bool(tasks_[static_cast<std::size_t>(wanted)]->state == TaskState::Done));
            return true;
        }
        case BuiltinId::TaskSleep:
            wait_milliseconds(int_arg(args, 0));
            return true;
        case BuiltinId::TaskYield:
            start_tasks();
            switch_pending_ = true; // the loop gives the ready tasks their turn, this one included
            return true;
        case BuiltinId::TaskCurrent:
            start_tasks();
            push(Value::from_int(current_task_));
            return true;
        case BuiltinId::TaskName:
        {
            start_tasks();
            const std::int64_t wanted = handle_arg(args, 0, tasks_.size(), "task");
            push(Value::from_string(tasks_[static_cast<std::size_t>(wanted)]->name));
            return true;
        }
        case BuiltinId::TaskCount:
            start_tasks();
            push(Value::from_int(static_cast<std::int64_t>(live_tasks_)));
            return true;
        case BuiltinId::TaskMutex:
            start_tasks();
            locks_.emplace_back();
            push(Value::from_int(static_cast<std::int64_t>(locks_.size() - 1)));
            return true;
        case BuiltinId::TaskLock:
        {
            start_tasks();
            const std::int64_t which = handle_arg(args, 0, locks_.size(), "lock");
            Lock& lock = locks_[static_cast<std::size_t>(which)];
            if (lock.owner < 0)
            {
                lock.owner = current_task_;
                return true;
            }
            if (lock.owner == static_cast<std::int64_t>(current_task_))
            {
                fail(DiagId::LockHeldAlready); // a task cannot wait for itself to let go
            }
            cannot_wait_here();
            lock.waiters.push_back(current_task_);
            block_current(TaskState::Blocked,
                sentence(language_->term("{0} waits for the lock {1} holds"), current().name,
                    tasks_[static_cast<std::size_t>(lock.owner)]->name));
            return true;
        }
        case BuiltinId::TaskTryLock:
        {
            start_tasks();
            const std::int64_t which = handle_arg(args, 0, locks_.size(), "lock");
            Lock& lock = locks_[static_cast<std::size_t>(which)];
            const bool taken = lock.owner < 0;
            if (taken)
            {
                lock.owner = current_task_;
            }
            push(Value::from_bool(taken));
            return true;
        }
        case BuiltinId::TaskUnlock:
        {
            start_tasks();
            const std::int64_t which = handle_arg(args, 0, locks_.size(), "lock");
            if (locks_[static_cast<std::size_t>(which)].owner != static_cast<std::int64_t>(current_task_))
            {
                fail(DiagId::LockNotHeld, language_->spelling(id));
            }
            release_lock(which);
            return true;
        }
        case BuiltinId::TaskCondition:
            start_tasks();
            conditions_.emplace_back();
            push(Value::from_int(static_cast<std::int64_t>(conditions_.size() - 1)));
            return true;
        case BuiltinId::TaskWait:
        {
            start_tasks();
            const std::int64_t which = handle_arg(args, 0, conditions_.size(), "condition");
            const std::int64_t lock = handle_arg(args, 1, locks_.size(), "lock");
            if (locks_[static_cast<std::size_t>(lock)].owner != static_cast<std::int64_t>(current_task_))
            {
                fail(DiagId::LockNotHeld, language_->spelling(id));
            }
            cannot_wait_here();
            // Let go of the lock, wait for the signal, and take the lock back before going on:
            // the three steps of a monitor, which is why they cannot be written in Thunder.
            current().retake_lock = lock;
            release_lock(lock);
            conditions_[static_cast<std::size_t>(which)].waiters.push_back(current_task_);
            block_current(TaskState::Blocked, sentence(language_->term("{0} waits for a condition"), current().name));
            return true;
        }
        case BuiltinId::TaskSignal:
        case BuiltinId::TaskBroadcast:
        {
            start_tasks();
            const std::int64_t which = handle_arg(args, 0, conditions_.size(), "condition");
            std::deque<std::uint32_t>& waiters = conditions_[static_cast<std::size_t>(which)].waiters;
            while (!waiters.empty())
            {
                const std::uint32_t woken = waiters.front();
                waiters.pop_front();
                Task& task = *tasks_[woken];
                const std::int64_t lock = task.retake_lock;
                task.retake_lock = -1;
                if (lock >= 0 && locks_[static_cast<std::size_t>(lock)].owner >= 0)
                {
                    // The lock is held (usually by the task doing the signalling, inside its own
                    // critical section): the woken task queues for it and wakes holding it.
                    locks_[static_cast<std::size_t>(lock)].waiters.push_back(woken);
                    task.waiting_for = sentence(language_->term("{0} waits for the lock {1} holds"), task.name,
                        tasks_[static_cast<std::size_t>(locks_[static_cast<std::size_t>(lock)].owner)]->name);
                }
                else
                {
                    if (lock >= 0)
                    {
                        locks_[static_cast<std::size_t>(lock)].owner = woken;
                    }
                    make_ready(woken);
                }
                if (id == BuiltinId::TaskSignal)
                {
                    break; // signal wakes one; broadcast wakes them all
                }
            }
            return true;
        }
        default:
            return false;
        }
    }
}
