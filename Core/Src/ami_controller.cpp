#include "ami_controller.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;

extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

extern UART_HandleTypeDef huart4;

extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

// Allocate memory for the static pointer
AMIController* AMIController::instance = nullptr;

// This is in 10 us/0.01 ms
inline uint32_t GetCurrentTime10us() {
    uint32_t high1;
    uint32_t low;
    uint32_t high2;
    do {
        high1 = __HAL_TIM_GET_COUNTER(&htim5);
        low = __HAL_TIM_GET_COUNTER(&htim4);
        high2 = __HAL_TIM_GET_COUNTER(&htim5);
    } while (high1 != high2);
    return (high1 << 16) | low;
}

AMIController::AMIController(): accel(&hspi1, ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, ACCEL_INT0_Pin,
        ProcessADXLData, OnADXLRecovery, GetCurrentTime10us),
    flash(&hspi2, MEM_nCS_GPIO_Port, MEM_nCS_Pin,
          MEM_nWP_GPIO_Port, MEM_nWP_Pin, MEM_nHOLD_GPIO_Port, MEM_nHOLD_Pin),
    scheduler(GetCurrentTime10us),
    led(&htim2, TIM_CHANNEL_4, &htim2, TIM_CHANNEL_3, &htim3, TIM_CHANNEL_4) {
    instance = this;
    led.SetDefaultPulse({0, 0, RGBLED::MaxBrightness * 2 / 5}, 2000U * MS);
}

void AMIController::Init() {
    /*
    * Initialize timers (10 us resolution)
    */
    // Make sure to start the slave before the master
    HAL_TIM_Base_Start(&htim5);
    HAL_TIM_Base_Start(&htim4);

    // Ensure both timers start at 0
    __HAL_TIM_SET_COUNTER(&htim4, 0);
    __HAL_TIM_SET_COUNTER(&htim5, 0);
    htim4.Instance->EGR = TIM_EGR_UG;
    htim5.Instance->EGR = TIM_EGR_UG;

    bool allPeripheralsGood = true;

    /*
    * Initialize LED
    */
    led.Init();


    /*
    * Initialize accel
    */
    allPeripheralsGood &= accel.Init();

    /* Connect is blocking; the flash scan starts asynchronously below. */
    const bool flashConnected = flash.ConnectBlocking() == W25N01GV::Result::Ok;
    allPeripheralsGood &= flashConnected;
    if (flashConnected) {
        allPeripheralsGood &= flash.MountAsync() == W25N01GV::Result::Pending;
    }

    if (allPeripheralsGood) {
        led.QueueBlink({0, RGBLED::MaxBrightness * 4 / 5, 0}, 3000 * MS, 500 * MS);
    } else {
        led.QueueBlink({RGBLED::MaxBrightness, 0, 0}, 3000 * MS, 500 * MS);
    }


    /*
    * Register tasks
    */
    ledTaskId = *scheduler.AddTask(ProcessLEDTask, 0);
    adxlTaskId = *scheduler.AddTask(ProcessADXLTask, 0);
    flashTaskId = *scheduler.AddTask(ProcessFlashTask, 0);
    RECINDUCERTASK = *scheduler.AddTask(RECINDUCE, 0);

    scheduler.ScheduleTaskDeltaTime(adxlTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(flashTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(ledTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(RECINDUCERTASK, 10 * 1000 * MS);
}

void AMIController::Run() {
    while (1) {
        instance->scheduler.Run();
    }
}

void AMIController::ProcessLEDTask() {
    if (instance->recoveryPulsePending.exchange(false, std::memory_order_relaxed)) {
        if (!instance->led.QueuePulse({RGBLED::MaxBrightness, RGBLED::MaxBrightness / 8, 0},
                                      3000 * MS, 500 * MS)) {
            instance->recoveryPulsePending.store(true, std::memory_order_relaxed);
        }
    }
    instance->led.Process(GetCurrentTime10us());
    instance->scheduler.ScheduleTaskDeltaTime(instance->ledTaskId, 1 * MS);
}

void AMIController::ProcessADXLTask() {
    instance->accel.ProcessTask();
    instance->scheduler.ScheduleTaskDeltaTime(instance->adxlTaskId, 0.1 * MS);
}

void AMIController::ProcessFlashTask() {
    instance->flash.ProcessTask();
    instance->scheduler.ScheduleTaskDeltaTime(instance->flashTaskId, 0.1 * MS);
}

void AMIController::RECINDUCE() {
    //HAL_Delay(5);
    instance->scheduler.ScheduleTaskDeltaTime(instance->RECINDUCERTASK, 10 * 1000 * MS);
}

int16_t x = 0;
int16_t y = 0;
int16_t z = 0;

void AMIController::ProcessADXLData(const int16_t *values, size_t count) {
    const size_t sampleSets = count / AXES;
    if (sampleSets == 0) return;

    int32_t xSum = 0;
    int32_t ySum = 0;
    int32_t zSum = 0;
    for (size_t i = 0; i < sampleSets; ++i) {
        const size_t offset = i * AXES;
        xSum += values[offset];
        ySum += values[offset + 1];
        zSum += values[offset + 2];
    }

    x = static_cast<int16_t>(xSum / static_cast<int32_t>(sampleSets));
    y = static_cast<int16_t>(ySum / static_cast<int32_t>(sampleSets));
    z = static_cast<int16_t>(zSum / static_cast<int32_t>(sampleSets));
}

void AMIController::OnADXLRecovery() {
    instance->recoveryPulsePending.store(true, std::memory_order_relaxed);
}

extern "C" {
    void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
        AMIController::instance->accel.FIFOWatermarkIntCallback(GPIO_Pin);
    }

    void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
        AMIController::instance->accel.TxCompleteCallback(hspi);
        AMIController::instance->flash.TxRxCompleteCallback(hspi);
    }

    void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
        AMIController::instance->flash.SpiErrorCallback(hspi);
    }
}
