// Define READ to replay recorded pages; leave it undefined to erase and record.
#define READ
#include "ami_controller.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"
#include <cstring>

// Visible in the debugger. In replay mode these are the current page averages.
volatile int16_t x = 0;
volatile int16_t y = 0;
volatile int16_t z = 0;

// DWT cycle-count timing of the most recent and slowest flash task calls.
volatile float flashProcessLastMs = 0.0f;
volatile float flashProcessMaxMs = 0.0f;
static float flashCyclesToMs = 0.0f;
static uint32_t flashProcessMaxCycles = 0;

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
    for (auto &page : capturePages_) {
        std::memset(page, 0xFF, sizeof(page));
        page[0] = 0;
        std::memset(page + 2044, 0, 4);
    }
    capturePageStates_[0] = CapturePageState::Filling;
}

void AMIController::Init() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    flashCyclesToMs = 1000.0f / static_cast<float>(SystemCoreClock);

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

    // Connect is the only blocking flash call. Format/mount run in the task.
    const bool flashConnected = flash.ConnectBlocking() == W25N01GV::Result::Ok;
    allPeripheralsGood &= flashConnected;
    if (flashConnected) {
        flashTestResult_ =
#ifdef READ
            flash.MountAsync();
#else
            flash.FormatAsync();
#endif
        if (flashTestResult_ == W25N01GV::Result::Pending) {
            flashTestState_ = FlashTestState::Initializing;
        } else {
            allPeripheralsGood = false;
        }
    } else {
        flashTestResult_ = flash.LastResult();
    }



    if (!allPeripheralsGood) {
        led.QueueBlink({RGBLED::MaxBrightness, 0, 0}, 3000 * MS, 500 * MS);
    }
#ifdef READ
    else {
        led.QueueBlink({0, RGBLED::MaxBrightness * 4 / 5, 0}, 3000 * MS, 500 * MS);
    }
#endif


    /*
    * Register tasks
    */
    ledTaskId = *scheduler.AddTask(ProcessLEDTask, 0);
#ifndef READ
    adxlTaskId = *scheduler.AddTask(ProcessADXLTask, 0);
    adxlInitTaskId = *scheduler.AddTask(InitADXLTask, 0);
#endif
    flashTaskId = *scheduler.AddTask(ProcessFlashTask, 0);
    RECINDUCERTASK = *scheduler.AddTask(RECINDUCE, 0);

#ifndef READ
    // Leave ADXL processing unscheduled until its one-shot init completes.
    scheduler.ScheduleTaskDeltaTime(adxlInitTaskId, 10 * 1000 * MS);
