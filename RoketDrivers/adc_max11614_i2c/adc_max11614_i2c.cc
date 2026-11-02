#include "adc_max11614_i2c.h"

#include <algorithm>
#include <cstdint>

// initialize the static instance pointer
AdcMax11614i2c* AdcMax11614i2c::_instance = nullptr;

AdcMax11614i2c::AdcMax11614i2c(I2C_HandleTypeDef* hi2c, GPIO_TypeDef* sclPort, uint16_t sclPin, GPIO_TypeDef* sdaPort, uint16_t sdaPin)
    : _hi2c(hi2c), _sclPort(sclPort), _sclPin(sclPin), _sdaPort(sdaPort), _sdaPin(sdaPin) {
    // register this instance for the HAL callbacks
    _instance = this;
}

bool AdcMax11614i2c::Init(const Config& config) {
    _config = config;

    // build setup byte
    uint8_t setupByte = 0b10000000;  // REG bit = 1 for setup byte
    setupByte |= (static_cast<uint8_t>(_config.refMode) << 4);
    setupByte |= (static_cast<uint8_t>(_config.clockMode) << 3);
    setupByte |= (static_cast<uint8_t>(_config.polarity) << 2);
    // bit 1 (RST) = 0 to reset config register, Bit 0 (X) = 0

    // try to write setup byte up to maxAttempts times
    uint8_t attempts = std::max<uint8_t>(1, _config.maxAttempts);
    bool setupSuccess = false;
    for (uint8_t i = 0; i < attempts; ++i) {
        if (HAL_I2C_Master_Transmit(_hi2c, ADC_MAX_I2C_ADDRESS << 1, &setupByte, 1, ADC_MAX_I2C_TIMEOUT_MS) == HAL_OK) {
            setupSuccess = true;
            break;
        }

        HAL_Delay(ADC_MAX_I2C_RETRY_DELAY_MS);
    }

    if (!setupSuccess) {
        _initFailed = true;
        _data = Data{};
        return false;
    }

    // write configuration byte with initial channel select
    if (!SetChannels(_config.channelSelect)) {
        _initFailed = true;
        _data = Data{};
        return false;
    }

    _initFailed = false;
    return true;
}

bool AdcMax11614i2c::SetChannels(uint16_t channelSelect) {
    // if no channels selected, return early
    if (channelSelect == 0) return false;

    _config.channelSelect = channelSelect;

    uint8_t minChannel = 8;
    uint8_t maxChannel = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        if (channelSelect & (1 << i)) {  // channel i is selected
            maxChannel = i;
            if (minChannel == 8) minChannel = i;
        }
    }

    uint8_t scanMode;

    // if only scanning one channel, use single channel scanning
    if (maxChannel == minChannel) {
        scanMode = 0b11;  // SCAN[1:0] = 11 (single channel selected)
        _startChannel = minChannel;
    } else if (minChannel >= 6) {  // else if the lowest requested channel is 6 or 7, use Upper Quartile scanning
        scanMode = 0b10;           // SCAN[1:0] = 10 (AIN6 to maxChannel)
        _startChannel = 6;
    } else {
        scanMode = 0b00;  // SCAN[1:0] =  00 (AIN0 to maxChannel)
        _startChannel = 0;
    }

    _numChannelsToRead = (maxChannel - _startChannel) + 1;

    // write configuration byte
    uint8_t configByteTxBuffer = 0b00000000;
    configByteTxBuffer |= (0 << 7);                                 // REG bit = 0 for configuration byte
    configByteTxBuffer |= (scanMode << 5);                          // SCAN[1:0] selects scanning config
    configByteTxBuffer |= (maxChannel << 1);                        // CS[3:0] selects upper channel
    configByteTxBuffer |= static_cast<uint8_t>(_config.inputMode);  // SGL/DIF based on persistent config

    // Transmit configuration byte (blocking, as this is typically run during init or state changes)
    uint8_t attempts = std::max<uint8_t>(1, _config.maxAttempts);
    for (uint8_t i = 0; i < attempts; ++i) {
        if (HAL_I2C_Master_Transmit(_hi2c, ADC_MAX_I2C_ADDRESS << 1, &configByteTxBuffer, 1, ADC_MAX_I2C_TIMEOUT_MS) == HAL_OK) {
            return true;
        }

        HAL_Delay(ADC_MAX_I2C_RETRY_DELAY_MS);
    }

    return false;
}

