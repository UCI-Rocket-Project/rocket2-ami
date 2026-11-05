#include "ami_controller.h"
#include "main.h"
#include "quick_profile.h"
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
size_t counter;
float timestamp;
float hz;

extern "C" {
volatile QuickProfile quickProfile = {};
}

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

AMIController::AMIController(): scheduler(GetCurrentTime10us),
    led(&htim2, TIM_CHANNEL_4, &htim2, TIM_CHANNEL_3, &htim3, TIM_CHANNEL_4),
    accel(&hspi1, ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, ACCEL_INT0_Pin, ProcessADXLData) {
    instance = this;
    led.SetDefaultPulse({0, 0, RGBLED::MaxBrightness * 2 / 5}, 2000U * MS);
}

void AMIController::Init() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

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

    scheduler.ScheduleTaskDeltaTime(adxlTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(ledTaskId, 0);
}

void AMIController::Run() {
    while (1) {
        instance->scheduler.Run();
    }
}

void AMIController::ProcessLEDTask() {
    const uint32_t start = DWT->CYCCNT;
    instance->led.Process(GetCurrentTime10us());
    instance->scheduler.ScheduleTaskDeltaTime(instance->ledTaskId, 1 * MS);
    ProfileRecord(quickProfile.ledTask, start);
}

void AMIController::ProcessADXLTask() {
    const uint32_t start = DWT->CYCCNT;
    instance->accel.ProcessTask();
    instance->scheduler.ScheduleTaskDeltaTime(instance->adxlTaskId, 0.1 * MS);
    ProfileRecord(quickProfile.accelTask, start);
}

int16_t x = 0;
int16_t y = 0;
int16_t z = 0;

void AMIController::ProcessADXLData(const int16_t *values, size_t count) {
    const uint32_t start = DWT->CYCCNT;
    // Do something with the X, Y, Z values here.
    x = values[0];
    y = values[1];
    z = values[2];
    (void)values;
    (void)count;
    ProfileRecord(quickProfile.accelDataCallback, start);
}

extern "C" {
    void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
        AMIController::instance->accel.FIFOWatermarkIntCallback(GPIO_Pin);
    }

    void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
        AMIController::instance->accel.TxCompleteCallback(hspi);
    }
}
