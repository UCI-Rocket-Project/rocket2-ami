# Firmware Task Scheduler

## Overview

This is a minimal, single-threaded cooperative task scheduler designed for STM32 embedded firmware. It runs callback functions at target timestamps once all configured dependencies have been met.

It is **timer-agnostic**: the application injects a clock function returning 32-bit unsigned timestamps (`uint32_t`). This can wrap `HAL_GetTick()` for millisecond intervals or read hardware timer registers for another resolution. The scheduler stores deadlines and manages recurring intervals without task-local timestamp variables. Requires C++17 or later.

## Usage

### Task Initialization

The scheduler is implemented as a template class parameterized by capacity limits, avoiding dynamic memory allocation:

```cpp
template <size_t MaxTasks, size_t MaxDepsPerTask>
class TaskScheduler;
```

- `MaxTasks`: The maximum number of tasks that can be allocated concurrently in the internal task table.
- `MaxDepsPerTask`: The maximum number of prerequisite dependency flags tracked per task.

Construct the scheduler with a non-null clock function (`uint32_t (*)()`):

```cpp
inline uint32_t GetCurrentTime() {
    return HAL_GetTick();
}

TaskScheduler<8, 2> scheduler(GetCurrentTime);
```

Ordinary free functions (including `inline` functions) and static member functions can be passed directly. The constructor only stores the pointer, so a global scheduler can be constructed before the timer is initialized. Initialize the timer before calling either scheduling method or `Run()`.

The clock must be fast, nonblocking, and safe to call from an ISR with interrupts disabled. It must consistently use the same timebase as all deadlines and intervals. The scheduler does not configure or reset the timer.

Tasks are registered using the `AddTask()` method:

```cpp
std::optional<size_t> AddTask(TaskCallback cb, size_t requiredDeps = 0);
```

- `cb`: A function pointer (`void (*)()`) to execute when the task is triggered.
- `requiredDeps`: The number of boolean dependencies this task requires before running (must be `<= MaxDepsPerTask`). Defaults to `0`.

`AddTask()` allocates the next available open slot in the static task table and returns the assigned `size_t` task ID. If `requiredDeps` exceeds `MaxDepsPerTask` or if the task table is full, it returns `std::nullopt`. Tasks are registered in an inactive state and must be explicitly scheduled before they will execute.

### Scheduling Tasks

There are two scheduling methods:

```cpp
void ScheduleTaskTargetTime(size_t id, uint32_t targetTime);
void ScheduleTaskDeltaTime(size_t id, uint32_t deltaTime);
```

- `id`: The task ID returned by `AddTask()`.
- `targetTime`: The timestamp at which the task should attempt execution, expressed in ticks of the application's chosen timebase (e.g. `TIM5->CNT` or `HAL_GetTick()`).
- `deltaTime`: The interval to add to the task's stored deadline, in ticks of the same timebase.

`ScheduleTaskTargetTime()` replaces any pending deadline with the supplied absolute timestamp. After the task has executed at least once, this timestamp also becomes the baseline for subsequent delta scheduling.

`ScheduleTaskDeltaTime()` follows these rules:

1. **Never scheduled:** queue the first execution at `clock() + deltaTime`.
2. **Waiting for first execution:** add `deltaTime` to the existing pending deadline. Repeated calls accumulate, even before the task has executed.
3. **Previously executed:** add `deltaTime` to the stored deadline, whether the task is currently queued or inactive.

Immediately before a task's **first callback**, `Run()` replaces its stored deadline with a fresh clock sample and marks its baseline initialized. This happens only once, after its deadline has arrived and its dependencies are satisfied.

Both methods insert the task into a singly linked list sorted by deadline. If already active, the task is unlinked and reinserted. Invalid or unallocated task IDs are ignored. Scheduling enters a critical section (`SCHEDULER_CRITICAL_ENTER()`), disabling interrupts on ARM platforms and restoring the prior interrupt state afterward. Delta calculation and list updates share that critical section, allowing scheduling from ISRs without losing increments. Callbacks execute outside the critical section.

**Tasks do not reschedule automatically.** For periodic execution, call `ScheduleTaskDeltaTime(id, interval)` from the callback.

### Task Dependencies

Tasks can declare up to `MaxDepsPerTask` boolean prerequisites that must all evaluate to `true` before the callback will execute. Dependencies are updated using `SetDependency()`:

```cpp
void SetDependency(size_t id, size_t depIndex, bool state);
```

- `id`: The target task ID.
- `depIndex`: The index of the dependency (`0` to `requiredDependencies - 1`).
- `state`: Boolean status flag indicating whether the prerequisite has been met.

Dependency flags can be asserted from ISRs. Dispatch resets them inside the scheduler critical section before invoking the callback.

**Dependencies are automatically reset**: When a task's callback runs, the scheduler resets all of that task's active dependencies back to `false`. Subsequent executions require dependencies to be explicitly re-asserted, preventing stale event flags from inadvertently triggering repeat executions.

If a task reaches its scheduled time but one or more dependencies remain unmet, **the task remains in the active list** and will be re-evaluated on subsequent scheduler cycles (so it will run as soon as the dependencies are fulfilled). Waiting for dependencies does not initialize the first-execution baseline.

### Main Scheduler Loop

The scheduler is driven by calling `Run()` repeatedly within the firmware superloop:

