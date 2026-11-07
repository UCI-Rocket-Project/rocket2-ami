#include "ami_controller.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

volatile float flashProcessLastMs = 0.0f;
volatile float flashProcessMaxMs = 0.0f;
volatile uint32_t flashAppendLastUs = 0;
volatile uint32_t flashAppendMaxUs = 0;
static float flashCyclesToMs = 0.0f;
static uint32_t flashProcessMaxCycles = 0;

namespace {
constexpr RGBLED::Color NormalPulse{0, 0, RGBLED::MaxBrightness * 2 / 5};
constexpr RGBLED::Color ArmedPulse{RGBLED::MaxBrightness * 2 / 5, 0, 0};
constexpr RGBLED::Color FullPulse{RGBLED::MaxBrightness * 2 / 5, 0,
                                   RGBLED::MaxBrightness * 2 / 5};
constexpr uint32_t DefaultPulsePeriod = 2000U * MS;
constexpr uint32_t RecordingBlinkPeriod = 500U * MS;
constexpr uint32_t FlightBlinkDuration = 1000U * MS;
constexpr uint32_t FlightBlinkPeriod = 100U * MS; // 100 ms on/off cycle
constexpr uint32_t FlashActivityPulsePeriod = 100U * MS; // 10 Hz
constexpr RGBLED::Color FlashActivityColor{
    RGBLED::MaxBrightness,
    RGBLED::MaxBrightness * 5 / 100,
    RGBLED::MaxBrightness * 30 / 100
};
}

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;

AMIController *AMIController::instance = nullptr;

static uint32_t GetCurrentTime10us() {
    uint32_t high1, low, high2;
    do {
        high1 = __HAL_TIM_GET_COUNTER(&htim5);
        low = __HAL_TIM_GET_COUNTER(&htim4);
        high2 = __HAL_TIM_GET_COUNTER(&htim5);
    } while (high1 != high2);
    return (high1 << 16) | low;
}

static int16_t DecodeAxis(const uint8_t *data) {
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) |
                                (static_cast<uint16_t>(data[1]) << 8));
}