bool AdcMax11614i2c::StartReadAsync() {
    // return false early if initialization failed earlier
    if (_initFailed) {
        return false;
    }

    // check if hardware is currently busy
    if (_state != DriverState::IDLE && _state != DriverState::ERROR) {
        return false;
    }

    // if no channels selected, return early
    if (_config.channelSelect == 0) return false;

    // initialize to 0x00 so corrupted headers can be caught
    for (int i = 0; i < 16; i++) {
        _rxBuffer[i] = 0;
    }

    // lock state machine
    _state = DriverState::TRANSMITTING;

    // ADC executes the conversion automatically when addressed for a READ.
    // Receive_IT will send START + ADDR + READ, wait for clock stretching, read data, and send NACK + STOP.
    // this uses the ADC's single read cycle; we would have to continuously send ACK to read continuous results
    uint8_t attempts = std::max<uint8_t>(1, _config.maxAttempts);
    for (uint8_t i = 0; i < attempts; ++i) {
        if (HAL_I2C_Master_Receive_IT(_hi2c, ADC_MAX_I2C_ADDRESS << 1, _rxBuffer, 2 * _numChannelsToRead) == HAL_OK) {
            return true;
        }

        HAL_Delay(ADC_MAX_I2C_RETRY_DELAY_MS);
    }

    _state = DriverState::ERROR;
    return false;
}

void AdcMax11614i2c::HandleRxComplete() {
    // mark state as complete so superloop knows it can pull data
    _state = DriverState::DATA_READY;
}

void AdcMax11614i2c::HandleError() { _state = DriverState::ERROR; }

bool AdcMax11614i2c::IsDataReady() const { return _state == DriverState::DATA_READY; }

std::optional<AdcMax11614i2c::Data> AdcMax11614i2c::FetchData() {
    // if called before transaction finished, return nullopt
    if (_state != DriverState::DATA_READY) return std::nullopt;

    Data data;

    // parse data into struct
    for (uint8_t i = 0; i < _numChannelsToRead; ++i) {
        uint8_t currentChannel = _startChannel + i;
        uint8_t byte1 = _rxBuffer[i * 2];
        uint8_t byte2 = _rxBuffer[(i * 2) + 1];
        uint16_t rawValue;

        // check for the '1111' header in the upper 4 bits
        if ((byte1 & 0xF0) == 0xF0) {
            // the header is intact, mask it out and combine the remaining 12 bits
            rawValue = ((byte1 & 0x0F) << 8) | byte2;
            data.channelValidBitmask |= (1 << currentChannel);
        } else {
            // the data is corrupted, empty, or failed to transmit
            rawValue = 0;
        }

        switch (currentChannel) {
            case 0:
                data.channelOutput0 = rawValue;
                break;
            case 1:
                data.channelOutput1 = rawValue;
                break;
            case 2:
                data.channelOutput2 = rawValue;
                break;
            case 3:
                data.channelOutput3 = rawValue;
                break;
            case 4:
                data.channelOutput4 = rawValue;
                break;
            case 5:
                data.channelOutput5 = rawValue;
                break;
            case 6:
                data.channelOutput6 = rawValue;
                break;
            case 7:
                data.channelOutput7 = rawValue;
                break;
            default:
                break;
        }
    }

    // store latest read data in the class
    _data = data;

    // reset state machine for next read
    _state = DriverState::IDLE;

    return data;
}

// global c callback routers

void AdcMax11614i2c::HAL_RxCpltCallback(I2C_HandleTypeDef* hi2c) {
    if (_instance && _instance->_hi2c == hi2c) _instance->HandleRxComplete();
}

void AdcMax11614i2c::HAL_ErrorCallback(I2C_HandleTypeDef* hi2c) {
    if (_instance && _instance->_hi2c == hi2c) _instance->HandleError();
}