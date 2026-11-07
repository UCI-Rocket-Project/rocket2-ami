#pragma once

#include "main.h"
#include "task_scheduler.h"
#include "led.h"
#include "accel_adxl383.h"
#include "w25n01gv.h"
#include "usb_cdc_serial.h"
#include <atomic>
#include <cstddef>
#include <cstdint>

#define MS 100

class AMIController {
public:
    static AMIController *instance;
    AMIController();
    void Init();
    void Run();

    ADXL383 accel;
    W25N01GV flash;
    UsbCdcSerial usb;

private:
    static constexpr size_t PageTimestampBytes = sizeof(uint32_t);
    static constexpr size_t AccelSamplesPerPage =
        (W25N01GV::PayloadBytes - PageTimestampBytes) / 6;
    static constexpr size_t CapturePageCount = 3;
    static constexpr size_t NoticeCount = 8;
    static constexpr size_t NoticeBytes = 192;
    static constexpr uint16_t ArmCheckStride = 32; // 32 ksps / 32 = 1 kHz
    static constexpr uint16_t ArmCalibrationChecks = 100; // 100 ms
    static constexpr uint16_t ArmHoldChecks = 100; // 100 ms
    static constexpr uint16_t DefaultArmThresholdCounts = 1000; // 2 g

    enum class CapturePageState : uint8_t { Free, Filling, Ready, InFlight };
    enum class Mode : uint8_t {
        Mounting, Idle, Armed, Erasing, Recording, Stopping, ReadOne, DumpCsv, DumpBin,
        Full, Error
    };

    TaskScheduler<10, 3> scheduler;
    size_t ledTaskId = 0;
    size_t adxlTaskId = 0;
    size_t adxlInitTaskId = 0;
    size_t flashTaskId = 0;
    size_t usbTaskId = 0;
    RGBLED led;
    std::atomic<bool> recoveryPulsePending{false};
    bool accelReady_ = false;
    Mode mode_ = Mode::Mounting;
    Mode returnModeAfterStream_ = Mode::Idle;
    W25N01GV::Result flashResult_ = W25N01GV::Result::NotConnected;
    uint8_t eraseConfirmCount_ = 0;
    std::atomic<bool> armResetPending_{false};
    uint16_t armThresholdCounts_ = DefaultArmThresholdCounts;
    uint16_t armStrideCount_ = 0;
    uint16_t armBaselineCount_ = 0;
    uint16_t armAboveCount_ = 0;
    int32_t armBaselineSum_[AXES]{};
    int32_t armBaseline_[AXES]{};

    // DMA owns an InFlight page until the append completes.
    alignas(4) uint8_t capturePages_[CapturePageCount][W25N01GV::PageBytes]{};
    CapturePageState capturePageStates_[CapturePageCount]{};
    size_t captureWriteIndex_ = 0;
    size_t captureReadIndex_ = 0;
    size_t inFlightPageIndex_ = CapturePageCount;
    size_t readyPageCount_ = 0;
    size_t samplesInCapturePage_ = 0;
    bool flashPagePending_ = false;
    bool stopRequested_ = false;
    uint32_t droppedAccelSamples_ = 0;
    uint32_t capturedAccelSamples_ = 0;
    uint32_t writtenFlashPages_ = 0;
    uint32_t flashAppendStart10us_ = 0;

    // One sequential read at a time. The payload remains stable throughout USB TX.
    uint8_t readPayload_[W25N01GV::PayloadBytes]{};
    bool readPending_ = false;
    bool pageReady_ = false;
    bool streamEnding_ = false;
    bool streamAborted_ = false;
    bool abortRequested_ = false;
    bool binaryEndSent_ = false;
    bool csvHeaderPending_ = false;
    bool readOneComplete_ = false;
    uint8_t binaryStage_ = 0;
    uint32_t readIndex_ = 0;
    uint32_t readTargetIndex_ = 0;
    size_t csvSample_ = 0;
    uint8_t binaryHeader_[12]{};
    uint8_t binaryEnd_[8]{};
    char csvChunk_[256]{};

    // Notices and command parsing are foreground-only; RX bytes arrive in IRQ.
    char notices_[NoticeCount][NoticeBytes]{};
    uint8_t noticeHead_ = 0;
    uint8_t noticeTail_ = 0;
    uint8_t noticeCount_ = 0;
    bool noticeInFlight_ = false;
    char command_[64]{};
    size_t commandLength_ = 0;
    bool commandOverflow_ = false;
    uint32_t seenRxOverflows_ = 0;

    void QueueNotice(const char *format, ...);
    void HandleCommand(char *line);
    void BeginStream(Mode mode, uint32_t targetIndex = 0);
    void ResetCaptureState();
    void ResetArmDetection();
    void ProcessArmSample(const int16_t *xyz);
    void SetDefaultLed(RGBLED::Color color);
    void ProcessStreamOutput();
    void FinishStream();
    static const char *ModeName(Mode mode);

    static void ProcessLEDTask();
    static void InitADXLTask();
    static void ProcessADXLTask();
    static void ProcessFlashTask();
    static void ProcessUSBTask();
    static void ProcessADXLData(const int16_t *values, size_t count);
    static void OnADXLRecovery();
};