```cpp
void Run();
```

`Run()` samples the injected clock once for readiness checks throughout that pass.

During each pass, `Run()` traverses the chronologically sorted task list:

1. Exit early: If the current time has not reached the head task's scheduled timestamp, `Run()` terminates without checking later tasks.
2. Check dependencies: For each task whose target time has arrived, `Run()` verifies that all required dependencies are satisfied.
3. Execute task: If all dependencies are satisfied and the task has not executed in this pass, remove it from the list, clear its active and dependency states, and initialize its baseline if this is its first execution. These updates share one critical section. Then invoke the callback outside the critical section.
4. Deferred tasks: If dependencies are not yet met, the task remains in the list and `Run()` advances to evaluate subsequent tasks whose scheduled time may also have passed.

## Current Limitations

### Lack of Task Priority

Tasks are scheduled and evaluated strictly based on chronological order (`targetTime`). If multiple tasks expire at the same time, they are dispatched in the order they were inserted (FIFO).

There is currently no support for task priorities or preemption levels. Future revisions could introduce priority queues or tie-breaking priority fields.

### Lack of Multithreading

The scheduler operates in a single-threaded, cooperative execution model:

- Callbacks run synchronously on the superloop's execution context.
- **A long-running or blocking task callback will stall the scheduler and starve all other pending tasks**.
- There is no thread preemption or hardware context switching. Callbacks must remain lightweight and non-blocking, delegating long delays to timer scheduling or asynchronous interrupts.

### Lack of Callback Context / User Arguments

Task callbacks are currently defined as bare function pointers without parameters:

```cpp
typedef void (*TaskCallback)();
```

Because callbacks cannot receive user-supplied data (such as a `void*` context pointer, an object reference, or even the task's own ID), tasks that reschedule themselves or interact with other tasks must access state and IDs via static or global variables.

This makes integrating the scheduler into applications more cumbersome (requiring static wrappers and extra boilerplate). Future revisions could support context arguments (e.g. `void (*)(void* user_data)` or passing the `size_t taskId` directly into the callback).

For now, it is recommended to encapsulate tasks, callbacks, and necessary state in a **controller class separate from the rest of the application** to ensure the global namespace doesn't get too polluted.

## Basic Usage Example

The example below demonstrates scheduling tasks using a hardware timer register (such as 32-bit timer `TIM5` at a configurable resolution, but this is just a made-up example) and signaling a dependency from an external GPIO interrupt: Note that you may want to contain these global variables within a user-defined class instead.

```cpp
#include <optional>
#include "stm32f1xx_hal.h"
#include "task_scheduler/task_scheduler.h"

// Helper to sample the hardware timebase (e.g. HAL_GetTick, whatever other timer)
inline uint32_t GetCurrentTime() {
    return TIM5->CNT;
}

// 1. Inject the clock (up to 8 tasks, 2 dependencies per task).
// Construction stores the pointer without reading the hardware timer.
TaskScheduler<8, 2> scheduler(GetCurrentTime);

// Task IDs (stored globally so callbacks and ISRs can reference them)
size_t sensorTaskId = 0;
size_t telemetryTaskId = 0;

// 2. Define task callbacks
void SensorTask() {
    // Initiate non-blocking sensor acquisition (e.g., start DMA or async read)...

    // Advance the stored deadline by 10,000 ticks (e.g. 10 ms at 1 MHz TIM5).
    // First execution anchors this recurrence to the actual execution time.
    scheduler.ScheduleTaskDeltaTime(sensorTaskId, 10000);
}

void TelemetryTask() {
    // Transmit telemetry packet (only runs when dependency 0 is satisfied)
    // Dependencies are automatically reset to false by the scheduler after running

    // Reschedule self for the next transmission window (e.g. 100,000 timer ticks)
    scheduler.ScheduleTaskDeltaTime(telemetryTaskId, 100000);
}

// 3. Hardware interrupt handler asserting a task dependency
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        // Sensor data-ready interrupt: satisfy dependency 0 for telemetry task
        // SetDependency() is interrupt-safe and can be called directly from ISRs
        scheduler.SetDependency(telemetryTaskId, 0, true);
    }
}

int main() {
    HAL_Init();
    SystemClock_Config();

    // Init rest of hardware

    // 4. Register tasks with AddTask
    std::optional<size_t> sensorId = scheduler.AddTask(SensorTask, 0);          // No dependencies
    std::optional<size_t> telemetryId = scheduler.AddTask(TelemetryTask, 1);    // 1 required dependency

    if (!sensorId.has_value() || !telemetryId.has_value()) {
        // Handle task registration failure (e.g. task table capacity reached)
        while (1);
    }

    sensorTaskId = *sensorId;
    telemetryTaskId = *telemetryId;

    // 5. Both startup paths work with delta-based rescheduling in the callbacks.
    // Delta-only: read the injected clock and schedule the first sensor execution.
    scheduler.ScheduleTaskDeltaTime(sensorTaskId, 10000);

    // You can also schedule a task using a target time for the first time
    uint32_t now = GetCurrentTime();
    scheduler.ScheduleTaskTargetTime(telemetryTaskId, now + 100000);

    // 6. Main superloop: continuously process ready tasks
    while (1) {
        scheduler.Run();
    }
}
```