static uint32_t DecodeTimestamp(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

static const char *FlashResultName(W25N01GV::Result result) {
    switch (result) {
    case W25N01GV::Result::Ok: return "ok";
    case W25N01GV::Result::Pending: return "pending";
    case W25N01GV::Result::EndOfLog: return "end-of-log";
    case W25N01GV::Result::Full: return "full";
    case W25N01GV::Result::NotConnected: return "not-connected";
    case W25N01GV::Result::NotMounted: return "not-mounted";
    case W25N01GV::Result::Busy: return "busy";
    case W25N01GV::Result::BadArgument: return "bad-argument";
    case W25N01GV::Result::SpiError: return "spi-error";
    case W25N01GV::Result::Timeout: return "timeout";
    case W25N01GV::Result::WrongDevice: return "wrong-device";
    case W25N01GV::Result::ConfigurationError: return "configuration-error";
    case W25N01GV::Result::EraseFailed: return "erase-failed";
    case W25N01GV::Result::ProgramFailed: return "program-failed";
    case W25N01GV::Result::CheckpointFailed: return "checkpoint-failed";
    case W25N01GV::Result::IncompatibleFormat: return "incompatible-format";
    }
    return "unknown";
}

AMIController::AMIController()
    : accel(&hspi1, ACCEL_nCS_GPIO_Port, ACCEL_nCS_Pin, ACCEL_INT0_Pin,
            ProcessADXLData, OnADXLRecovery, GetCurrentTime10us),
      flash(&hspi2, MEM_nCS_GPIO_Port, MEM_nCS_Pin,
            MEM_nWP_GPIO_Port, MEM_nWP_Pin, MEM_nHOLD_GPIO_Port, MEM_nHOLD_Pin),
      scheduler(GetCurrentTime10us),
      led(&htim2, TIM_CHANNEL_4, &htim2, TIM_CHANNEL_3, &htim3, TIM_CHANNEL_4) {
    instance = this;
    led.SetDefaultPulse(NormalPulse, DefaultPulsePeriod);
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

    HAL_TIM_Base_Start(&htim5);
    HAL_TIM_Base_Start(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0);
    __HAL_TIM_SET_COUNTER(&htim5, 0);
    htim4.Instance->EGR = TIM_EGR_UG;
    htim5.Instance->EGR = TIM_EGR_UG;

    led.Init();
    flashResult_ = flash.ConnectBlocking();
    if (flashResult_ == W25N01GV::Result::Ok) {
        flashResult_ = flash.MountAsync();
        mode_ = flashResult_ == W25N01GV::Result::Pending ? Mode::Mounting : Mode::Error;
    } else {
        mode_ = Mode::Error;
    }
    if (mode_ == Mode::Error) {
        led.QueueBlink({RGBLED::MaxBrightness, 0, 0}, 3000 * MS, 500 * MS);
    }

    ledTaskId = *scheduler.AddTask(ProcessLEDTask, 0);
    adxlTaskId = *scheduler.AddTask(ProcessADXLTask, 0);
    adxlInitTaskId = *scheduler.AddTask(InitADXLTask, 0);
    flashTaskId = *scheduler.AddTask(ProcessFlashTask, 0);
    usbTaskId = *scheduler.AddTask(ProcessUSBTask, 0);
    scheduler.ScheduleTaskDeltaTime(adxlInitTaskId, 10 * 1000 * MS);
    scheduler.ScheduleTaskDeltaTime(flashTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(usbTaskId, 0);
    scheduler.ScheduleTaskDeltaTime(ledTaskId, 0);
}

void AMIController::Run() {
    while (true) scheduler.Run();
}

void AMIController::ProcessLEDTask() {
    AMIController &self = *instance;
    const bool flashActivity = self.mode_ == Mode::Erasing ||
                               self.mode_ == Mode::DumpCsv ||
                               self.mode_ == Mode::DumpBin;
    if (self.recoveryPulsePending.exchange(false, std::memory_order_relaxed)) {
        if (!flashActivity &&
            !self.led.QueuePulse({RGBLED::MaxBrightness, RGBLED::MaxBrightness / 8, 0},
                                 3000 * MS, 500 * MS)) {
            self.recoveryPulsePending.store(true, std::memory_order_relaxed);
        }
    }
    const uint32_t now10us = GetCurrentTime10us();
    self.led.Process(now10us);
    if (flashActivity) {
        // Display an operation indicator without changing the default or
        // adding to the LED queue. Queued effects continue underneath it.
        const uint32_t phase = now10us % FlashActivityPulsePeriod;
        const uint32_t distanceToEdge = phase < FlashActivityPulsePeriod / 2U
            ? phase : FlashActivityPulsePeriod - phase;
        const uint32_t level = 2U * distanceToEdge * RGBLED::MaxBrightness /
                               FlashActivityPulsePeriod;
        self.led.setRedBrightness(static_cast<uint16_t>(
            static_cast<uint32_t>(FlashActivityColor.red) * level / RGBLED::MaxBrightness));
        self.led.setGreenBrightness(static_cast<uint16_t>(
            static_cast<uint32_t>(FlashActivityColor.green) * level / RGBLED::MaxBrightness));
        self.led.setBlueBrightness(static_cast<uint16_t>(
            static_cast<uint32_t>(FlashActivityColor.blue) * level / RGBLED::MaxBrightness));
    }
    self.scheduler.ScheduleTaskDeltaTime(self.ledTaskId, 1 * MS);
}

void AMIController::InitADXLTask() {
    AMIController &self = *instance;
    if (self.mode_ == Mode::Mounting || self.mode_ == Mode::Erasing) {
        self.scheduler.ScheduleTaskDeltaTime(self.adxlInitTaskId, 100 * MS);
        return;
    }
    self.accelReady_ = self.accel.Init();
    if (self.accelReady_) {
        if (self.mode_ != Mode::Error) {
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

const char *AMIController::ModeName(Mode mode) {
    switch (mode) {
    case Mode::Mounting: return "mounting";
    case Mode::Idle: return "idle";
    case Mode::Armed: return "armed";
    case Mode::Erasing: return "erasing";
    case Mode::Recording: return "recording";
    case Mode::Stopping: return "stopping";
    case Mode::ReadOne: return "reading";
    case Mode::DumpCsv: return "dump-csv";
    case Mode::DumpBin: return "dump-bin";
    case Mode::Full: return "full";
    case Mode::Error: return "error";
    }
    return "unknown";
}

void AMIController::QueueNotice(const char *format, ...) {
    if (noticeCount_ == NoticeCount) return;
    char *const slot = notices_[noticeTail_];
    va_list args;
    va_start(args, format);
    std::vsnprintf(slot, NoticeBytes, format, args);
    va_end(args);
    noticeTail_ = static_cast<uint8_t>((noticeTail_ + 1U) % NoticeCount);
    ++noticeCount_;
}

void AMIController::BeginStream(Mode mode, uint32_t targetIndex) {
    flash.RewindReadCursor();
    returnModeAfterStream_ = mode_;
    mode_ = mode;
    readTargetIndex_ = targetIndex;
    readIndex_ = 0;
    readPending_ = false;
    pageReady_ = false;
    streamEnding_ = false;
    streamAborted_ = false;
    abortRequested_ = false;
    binaryEndSent_ = false;
    binaryStage_ = 0;
    csvHeaderPending_ = mode != Mode::DumpBin;
    readOneComplete_ = false;
    csvSample_ = 0;
}

void AMIController::ResetCaptureState() {
    for (auto &state : capturePageStates_) state = CapturePageState::Free;
    capturePageStates_[0] = CapturePageState::Filling;
    captureWriteIndex_ = 0;
    captureReadIndex_ = 0;
    inFlightPageIndex_ = CapturePageCount;
    readyPageCount_ = 0;
    samplesInCapturePage_ = 0;
    flashPagePending_ = false;
    stopRequested_ = false;
    writtenFlashPages_ = 0;
    droppedAccelSamples_ = 0;
    capturedAccelSamples_ = 0;
    flashAppendLastUs = 0;
    flashAppendMaxUs = 0;
}

void AMIController::ResetArmDetection() {
    armStrideCount_ = 0;
    armBaselineCount_ = 0;
    armAboveCount_ = 0;
    for (unsigned axis = 0; axis < AXES; ++axis) {
        armBaselineSum_[axis] = 0;
        armBaseline_[axis] = 0;
    }
}

void AMIController::SetDefaultLed(RGBLED::Color color) {
    led.SetDefaultPulse(color, DefaultPulsePeriod);
}

void AMIController::ProcessArmSample(const int16_t *xyz) {
    if (++armStrideCount_ < ArmCheckStride) return;
    armStrideCount_ = 0;
    if (armResetPending_.exchange(false, std::memory_order_relaxed)) ResetArmDetection();

    if (armBaselineCount_ < ArmCalibrationChecks) {
        for (unsigned axis = 0; axis < AXES; ++axis) armBaselineSum_[axis] += xyz[axis];
        if (++armBaselineCount_ == ArmCalibrationChecks) {
            for (unsigned axis = 0; axis < AXES; ++axis) {
                armBaseline_[axis] = armBaselineSum_[axis] / ArmCalibrationChecks;
            }
        }
        return;
    }

    // The baseline includes gravity at any setup angle and sensor bias.
    // Squared magnitudes avoid a square root in the accelerometer path.
    int64_t motionSquared = 0;
    for (unsigned axis = 0; axis < AXES; ++axis) {
        const int32_t delta = static_cast<int32_t>(xyz[axis]) - armBaseline_[axis];
        motionSquared += static_cast<int64_t>(delta) * delta;
    }
    const int64_t thresholdSquared =
        static_cast<int64_t>(armThresholdCounts_) * armThresholdCounts_;
    if (motionSquared < thresholdSquared) {
        armAboveCount_ = 0;
        return;
    }
    if (++armAboveCount_ == ArmHoldChecks) {
        mode_ = Mode::Recording;
        stopRequested_ = false;
        led.SetDefaultBlink(NormalPulse, RecordingBlinkPeriod);
        led.QueueBlink({0, RGBLED::MaxBrightness, 0},
                       FlightBlinkDuration, FlightBlinkPeriod);
        QueueNotice("TRIGGER recording after 100 ms above threshold\r\n");
    }
}

void AMIController::HandleCommand(char *line) {
    while (*line == ' ') ++line;
    char *endOfLine = line + std::strlen(line);
    while (endOfLine > line && endOfLine[-1] == ' ') *--endOfLine = '\0';
    if (!*line) {
        eraseConfirmCount_ = 0;
        return;
    }
    // Only three consecutive, separate erase commands can start a format.
    if (std::strcmp(line, "erase") != 0) eraseConfirmCount_ = 0;
    if (mode_ == Mode::ReadOne || mode_ == Mode::DumpCsv || mode_ == Mode::DumpBin) {
        if (std::strcmp(line, "abort") == 0) abortRequested_ = true;
        return; // No text response may be interleaved with a stream.
    }
    if (std::strcmp(line, "help") == 0) {
        QueueNotice("help | status | erase | arm [g] | disarm | record start | record stop | read <n> | dump csv | dump bin | abort\r\n");
        QueueNotice("read uses zero-based logical page numbers; dump bin uses UCP2 frames.\r\n");
        QueueNotice("erase requires three consecutive erase commands.\r\n");
        QueueNotice("arm defaults to 2 g motion after gravity baseline; hold 100 ms.\r\n");
    } else if (std::strcmp(line, "status") == 0) {
        QueueNotice("STATUS mode=%s accel=%u connected=%u mounted=%u result=%s next=%u:%u written=%lu captured=%lu dropped=%lu rx_overflow=%lu\r\n",
                    ModeName(mode_), accelReady_ ? 1U : 0U, flash.Connected() ? 1U : 0U,
                    flash.Mounted() ? 1U : 0U,
                    FlashResultName(flashResult_), flash.NextBlock(), flash.NextPage(),
                    static_cast<unsigned long>(writtenFlashPages_),
                    static_cast<unsigned long>(capturedAccelSamples_),
                    static_cast<unsigned long>(droppedAccelSamples_),
                    static_cast<unsigned long>(usb.RxOverflows()));
        if (mode_ == Mode::Armed) {
            QueueNotice("ARM threshold_mg=%u baseline_ms=%u/%u hold_ms=%u/%u\r\n",
                        static_cast<unsigned>(armThresholdCounts_) * 2U,
                        static_cast<unsigned>(armBaselineCount_),
                        static_cast<unsigned>(ArmCalibrationChecks),
                        static_cast<unsigned>(armAboveCount_),
                        static_cast<unsigned>(ArmHoldChecks));
        }
    } else if (std::strcmp(line, "erase") == 0) {
        if (mode_ != Mode::Idle && mode_ != Mode::Error && mode_ != Mode::Full) {
            eraseConfirmCount_ = 0;
            QueueNotice("ERR busy\r\n");
            return;
        }
        if (mode_ == Mode::Error &&
            (flashResult_ == W25N01GV::Result::SpiError ||
             flashResult_ == W25N01GV::Result::Timeout)) {
            eraseConfirmCount_ = 0;
            QueueNotice("ERR reset board after SPI failure\r\n");
            return;
        }
        if (++eraseConfirmCount_ < 3) {
            QueueNotice("ERASE %u/3; type erase %u more time%s to erase all data\r\n",
                        static_cast<unsigned>(eraseConfirmCount_),
                        3U - static_cast<unsigned>(eraseConfirmCount_),
                        eraseConfirmCount_ == 2 ? "" : "s");
            return;
        }
        eraseConfirmCount_ = 0;
        flashResult_ = flash.FormatAsync();
        if (flashResult_ == W25N01GV::Result::Pending) {
            mode_ = Mode::Erasing;
            QueueNotice("ERASE 3/3; OK erase started\r\n");
        } else {
            QueueNotice("ERR erase result=%s\r\n", FlashResultName(flashResult_));
        }
    } else if (std::strcmp(line, "arm") == 0 || std::strncmp(line, "arm ", 4) == 0) {
        if (mode_ != Mode::Idle || !flash.Mounted() || !accelReady_) {
            QueueNotice("ERR not ready mode=%s accel=%u\r\n", ModeName(mode_), accelReady_ ? 1U : 0U);
            return;
        }
        uint16_t thresholdCounts = DefaultArmThresholdCounts;
        if (line[3] != '\0') {
            char *end = nullptr;
            const float thresholdG = std::strtof(line + 4, &end);
            if (end == line + 4 || *end != '\0' ||
                !(thresholdG > 0.0f && thresholdG <= 60.0f)) {
                QueueNotice("ERR use arm [threshold_g], between 0 and 60 g\r\n");
                return;
            }
            thresholdCounts = static_cast<uint16_t>(thresholdG * 500.0f + 0.5f);
            if (thresholdCounts == 0) {
                QueueNotice("ERR threshold too small\r\n");
                return;
            }
        }
        armThresholdCounts_ = thresholdCounts;
        armResetPending_.store(false, std::memory_order_relaxed);
        ResetArmDetection();
        mode_ = Mode::Armed;
        SetDefaultLed(ArmedPulse);
        QueueNotice("OK armed threshold_mg=%u; calibrating 100 ms, then hold 100 ms\r\n",
                    static_cast<unsigned>(armThresholdCounts_) * 2U);
    } else if (std::strcmp(line, "disarm") == 0) {
        if (mode_ != Mode::Armed) {
            QueueNotice("ERR not armed\r\n");
            return;
        }
        mode_ = Mode::Idle;
        ResetArmDetection();
        SetDefaultLed(NormalPulse);
        QueueNotice("OK disarmed\r\n");
    } else if (std::strcmp(line, "record start") == 0) {
        if ((mode_ != Mode::Idle && mode_ != Mode::Armed) ||
            !flash.Mounted() || !accelReady_) {
            QueueNotice("ERR not ready mode=%s accel=%u\r\n", ModeName(mode_), accelReady_ ? 1U : 0U);
            return;
        }
        if (mode_ == Mode::Armed) SetDefaultLed(NormalPulse);
        ResetArmDetection();
        stopRequested_ = false;
        mode_ = Mode::Recording;
        QueueNotice("OK recording\r\n");
    } else if (std::strcmp(line, "record stop") == 0) {
        if (mode_ != Mode::Recording) {
            QueueNotice("ERR not recording\r\n");
            return;
        }
        stopRequested_ = true;
        if (samplesInCapturePage_ == 0) mode_ = Mode::Stopping;
        QueueNotice("OK stopping at page boundary\r\n");
    } else if (std::strncmp(line, "read ", 5) == 0) {
        char *end = nullptr;
        const unsigned long target = std::strtoul(line + 5, &end, 10);
        if ((mode_ != Mode::Idle && mode_ != Mode::Full) ||
            line[5] < '0' || line[5] > '9' ||
            end == line + 5 || *end ||
            target >= W25N01GV::BlockCount * (W25N01GV::PagesPerBlock - 1U) ||
            !flash.Mounted()) {
            QueueNotice("ERR read argument or busy\r\n");
            return;
        }
        BeginStream(Mode::ReadOne, static_cast<uint32_t>(target));
        QueueNotice("OK read %lu\r\n", target);
    } else if (std::strcmp(line, "dump csv") == 0 || std::strcmp(line, "dump bin") == 0) {
        if ((mode_ != Mode::Idle && mode_ != Mode::Full) || !flash.Mounted()) {
            QueueNotice("ERR busy or not mounted\r\n");
            return;
        }
        const bool binary = std::strcmp(line, "dump bin") == 0;
        BeginStream(binary ? Mode::DumpBin : Mode::DumpCsv);
        QueueNotice("OK dump %s\r\n", binary ? "bin" : "csv");
    } else if (std::strcmp(line, "abort") == 0) {
        QueueNotice("ERR no active transfer\r\n");
    } else {
        QueueNotice("ERR unknown command\r\n");
    }
}

void AMIController::ProcessFlashTask() {
    const uint32_t startCycles = DWT->CYCCNT;
    AMIController &self = *instance;
    self.flash.ProcessTask();

    if (self.mode_ == Mode::Mounting && !self.flash.Busy()) {
        self.flashResult_ = self.flash.LastResult();
        self.mode_ = self.flashResult_ != W25N01GV::Result::Ok ? Mode::Error :
            (self.flash.NextBlock() >= W25N01GV::BlockCount ? Mode::Full : Mode::Idle);
        if (self.mode_ == Mode::Full) self.SetDefaultLed(FullPulse);
        if (self.mode_ == Mode::Error) self.QueueNotice("ERR mount result=%s\r\n", FlashResultName(self.flashResult_));
    } else if (self.mode_ == Mode::Erasing && !self.flash.Busy()) {
        self.flashResult_ = self.flash.LastResult();
        if (self.flashResult_ == W25N01GV::Result::Ok) {
            self.ResetCaptureState();
            self.mode_ = Mode::Idle;
            self.SetDefaultLed(NormalPulse);
            self.QueueNotice("DONE erase\r\n");
        } else {
            self.mode_ = Mode::Error;
            self.QueueNotice("ERR erase result=%s\r\n", FlashResultName(self.flashResult_));
        }
    }

    if (self.mode_ == Mode::Recording || self.mode_ == Mode::Stopping) {
        if (self.flashPagePending_ && !self.flash.Busy()) {
            self.flashPagePending_ = false;
            flashAppendLastUs = (GetCurrentTime10us() - self.flashAppendStart10us_) * 10U;
            if (flashAppendLastUs > flashAppendMaxUs) flashAppendMaxUs = flashAppendLastUs;
            self.flashResult_ = self.flash.LastResult();
            if (self.flashResult_ == W25N01GV::Result::Ok) {
                self.capturePageStates_[self.inFlightPageIndex_] = CapturePageState::Free;
                self.inFlightPageIndex_ = CapturePageCount;
                if (self.capturePageStates_[self.captureWriteIndex_] == CapturePageState::Free) {
                    self.capturePageStates_[self.captureWriteIndex_] = CapturePageState::Filling;
                }
                ++self.writtenFlashPages_;
                if (self.flash.NextBlock() >= W25N01GV::BlockCount) {
                    self.mode_ = Mode::Full;
                    self.SetDefaultLed(FullPulse);
                    self.QueueNotice("DONE flash full pages=%lu\r\n",
                                     static_cast<unsigned long>(self.writtenFlashPages_));
                }
            } else {
                // A timed-out DMA may still reference this page, so keep it reserved.
                self.mode_ = self.flashResult_ == W25N01GV::Result::Full ? Mode::Full : Mode::Error;
                if (self.mode_ == Mode::Full) self.SetDefaultLed(FullPulse);
                self.QueueNotice("ERR write result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
        if ((self.mode_ == Mode::Recording || self.mode_ == Mode::Stopping) &&
            !self.flashPagePending_ && self.readyPageCount_ != 0) {
            self.flashResult_ = self.flash.AppendPageAsync(self.capturePages_[self.captureReadIndex_]);
            if (self.flashResult_ == W25N01GV::Result::Pending) {
                self.flashAppendStart10us_ = GetCurrentTime10us();
                self.capturePageStates_[self.captureReadIndex_] = CapturePageState::InFlight;
                self.inFlightPageIndex_ = self.captureReadIndex_;
                self.captureReadIndex_ = (self.captureReadIndex_ + 1) % CapturePageCount;
                --self.readyPageCount_;
                self.flashPagePending_ = true;
            } else if (self.flashResult_ != W25N01GV::Result::Busy) {
                self.mode_ = self.flashResult_ == W25N01GV::Result::Full ? Mode::Full : Mode::Error;
                if (self.mode_ == Mode::Full) self.SetDefaultLed(FullPulse);
                self.QueueNotice("ERR write result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
        if (self.mode_ == Mode::Stopping && !self.flashPagePending_ &&
            self.readyPageCount_ == 0) {
            self.mode_ = Mode::Idle;
            self.SetDefaultLed(NormalPulse);
            self.stopRequested_ = false;
            self.QueueNotice("DONE record pages=%lu captured=%lu dropped=%lu\r\n",
                             static_cast<unsigned long>(self.writtenFlashPages_),
                             static_cast<unsigned long>(self.capturedAccelSamples_),
                             static_cast<unsigned long>(self.droppedAccelSamples_));
        }
    }

    const bool streaming = self.mode_ == Mode::ReadOne ||
                           self.mode_ == Mode::DumpCsv || self.mode_ == Mode::DumpBin;
    if (streaming) {
        if (self.readPending_ && !self.flash.Busy()) {
            self.readPending_ = false;
            self.flashResult_ = self.flash.LastResult();
            if (self.abortRequested_) {
                self.streamAborted_ = true;
                self.streamEnding_ = true;
            } else if (self.flashResult_ == W25N01GV::Result::Ok) {
                if (self.mode_ == Mode::ReadOne && self.readIndex_ < self.readTargetIndex_) {
                    ++self.readIndex_;
                } else {
                    self.pageReady_ = true;
                }
            } else if (self.flashResult_ == W25N01GV::Result::EndOfLog) {
                if (self.mode_ == Mode::ReadOne) {
                    self.mode_ = self.returnModeAfterStream_;
                    self.QueueNotice("ERR page out of range\r\n");
                } else {
                    self.streamEnding_ = true;
                }
            } else {
                self.mode_ = Mode::Error;
                self.QueueNotice("ERR read result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
        if ((self.mode_ == Mode::ReadOne || self.mode_ == Mode::DumpCsv ||
             self.mode_ == Mode::DumpBin) && !self.readPending_ &&
            !self.pageReady_ && !self.streamEnding_ && !self.abortRequested_) {
            self.flashResult_ = self.flash.ReadNextPageAsync(self.readPayload_);
            if (self.flashResult_ == W25N01GV::Result::Pending) {
                self.readPending_ = true;
            } else if (self.flashResult_ == W25N01GV::Result::EndOfLog) {
                if (self.mode_ == Mode::ReadOne) {
                    self.mode_ = self.returnModeAfterStream_;
                    self.QueueNotice("ERR page out of range\r\n");
                } else {
                    self.streamEnding_ = true;
                }
            } else if (self.flashResult_ != W25N01GV::Result::Busy) {
                self.mode_ = Mode::Error;
                self.QueueNotice("ERR read result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
    }

    // A page arrives about every 10.6 ms at 32 ksps. Poll the multi-command
    // flash write more often while recording, without increasing idle CPU load.
    const uint32_t flashPeriod = (self.mode_ == Mode::Recording ||
                                  self.mode_ == Mode::Stopping) ? MS / 5U : MS;
    self.scheduler.ScheduleTaskDeltaTime(self.flashTaskId, flashPeriod);
    const uint32_t elapsedCycles = DWT->CYCCNT - startCycles;
    flashProcessLastMs = static_cast<float>(elapsedCycles) * flashCyclesToMs;
    if (elapsedCycles > flashProcessMaxCycles) {
        flashProcessMaxCycles = elapsedCycles;
        flashProcessMaxMs = flashProcessLastMs;
    }
}

void AMIController::FinishStream() {
    const Mode finished = mode_;
    mode_ = returnModeAfterStream_;
    pageReady_ = false;
    readPending_ = false;
    streamEnding_ = false;
    abortRequested_ = false;
    if (finished != Mode::DumpBin) {
        QueueNotice("DONE %s pages=%lu%s\r\n",
                    finished == Mode::ReadOne ? "read" : "dump",
                    static_cast<unsigned long>(finished == Mode::ReadOne ?
                        (readOneComplete_ ? 1U : 0U) : readIndex_),
                    streamAborted_ ? " aborted" : "");
    }
}

void AMIController::ProcessStreamOutput() {
    // A binary frame must finish once its header has been sent, otherwise the
    // receiver would mistake the end marker for page payload bytes.
    const bool binaryFrameStarted = mode_ == Mode::DumpBin && pageReady_ &&
                                    binaryStage_ != 0 && usb.Connected();
    if (abortRequested_ && !readPending_ && !binaryFrameStarted) {
        streamAborted_ = true;
        pageReady_ = false;
        streamEnding_ = true;
    }
    if (csvHeaderPending_ && (mode_ == Mode::DumpCsv || pageReady_)) {
        static const char header[] = "page,sample,page_time10us,x,y,z\r\n";
        if (usb.Write(reinterpret_cast<const uint8_t *>(header), sizeof(header) - 1)) {
            csvHeaderPending_ = false;
        }
        return;
    }
    if (streamEnding_ && !pageReady_) {
        if (mode_ == Mode::DumpBin) {
            if (!binaryEndSent_) {
                std::memcpy(binaryEnd_, streamAborted_ ? "UABT" : "UEND", 4);
                for (size_t i = 0; i < 4; ++i) {
                    binaryEnd_[4 + i] = static_cast<uint8_t>(readIndex_ >> (8 * i));
                }
                if (usb.Write(binaryEnd_, sizeof(binaryEnd_))) binaryEndSent_ = true;
            } else {
                FinishStream();
            }
        } else {
            FinishStream();
        }
        return;
    }
    if (!pageReady_) return;

    if (mode_ == Mode::DumpBin) {
        if (binaryStage_ == 0) {
            std::memcpy(binaryHeader_, "UCP2", 4);
            for (size_t i = 0; i < 4; ++i) {
                binaryHeader_[4 + i] = static_cast<uint8_t>(readIndex_ >> (8 * i));
            }
            binaryHeader_[8] = static_cast<uint8_t>(W25N01GV::PayloadBytes);
            binaryHeader_[9] = static_cast<uint8_t>(W25N01GV::PayloadBytes >> 8);
            binaryHeader_[10] = static_cast<uint8_t>(AccelSamplesPerPage);
            binaryHeader_[11] = static_cast<uint8_t>(AccelSamplesPerPage >> 8);
            if (usb.Write(binaryHeader_, sizeof(binaryHeader_))) binaryStage_ = 1;
        } else if (binaryStage_ == 1) {
            if (usb.Write(readPayload_, sizeof(readPayload_))) binaryStage_ = 2;
        } else {
            pageReady_ = false;
            binaryStage_ = 0;
            ++readIndex_;
        }
        return;
    }

    if (csvSample_ == AccelSamplesPerPage) {
        pageReady_ = false;
        csvSample_ = 0;
        ++readIndex_;
        if (mode_ == Mode::ReadOne) {
            readOneComplete_ = true;
            streamEnding_ = true;
        }
        return;
    }

    const uint32_t timestamp = DecodeTimestamp(readPayload_);
    size_t used = 0;
    size_t samples = 0;
    while (csvSample_ + samples < AccelSamplesPerPage) {
        const size_t offset = PageTimestampBytes + (csvSample_ + samples) * 6;
        const int written = std::snprintf(csvChunk_ + used, sizeof(csvChunk_) - used,
                                          "%lu,%u,%lu,%d,%d,%d\r\n",
                                          static_cast<unsigned long>(readIndex_),
                                          static_cast<unsigned>(csvSample_ + samples),
                                          static_cast<unsigned long>(timestamp),
                                          DecodeAxis(readPayload_ + offset),
                                          DecodeAxis(readPayload_ + offset + 2),
                                          DecodeAxis(readPayload_ + offset + 4));
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(csvChunk_) - used) break;
        used += static_cast<size_t>(written);
        ++samples;
    }
    if (used && usb.Write(reinterpret_cast<const uint8_t *>(csvChunk_), used)) {
        csvSample_ += samples;
    }
}

void AMIController::ProcessUSBTask() {
    AMIController &self = *instance;
    self.usb.ProcessTask();
    if (self.noticeInFlight_ && !self.usb.Busy()) {
        self.noticeInFlight_ = false;
        self.noticeHead_ = static_cast<uint8_t>((self.noticeHead_ + 1U) % NoticeCount);
        --self.noticeCount_;
    }
    if (!self.usb.Connected() && (self.mode_ == Mode::ReadOne ||
        self.mode_ == Mode::DumpCsv || self.mode_ == Mode::DumpBin)) {
        self.abortRequested_ = true;
        if (!self.readPending_) {
            self.mode_ = self.returnModeAfterStream_;
            self.pageReady_ = false;
            self.streamEnding_ = false;
            self.abortRequested_ = false;
        }
    }
    if (!self.usb.Connected()) {
        self.eraseConfirmCount_ = 0;
        self.commandLength_ = 0;
        self.commandOverflow_ = false;
    }
    const uint32_t overflows = self.usb.RxOverflows();
    if (overflows != self.seenRxOverflows_) {
        self.seenRxOverflows_ = overflows;
        self.commandLength_ = 0;
        self.commandOverflow_ = true;
        self.eraseConfirmCount_ = 0;
    }
    uint8_t byte;
    // Keep one notice slot available for an asynchronous DONE/ERR completion.
    for (unsigned i = 0; i < 64 && self.noticeCount_ < NoticeCount - 1 &&
                         self.usb.ReadByte(byte); ++i) {
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (self.commandOverflow_) {
                self.eraseConfirmCount_ = 0;
                if (self.mode_ != Mode::ReadOne && self.mode_ != Mode::DumpCsv &&
                    self.mode_ != Mode::DumpBin) self.QueueNotice("ERR command too long or RX overflow\r\n");
            } else {
                self.command_[self.commandLength_] = '\0';
                self.HandleCommand(self.command_);
            }
            self.commandLength_ = 0;
            self.commandOverflow_ = false;
        } else if (self.commandLength_ + 1 < sizeof(self.command_)) {
            if (!self.commandOverflow_) self.command_[self.commandLength_++] = static_cast<char>(byte);
        } else {
            self.commandOverflow_ = true;
        }
    }
    if (!self.usb.Busy()) {
        if (self.noticeCount_ != 0) {
            if (!self.noticeInFlight_ &&
                self.usb.Write(reinterpret_cast<const uint8_t *>(self.notices_[self.noticeHead_]),
                               std::strlen(self.notices_[self.noticeHead_]))) {
                self.noticeInFlight_ = true;
            }
        } else if (self.mode_ == Mode::ReadOne || self.mode_ == Mode::DumpCsv ||
                   self.mode_ == Mode::DumpBin) {
            self.ProcessStreamOutput();
        }
    }
    self.scheduler.ScheduleTaskDeltaTime(self.usbTaskId, 1 * MS);
}

void AMIController::ProcessADXLData(const int16_t *values, size_t count) {
    AMIController &self = *instance;
    if (self.mode_ != Mode::Recording && self.mode_ != Mode::Armed) return;
    const size_t sampleSets = count / AXES;
    for (size_t i = 0; i < sampleSets; ++i) {
        if (self.mode_ == Mode::Armed) {
            self.ProcessArmSample(values + i * AXES);
            continue;
        }
        if (self.mode_ != Mode::Recording) break;
        CapturePageState &state = self.capturePageStates_[self.captureWriteIndex_];
        if (state == CapturePageState::Free) state = CapturePageState::Filling;
        if (state != CapturePageState::Filling) {
            ++self.droppedAccelSamples_;
            continue;
        }
        uint8_t *const page = self.capturePages_[self.captureWriteIndex_];
        if (self.samplesInCapturePage_ == 0) {
            const uint32_t timestamp = GetCurrentTime10us();
            for (size_t j = 0; j < PageTimestampBytes; ++j) {
                page[1 + j] = static_cast<uint8_t>(timestamp >> (8 * j));
            }
        }
        const size_t pageOffset = 1 + PageTimestampBytes + self.samplesInCapturePage_ * 6;
        for (size_t axis = 0; axis < AXES; ++axis) {
            const uint16_t raw = static_cast<uint16_t>(values[i * AXES + axis]);
            page[pageOffset + axis * 2] = static_cast<uint8_t>(raw);
            page[pageOffset + axis * 2 + 1] = static_cast<uint8_t>(raw >> 8);
        }
        ++self.capturedAccelSamples_;
        if (++self.samplesInCapturePage_ == AccelSamplesPerPage) {
            state = CapturePageState::Ready;
            ++self.readyPageCount_;
            self.captureWriteIndex_ = (self.captureWriteIndex_ + 1) % CapturePageCount;
            self.samplesInCapturePage_ = 0;
            if (self.capturePageStates_[self.captureWriteIndex_] == CapturePageState::Free) {
                self.capturePageStates_[self.captureWriteIndex_] = CapturePageState::Filling;
            }
            if (self.stopRequested_) {
                self.mode_ = Mode::Stopping;
                break;
            }
        }
    }
}

void AMIController::OnADXLRecovery() {
    instance->recoveryPulsePending.store(true, std::memory_order_relaxed);
    instance->armResetPending_.store(true, std::memory_order_relaxed);
}

extern "C" {
void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    AMIController::instance->accel.FIFOWatermarkIntCallback(pin);
}
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi) {
    AMIController::instance->accel.TxCompleteCallback(spi);
    AMIController::instance->flash.TxRxCompleteCallback(spi);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi) {
    AMIController::instance->flash.SpiErrorCallback(spi);
}
}
