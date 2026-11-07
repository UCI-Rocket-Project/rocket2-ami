#pragma once

#include "main.h"
#include "task_scheduler.h"
#include "led.h"
#include "accel_adxl383.h"
#include "w25n01gv.h"
#include <atomic>
#include <cstdint>

#define MS 100

class AMIController {
  public:
    static AMIController* instance;

    AMIController();
    void Init();
    void Run();

    // Hardware
    ADXL383 accel;
    W25N01GV flash;

  private:

    // Task scheduler setup
    TaskScheduler<10, 3> scheduler;
    size_t ledTaskId;
    size_t adxlTaskId;
    size_t adxlInitTaskId;
    size_t flashTaskId;
    size_t RECINDUCERTASK;

    // Hardware
    RGBLED led;
    std::atomic<bool> recoveryPulsePending{false};

    // Flash test record: 00 marker, 340 XYZ sets, three FF padding bytes,
    // and four zeroed reserved CRC bytes in a caller-owned 2048-byte page.
    static constexpr size_t AccelSamplesPerPage = W25N01GV::PayloadBytes / 6;
    static constexpr size_t CapturePageCount = 3;
    enum class CapturePageState : uint8_t { Free, Filling, Ready, InFlight };
    enum class FlashTestState : uint8_t { Error, Initializing, Recording, Reading, Full };
    FlashTestState flashTestState_ = FlashTestState::Error;
    W25N01GV::Result flashTestResult_ = W25N01GV::Result::NotConnected;
    alignas(4) uint8_t capturePages_[CapturePageCount][W25N01GV::PageBytes]{};
    CapturePageState capturePageStates_[CapturePageCount]{};
    uint8_t readPayload_[W25N01GV::PayloadBytes]{};
    size_t captureWriteIndex_ = 0;
    size_t captureReadIndex_ = 0;
    size_t inFlightPageIndex_ = CapturePageCount;
    size_t readyPageCount_ = 0;
    size_t samplesInCapturePage_ = 0;
    uint32_t droppedAccelSamples_ = 0;
    uint32_t writtenFlashPages_ = 0;
    uint32_t readFlashPages_ = 0;
    bool flashPagePending_ = false;
    bool readPageReady_ = false;
    bool readPlaybackStarted_ = false;
    bool readLogEmpty_ = false;
    bool readHalfTick_ = false;
    uint32_t readNextPresentation10us_ = 0;

    // Task callbacks
    static void ProcessLEDTask();
    static void InitADXLTask();
    static void ProcessADXLTask();
    static void ProcessFlashTask();
    static void ProcessADXLData(const int16_t *values, size_t count);
    static void OnADXLRecovery();
    static void RECINDUCE();
};
