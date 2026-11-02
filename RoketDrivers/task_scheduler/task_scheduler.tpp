// Note that the template header is required on all non-inline functions in .tpp files

#pragma once

#include <cassert>

template <size_t MaxTasks, size_t MaxDepsPerTask>
TaskScheduler<MaxTasks, MaxDepsPerTask>::TaskScheduler(ClockFunction clock) : _clock(clock) {
    assert(clock != nullptr);
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
std::optional<size_t> TaskScheduler<MaxTasks, MaxDepsPerTask>::AddTask(TaskCallback cb, size_t requiredDeps) {
    if (requiredDeps > MaxDepsPerTask) return std::nullopt;

    for (size_t i = 0; i < MaxTasks; i++) {
        if (!_tasks[i].callback) {
            _tasks[i].callback = cb;
            _tasks[i].requiredDependencies = requiredDeps;
            _tasks[i].scheduledTime = 0;
            _tasks[i].timeInitialized = false;
            _tasks[i].active = false;
            _tasks[i].next = MaxTasks;

            for (size_t d = 0; d < MaxDepsPerTask; ++d) {
                _tasks[i].dependencies[d] = false;
            }
            return i;
        }
    }
    return std::nullopt;
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::ScheduleTaskTargetTime(size_t id, uint32_t targetTime) {
    if (id >= MaxTasks || !_tasks[id].callback) return;

    // Disable interrupts temporarily to prevent superloop modifying task at the same time
    SCHEDULER_CRITICAL_ENTER();

    // After first execution, this time becomes the baseline for later deltas.
    ScheduleTask(id, targetTime);

    SCHEDULER_CRITICAL_EXIT();
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::ScheduleTaskDeltaTime(size_t id, uint32_t deltaTime) {
    if (id >= MaxTasks || !_tasks[id].callback) return;

    SCHEDULER_CRITICAL_ENTER();

    Task& t = _tasks[id];
    // An active task already has a deadline, even if it has never executed.
    // Only a never-scheduled task reads the clock here. Keep this read/add/insert
    // atomic so ISR scheduling cannot overwrite another deadline increment.
    uint32_t baseline = (t.timeInitialized || t.active) ? t.scheduledTime : _clock();
    ScheduleTask(id, baseline + deltaTime);

    SCHEDULER_CRITICAL_EXIT();
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::ScheduleTask(size_t id, uint32_t targetTime) {
    RemoveFromList(id);

    _tasks[id].scheduledTime = targetTime;
    _tasks[id].active = true;
    _tasks[id].next = MaxTasks;

    // If list is empty OR targetTime is chronologically BEFORE the head's time,
    // move the new task to the front
    if (_head == MaxTasks || TimeIsBefore(targetTime, _tasks[_head].scheduledTime)) {
        _tasks[id].next = _head;
        _head = id;
    } else {
        size_t curr = _head;

        // Iterate until we find a task scheduled LATER than our target time
        while (_tasks[curr].next != MaxTasks && !TimeIsBefore(targetTime, _tasks[_tasks[curr].next].scheduledTime)) {
            curr = _tasks[curr].next;
        }
        _tasks[id].next = _tasks[curr].next;
        _tasks[curr].next = id;
    }
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::SetDependency(size_t id, size_t depIndex, bool state) {
    if (id < MaxTasks && depIndex < _tasks[id].requiredDependencies) {
        _tasks[id].dependencies[depIndex] = state;
    }
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::Run() {
    // Use one readiness timestamp throughout this pass, even if callbacks take time.
    const uint32_t currentTime = _clock();
    std::array<bool, MaxTasks> executedInThisRun = {false};

    // Repeatedly find the next task ready to run until no more ready tasks exist
    while (true) {
        size_t readyTask = MaxTasks;

        SCHEDULER_CRITICAL_ENTER();
        size_t curr = _head;
        while (curr != MaxTasks) {
            Task& t = _tasks[curr];

            // If scheduled time hasn't arrived, stop looking because the list is sorted
            if (!TimeIsReady(currentTime, t.scheduledTime)) {
                break;
            }

            if (!executedInThisRun[curr]) {
                // Check dependencies
                bool depsMet = true;
                for (size_t i = 0; i < t.requiredDependencies; ++i) {
                    if (!t.dependencies[i]) {
                        depsMet = false;
                        break;
                    }
                }

                if (depsMet) {
                    readyTask = curr;
                    RemoveFromList(readyTask);

                    // Commit dispatch state before interrupts can schedule this task
                    // again. Do not clear these fields after leaving the critical section.
                    t.active = false;
                    t.next = MaxTasks;
                    if (!t.timeInitialized) {
                        // Anchor recurrence to actual first execution, not setup time
                        // or a deadline delayed by dependencies. Do this before the
                        // callback so it can immediately reschedule itself with a delta.
                        t.scheduledTime = _clock();
                        t.timeInitialized = true;
                    }
                    for (size_t i = 0; i < t.requiredDependencies; ++i) {
                        t.dependencies[i] = false;
                    }
                    break;
                }
            }

            curr = t.next;
        }
        SCHEDULER_CRITICAL_EXIT();

        // If no task is ready and eligible to run at currentTime, exit
        if (readyTask == MaxTasks) {
            break;
        }

        executedInThisRun[readyTask] = true;

        Task& t = _tasks[readyTask];
        // Execute the callback
        t.callback();
    }
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
inline bool TaskScheduler<MaxTasks, MaxDepsPerTask>::TimeIsBefore(uint32_t t1, uint32_t t2) {
    return (int32_t)(t1 - t2) < 0;
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
inline bool TaskScheduler<MaxTasks, MaxDepsPerTask>::TimeIsReady(uint32_t current, uint32_t scheduled) {
    return (int32_t)(current - scheduled) >= 0;
}

template <size_t MaxTasks, size_t MaxDepsPerTask>
void TaskScheduler<MaxTasks, MaxDepsPerTask>::RemoveFromList(size_t id) {
    if (_head == MaxTasks) return;
    if (_head == id) {
        _head = _tasks[id].next;
        return;
    }
    size_t curr = _head;
    while (_tasks[curr].next != MaxTasks) {
        if (_tasks[curr].next == id) {
            _tasks[curr].next = _tasks[id].next;
            return;
        }
        curr = _tasks[curr].next;
    }
}
