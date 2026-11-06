#pragma once

#include "main.h"
#include "task_scheduler.h"
#include "led.h"
#include "accel_adxl383.h"
#include <atomic>

#define MS 100

class AMIController {
  public:
    static AMIController* instance;

    AMIController();
    void Init();
    void Run();

    // Hardware
    ADXL383 accel;

  private:

    // Task scheduler setup
    TaskScheduler<10, 3> scheduler;
    size_t ledTaskId;
    size_t adxlTaskId;
    size_t RECINDUCERTASK;

    // Hardware
    RGBLED led;
    std::atomic<bool> recoveryPulsePending{false};

    // Task callbacks
    static void ProcessLEDTask();
    static void ProcessADXLTask();
    static void ProcessADXLData(const int16_t *values, size_t count);
    static void OnADXLRecovery();
    static void RECINDUCE();
};