#endif
    scheduler.ScheduleTaskDeltaTime(flashTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(ledTaskId, 0);
    //scheduler.ScheduleTaskDeltaTime(RECINDUCERTASK, 10 * 1000 * MS);
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

void AMIController::InitADXLTask() {
    AMIController &self = *instance;
    if (self.accel.Init()) {
        if (self.flash.Connected() && self.flashTestState_ != FlashTestState::Error) {
            self.led.QueueBlink({0, RGBLED::MaxBrightness * 4 / 5, 0},
                                3000 * MS, 500 * MS);
        }
        self.scheduler.ScheduleTaskDeltaTime(self.adxlTaskId, 0);
    } else {
        self.led.QueueBlink({RGBLED::MaxBrightness, 0, 0}, 3000 * MS, 500 * MS);
    }
}

void AMIController::ProcessADXLTask() {
    instance->accel.ProcessTask();
    instance->scheduler.ScheduleTaskDeltaTime(instance->adxlTaskId, 0.1 * MS);
}

void AMIController::ProcessFlashTask() {
    const uint32_t startCycles = DWT->CYCCNT;
    AMIController &self = *instance;
    self.flash.ProcessTask();

    if (self.flashTestState_ == FlashTestState::Initializing && !self.flash.Busy()) {
        self.flashTestResult_ = self.flash.LastResult();
        if (self.flashTestResult_ == W25N01GV::Result::Ok) {
#ifdef READ
            self.flash.RewindReadCursor();
            self.readPageReady_ = false;
            self.readPlaybackStarted_ = false;
            self.readLogEmpty_ = false;
            self.readHalfTick_ = false;
            self.flashTestState_ = FlashTestState::Reading;
#else
            self.flashTestState_ = FlashTestState::Recording;
#endif
        } else {
            self.flashTestState_ = FlashTestState::Error;
        }
    }

#ifdef READ
    if (self.flashTestState_ == FlashTestState::Reading) {
        if (self.flashPagePending_) {
            if (!self.flash.Busy()) {
                self.flashPagePending_ = false;
                self.flashTestResult_ = self.flash.LastResult();
                if (self.flashTestResult_ == W25N01GV::Result::Ok) {
                    self.readPageReady_ = true;
                } else if (self.flashTestResult_ == W25N01GV::Result::EndOfLog) {
                    if (self.readFlashPages_ == 0) self.readLogEmpty_ = true;
                    else self.flash.RewindReadCursor();
                } else {
                    self.flashTestState_ = FlashTestState::Error;
                }
            }
        }
        // 340 XYZ sets at 32,000 sets/s occupy 10.625 ms. The 10 us clock
        // alternates 1062/1063 ticks to represent that interval exactly.
        // Schedule from the actual presentation time so a delayed DMA read
        // cannot cause a burst of catch-up updates.
        const uint32_t now = GetCurrentTime10us();
        if (self.readPageReady_ &&
            (!self.readPlaybackStarted_ ||
             static_cast<int32_t>(now - self.readNextPresentation10us_) >= 0)) {
            int32_t xSum = 0;
            int32_t ySum = 0;
            int32_t zSum = 0;
            for (size_t i = 0; i < AccelSamplesPerPage; ++i) {
                const size_t offset = i * 6;
                const auto decode = [&](size_t axis) -> int16_t {
                    const uint16_t raw = static_cast<uint16_t>(self.readPayload_[offset + axis]) |
                        (static_cast<uint16_t>(self.readPayload_[offset + axis + 1]) << 8);
                    return static_cast<int16_t>(raw);
                };
                xSum += decode(0);
                ySum += decode(2);
                zSum += decode(4);
            }
            x = static_cast<int16_t>(xSum / static_cast<int32_t>(AccelSamplesPerPage));
            y = static_cast<int16_t>(ySum / static_cast<int32_t>(AccelSamplesPerPage));
            z = static_cast<int16_t>(zSum / static_cast<int32_t>(AccelSamplesPerPage));
            ++self.readFlashPages_;
            self.readPageReady_ = false;
            self.readPlaybackStarted_ = true;
            self.readNextPresentation10us_ = now + (self.readHalfTick_ ? 1063U : 1062U);
            self.readHalfTick_ = !self.readHalfTick_;
        }
        if (!self.flashPagePending_ && !self.readPageReady_ && !self.readLogEmpty_ &&
            self.flashTestState_ == FlashTestState::Reading) {
            self.flashTestResult_ = self.flash.ReadNextPageAsync(self.readPayload_);
            if (self.flashTestResult_ == W25N01GV::Result::Pending) {
                self.flashPagePending_ = true;
            } else if (self.flashTestResult_ == W25N01GV::Result::EndOfLog) {
                if (self.readFlashPages_ == 0) self.readLogEmpty_ = true;
                else self.flash.RewindReadCursor();
            } else {
                self.flashTestState_ = FlashTestState::Error;
            }
        }
    }
#else
    if (self.flashTestState_ == FlashTestState::Recording) {
        if (self.flashPagePending_ && !self.flash.Busy()) {
            self.flashPagePending_ = false;
            self.flashTestResult_ = self.flash.LastResult();
            if (self.flashTestResult_ == W25N01GV::Result::Ok) {
                self.capturePageStates_[self.inFlightPageIndex_] = CapturePageState::Free;
                self.inFlightPageIndex_ = CapturePageCount;
                if (self.capturePageStates_[self.captureWriteIndex_] == CapturePageState::Free) {
                    self.capturePageStates_[self.captureWriteIndex_] = CapturePageState::Filling;
                }
                ++self.writtenFlashPages_;
            } else {
                // A timed-out DMA may still own the page. Keep it reserved.
                self.flashTestState_ = self.flashTestResult_ == W25N01GV::Result::Full
                    ? FlashTestState::Full : FlashTestState::Error;
            }
        }
        if (!self.flashPagePending_ && self.flashTestState_ == FlashTestState::Recording &&
            self.readyPageCount_ != 0) {
            self.flashTestResult_ = self.flash.AppendPageAsync(self.capturePages_[self.captureReadIndex_]);
            if (self.flashTestResult_ == W25N01GV::Result::Pending) {
                // The driver DMA reads this slot directly. Release it only
                // after the append operation finishes.
                self.capturePageStates_[self.captureReadIndex_] = CapturePageState::InFlight;
                self.inFlightPageIndex_ = self.captureReadIndex_;
                self.captureReadIndex_ = (self.captureReadIndex_ + 1) % CapturePageCount;
                --self.readyPageCount_;
                self.flashPagePending_ = true;
            } else if (self.flashTestResult_ != W25N01GV::Result::Busy) {
                self.flashTestState_ = self.flashTestResult_ == W25N01GV::Result::Full
                    ? FlashTestState::Full : FlashTestState::Error;
            }
        }
    }
#endif

    self.scheduler.ScheduleTaskDeltaTime(self.flashTaskId, 1 * MS);
    const uint32_t elapsedCycles = DWT->CYCCNT - startCycles;
    flashProcessLastMs = static_cast<float>(elapsedCycles) * flashCyclesToMs;
    if (elapsedCycles > flashProcessMaxCycles) {
        flashProcessMaxCycles = elapsedCycles;
        flashProcessMaxMs = flashProcessLastMs;
    }
}

void AMIController::RECINDUCE() {
    //HAL_Delay(5);
    instance->scheduler.ScheduleTaskDeltaTime(instance->RECINDUCERTASK, 10 * 1000 * MS);
}

void AMIController::ProcessADXLData(const int16_t *values, size_t count) {
#ifdef READ
    (void)values;
    (void)count;
#else
    const size_t sampleSets = count / AXES;
    AMIController &self = *instance;
    if (self.flashTestState_ != FlashTestState::Recording) return;
    for (size_t i = 0; i < sampleSets; ++i) {
        CapturePageState &writeState = self.capturePageStates_[self.captureWriteIndex_];
        if (writeState == CapturePageState::Free) writeState = CapturePageState::Filling;
        if (writeState != CapturePageState::Filling) {
            ++self.droppedAccelSamples_;
            continue;
        }
        const size_t offset = i * AXES;
        uint8_t *const page = self.capturePages_[self.captureWriteIndex_];
        const size_t pageOffset = 1 + self.samplesInCapturePage_ * 6;
        for (size_t axis = 0; axis < AXES; ++axis) {
            const uint16_t raw = static_cast<uint16_t>(values[offset + axis]);
            page[pageOffset + axis * 2] = static_cast<uint8_t>(raw);
            page[pageOffset + axis * 2 + 1] = static_cast<uint8_t>(raw >> 8);
        }
        if (++self.samplesInCapturePage_ == AccelSamplesPerPage) {
            writeState = CapturePageState::Ready;
            ++self.readyPageCount_;
            self.captureWriteIndex_ = (self.captureWriteIndex_ + 1) % CapturePageCount;
            self.samplesInCapturePage_ = 0;
            if (self.capturePageStates_[self.captureWriteIndex_] == CapturePageState::Free) {
                self.capturePageStates_[self.captureWriteIndex_] = CapturePageState::Filling;
            }
        }
    }
#endif
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
