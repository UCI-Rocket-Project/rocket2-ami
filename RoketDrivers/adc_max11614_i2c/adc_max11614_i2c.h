#pragma once

#include <cstdint>
#include <optional>

#include "main.h"  // # includes "stm32xxxx_hal.h"

#define ADC_MAX_I2C_ADDRESS 0x33  // 0110011
#define ADC_MAX_I2C_TIMEOUT_MS 10
#define ADC_MAX_I2C_RETRY_DELAY_MS 5

class AdcMax11614i2c {
  public:
    static constexpr uint16_t maxChannelOutput = 4095;  // 12-bit ADC max value

    struct Data {
        // 8-bit bitmask indicating whether each channel is good (1 = good, 0 = bad/not read)
        uint8_t channelValidBitmask = 0;

        // adc channel output from 0-4095, with 0 meaning not read/could not be read
        uint16_t channelOutput0 = 0;
        uint16_t channelOutput1 = 0;
        uint16_t channelOutput2 = 0;
        uint16_t channelOutput3 = 0;
        uint16_t channelOutput4 = 0;
        uint16_t channelOutput5 = 0;
        uint16_t channelOutput6 = 0;
        uint16_t channelOutput7 = 0;

        /**
         * @brief check if a specific channel was read successfully
         * @param channel channel index 0-7
         * @retval true if channel is good (bit is set)
         */
        bool IsChannelGood(uint8_t channel) const {
            if (channel > 7) return false;
            return (channelValidBitmask & (1 << channel)) != 0;
        }
    };

    // strongly-typed enums for safe configuration
    enum class ClockMode : uint8_t {
        Internal = 0,
        External = 1
    };

    enum class ReferenceMode : uint8_t {
        VDD = 0b000,
        External = 0b010,
        Internal_AnalogIn = 0b100,
        Internal_AlwaysOn = 0b101,
        Internal_RefOut = 0b110,
        Internal_RefOut_AlwaysOn = 0b111
    };

    enum class Polarity : uint8_t {
        Unipolar = 0,
        Bipolar = 1
    };

    enum class InputMode : uint8_t {
        Differential = 0,
        SingleEnded = 1
    };

    struct Config {
        uint8_t maxAttempts = 3;  // total tries per operation (1 initial + 2 retries)
        ClockMode clockMode = ClockMode::Internal;
        ReferenceMode refMode = ReferenceMode::VDD;
        Polarity polarity = Polarity::Unipolar;
        InputMode inputMode = InputMode::SingleEnded;
        uint16_t channelSelect = 0xFF;  // bitmask to select channels during initialization, defaults to all channels
    };

    // internal states for the non-blocking state machine
    enum class DriverState {
        IDLE,
        TRANSMITTING,
        RECEIVING,
        DATA_READY,
        ERROR
    };

    /**
     * @param hi2c I2C bus handler
     * @param sclPort serial clock GPIO port
     * @param sclPin serial clock GPIO pin
     * @param sdaPort serial data GPIO port
     * @param sdaPin serial data GPIO pin
     */
    AdcMax11614i2c(I2C_HandleTypeDef* hi2c, GPIO_TypeDef* sclPort, uint16_t sclPin, GPIO_TypeDef* sdaPort, uint16_t sdaPin);

    /**
     * @brief write configs to Setup register
     * @param config the persistent configuration settings for the ADC
     * @retval write status, true for success
     */
    bool Init(const Config& config);

    /**
     * @brief update the hardware channel selection configuration byte
     * @param channelSelect 8-bit bitmask specifying the channels to read from, 0-7
     * @retval write status, true for success
     */
    bool SetChannels(uint16_t channelSelect);

    /**
     * @brief initiate a non-blocking read using the previously stored channel configuration
     * @retval true if the transaction successfully started on the bus, false if busy or init failed
     */
    bool StartReadAsync();

    /**
     * @brief check if the hardware has finished receiving data via interrupt
     * @retval true if data is ready to be fetched
     */
    bool IsDataReady() const;

    /**
     * @brief parse and fetch the data populated by the receive interrupt
     * @retval std::optional containing channel outputs, or std::nullopt if the transaction failed
     */
    std::optional<Data> FetchData();

    /**
     * @brief check if driver initialization failed
     * @retval true if initialization failed
     */
    bool InitFailed() const { return _initFailed; }
    bool HasInitFailed() const { return _initFailed; }

    /**
     * @brief get the latest 8-bit channel status bitmask (1 = good)
     * @retval 8-bit bitmask of channel status
     */
    uint8_t GetchannelValidBitmask() const { return _data.channelValidBitmask; }

    /**
     * @brief get the latest parsed channel data
     * @retval const reference to latest Data
     */
    const Data& GetData() const { return _data; }

    // global c callback routers
    static void HAL_RxCpltCallback(I2C_HandleTypeDef* hi2c);
    static void HAL_ErrorCallback(I2C_HandleTypeDef* hi2c);

  private:
    I2C_HandleTypeDef* _hi2c;
    GPIO_TypeDef* _sclPort;
    uint16_t _sclPin;
    GPIO_TypeDef* _sdaPort;
    uint16_t _sdaPin;

    Config _config;

    // initialization status
    bool _initFailed = false;

    // latest parsed channel outputs and status bitmask
    Data _data{};

    // async state machine variables
    volatile DriverState _state = DriverState::IDLE;
    uint8_t _rxBuffer[16];
    uint8_t _numChannelsToRead;
    uint8_t _startChannel;

    // internal interrupt handlers
    void HandleRxComplete();
    void HandleError();

    // static instance
    static AdcMax11614i2c* _instance;
};