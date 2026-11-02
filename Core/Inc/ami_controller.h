#pragma once

#include "main.h"
#include "task_scheduler.h"
#include "led.h"

#define MS 100

class AMIController {
  public:
    static AMIController* instance;

    AMIController();
    void Init();
    void Run();

  private:

    // Task scheduler setup
    TaskScheduler<10, 3> scheduler;
    size_t ledTaskId;

    // Hardware
    RGBLED led;

    // Task callbacks
    static void PulseLEDTask();
};