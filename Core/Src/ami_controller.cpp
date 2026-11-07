#include "ami_controller.h"
#include "main.h"
#include "stm32f1xx_hal_tim.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

volatile float flashProcessLastMs = 0.0f;
volatile float flashProcessMaxMs = 0.0f;
static float flashCyclesToMs = 0.0f;
static uint32_t flashProcessMaxCycles = 0;

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
    if (self.recoveryPulsePending.exchange(false, std::memory_order_relaxed)) {
        if (!self.led.QueuePulse({RGBLED::MaxBrightness, RGBLED::MaxBrightness / 8, 0},
                                 3000 * MS, 500 * MS)) {
            self.recoveryPulsePending.store(true, std::memory_order_relaxed);
        }
    }
    self.led.Process(GetCurrentTime10us());
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
}

void AMIController::HandleCommand(char *line) {
    while (*line == ' ') ++line;
    char *endOfLine = line + std::strlen(line);
    while (endOfLine > line && endOfLine[-1] == ' ') *--endOfLine = '\0';
    if (!*line) return;
    if (mode_ == Mode::ReadOne || mode_ == Mode::DumpCsv || mode_ == Mode::DumpBin) {
        if (std::strcmp(line, "abort") == 0) abortRequested_ = true;
        return; // No text response may be interleaved with a stream.
    }
    if (std::strcmp(line, "help") == 0) {
        QueueNotice("help | status | erase | record start | record stop | read <n> | dump csv | dump bin | abort\r\n");
        QueueNotice("read uses zero-based logical page numbers; dump bin uses UCP2 frames.\r\n");
    } else if (std::strcmp(line, "status") == 0) {
        QueueNotice("STATUS mode=%s accel=%u connected=%u mounted=%u result=%s next=%u:%u written=%lu dropped=%lu rx_overflow=%lu\r\n",
                    ModeName(mode_), accelReady_ ? 1U : 0U, flash.Connected() ? 1U : 0U,
                    flash.Mounted() ? 1U : 0U,
                    FlashResultName(flashResult_), flash.NextBlock(), flash.NextPage(),
                    static_cast<unsigned long>(writtenFlashPages_),
                    static_cast<unsigned long>(droppedAccelSamples_),
                    static_cast<unsigned long>(usb.RxOverflows()));
    } else if (std::strcmp(line, "erase") == 0) {
        if (mode_ != Mode::Idle && mode_ != Mode::Error && mode_ != Mode::Full) {
            QueueNotice("ERR busy\r\n");
            return;
        }
        if (mode_ == Mode::Error &&
            (flashResult_ == W25N01GV::Result::SpiError ||
             flashResult_ == W25N01GV::Result::Timeout)) {
            QueueNotice("ERR reset board after SPI failure\r\n");
            return;
        }
        flashResult_ = flash.FormatAsync();
        if (flashResult_ == W25N01GV::Result::Pending) {
            mode_ = Mode::Erasing;
            QueueNotice("OK erase started\r\n");
        } else {
            QueueNotice("ERR erase result=%s\r\n", FlashResultName(flashResult_));
        }
    } else if (std::strcmp(line, "record start") == 0) {
        if (mode_ != Mode::Idle || !flash.Mounted() || !accelReady_) {
            QueueNotice("ERR not ready mode=%s accel=%u\r\n", ModeName(mode_), accelReady_ ? 1U : 0U);
            return;
        }
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
        if (mode_ != Mode::Idle || line[5] < '0' || line[5] > '9' ||
            end == line + 5 || *end ||
            target >= W25N01GV::BlockCount * (W25N01GV::PagesPerBlock - 1U) ||
            !flash.Mounted()) {
            QueueNotice("ERR read argument or busy\r\n");
            return;
        }
        BeginStream(Mode::ReadOne, static_cast<uint32_t>(target));
        QueueNotice("OK read %lu\r\n", target);
    } else if (std::strcmp(line, "dump csv") == 0 || std::strcmp(line, "dump bin") == 0) {
        if (mode_ != Mode::Idle || !flash.Mounted()) {
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
        self.mode_ = self.flashResult_ == W25N01GV::Result::Ok ? Mode::Idle : Mode::Error;
        if (self.mode_ == Mode::Error) self.QueueNotice("ERR mount result=%s\r\n", FlashResultName(self.flashResult_));
    } else if (self.mode_ == Mode::Erasing && !self.flash.Busy()) {
        self.flashResult_ = self.flash.LastResult();
        if (self.flashResult_ == W25N01GV::Result::Ok) {
            self.ResetCaptureState();
            self.mode_ = Mode::Idle;
            self.QueueNotice("DONE erase\r\n");
        } else {
            self.mode_ = Mode::Error;
            self.QueueNotice("ERR erase result=%s\r\n", FlashResultName(self.flashResult_));
        }
    }

    if (self.mode_ == Mode::Recording || self.mode_ == Mode::Stopping) {
        if (self.flashPagePending_ && !self.flash.Busy()) {
            self.flashPagePending_ = false;
            self.flashResult_ = self.flash.LastResult();
            if (self.flashResult_ == W25N01GV::Result::Ok) {
                self.capturePageStates_[self.inFlightPageIndex_] = CapturePageState::Free;
                self.inFlightPageIndex_ = CapturePageCount;
                if (self.capturePageStates_[self.captureWriteIndex_] == CapturePageState::Free) {
                    self.capturePageStates_[self.captureWriteIndex_] = CapturePageState::Filling;
                }
                ++self.writtenFlashPages_;
            } else {
                // A timed-out DMA may still reference this page, so keep it reserved.
                self.mode_ = self.flashResult_ == W25N01GV::Result::Full ? Mode::Full : Mode::Error;
                self.QueueNotice("ERR write result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
        if ((self.mode_ == Mode::Recording || self.mode_ == Mode::Stopping) &&
            !self.flashPagePending_ && self.readyPageCount_ != 0) {
            self.flashResult_ = self.flash.AppendPageAsync(self.capturePages_[self.captureReadIndex_]);
            if (self.flashResult_ == W25N01GV::Result::Pending) {
                self.capturePageStates_[self.captureReadIndex_] = CapturePageState::InFlight;
                self.inFlightPageIndex_ = self.captureReadIndex_;
                self.captureReadIndex_ = (self.captureReadIndex_ + 1) % CapturePageCount;
                --self.readyPageCount_;
                self.flashPagePending_ = true;
            } else if (self.flashResult_ != W25N01GV::Result::Busy) {
                self.mode_ = self.flashResult_ == W25N01GV::Result::Full ? Mode::Full : Mode::Error;
                self.QueueNotice("ERR write result=%s\r\n", FlashResultName(self.flashResult_));
            }
        }
        if (self.mode_ == Mode::Stopping && !self.flashPagePending_ &&
            self.readyPageCount_ == 0) {
            self.mode_ = Mode::Idle;
            self.stopRequested_ = false;
            self.QueueNotice("DONE record pages=%lu dropped=%lu\r\n",
                             static_cast<unsigned long>(self.writtenFlashPages_),
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
                    self.mode_ = Mode::Idle;
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
                    self.mode_ = Mode::Idle;
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

    self.scheduler.ScheduleTaskDeltaTime(self.flashTaskId, 1 * MS);
    const uint32_t elapsedCycles = DWT->CYCCNT - startCycles;
    flashProcessLastMs = static_cast<float>(elapsedCycles) * flashCyclesToMs;
    if (elapsedCycles > flashProcessMaxCycles) {
        flashProcessMaxCycles = elapsedCycles;
        flashProcessMaxMs = flashProcessLastMs;
    }
}

void AMIController::FinishStream() {
    const Mode finished = mode_;
    mode_ = Mode::Idle;
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
            self.mode_ = Mode::Idle;
            self.pageReady_ = false;
            self.streamEnding_ = false;
            self.abortRequested_ = false;
        }
    }
    const uint32_t overflows = self.usb.RxOverflows();
    if (overflows != self.seenRxOverflows_) {
        self.seenRxOverflows_ = overflows;
        self.commandLength_ = 0;
        self.commandOverflow_ = true;
    }
    uint8_t byte;
    // Keep one notice slot available for an asynchronous DONE/ERR completion.
    for (unsigned i = 0; i < 64 && self.noticeCount_ < NoticeCount - 1 &&
                         self.usb.ReadByte(byte); ++i) {
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (self.commandOverflow_) {
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
    if (self.mode_ != Mode::Recording) return;
    const size_t sampleSets = count / AXES;
    for (size_t i = 0; i < sampleSets; ++i) {
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
