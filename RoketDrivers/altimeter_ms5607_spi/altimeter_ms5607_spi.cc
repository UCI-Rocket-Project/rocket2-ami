#include "altimeter_ms5607_spi.h"

#include <cmath>

AltimeterMs5607Spi::AltimeterMs5607Spi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *misoPort, uint16_t misoPin, double seaLevelPressure, int timeout = 10)
    : _hspi(hspi), _csPort(csPort), _csPin(csPin), _misoPort(misoPort), _misoPin(misoPin), _seaLevelPressure(seaLevelPressure), _timeout(timeout) {}

bool AltimeterMs5607Spi::Reset() {
    bool opStatus = true;

    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    // reset, opcode 0x1E
    if (HAL_SPI_Transmit(_hspi, (uint8_t *)(const uint8_t[]){0x1E}, 1, _timeout) != HAL_OK) opStatus = false;
    // poll for reset complete (approx 2.8 ms)
    uint32_t startTime = HAL_GetTick();
    GPIO_PinState readStatus = GPIO_PIN_RESET;
    while (readStatus == GPIO_PIN_RESET) {
        readStatus = HAL_GPIO_ReadPin(_misoPort, _misoPin);

        // 5 ms timeout
        if (HAL_GetTick() - startTime >= 5) {
            opStatus = false;
            break;
        }
    }
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    return opStatus;
}

AltimeterMs5607Spi::State AltimeterMs5607Spi::Init() {
    _state = State::IDLE;

    // read factory coefficients
    for (int i = 0; i < 8; i++) {
        uint8_t buffer[2];
        HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
        // PROM read, opcode 0b1010xxx0
        uint8_t command[1] = {(uint8_t)(0b10100000 | (i << 1))};
        if (HAL_SPI_Transmit(_hspi, command, 1, _timeout) != HAL_OK) _state = State::ERROR;
        if (HAL_SPI_Receive(_hspi, buffer, 2, _timeout) != HAL_OK) _state = State::ERROR;
        _coefficients[i] = ((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1];
        HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
    }

    // CRC algorithm copied from TE application note AN520
    uint16_t crc = 0xF & _coefficients[7];
    uint16_t crcActual = 0;
    _coefficients[7] = 0xFF00 & _coefficients[7];
    for (int i = 0; i < 16; i++) {
        crcActual ^= i % 2 == 0 ? _coefficients[i >> 1] >> 8 : _coefficients[i >> 1] & 0x00FF;
        for (int j = 0; j < 8; j++) {
            crcActual = crcActual & 0x8000 ? (crcActual << 1) ^ 0x3000 : (crcActual << 1);
        }
    }
    crcActual = crcActual >> 12;
    if (crc != crcActual) _state = State::ERROR;

    return _state;
}

AltimeterMs5607Spi::State AltimeterMs5607Spi::Read(Rate rate) {
    switch (_state) {
        case State::IDLE: {
            _state = State::POLL_D1;

            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            // convert D1 pressure, the value of the rate enum is the opcode of the corresponding conversion
            uint8_t command = (uint8_t)rate;
            if (HAL_SPI_Transmit(_hspi, &command, 1, _timeout) != HAL_OK) _state = State::ERROR;
            break;
        }

        case State::POLL_D1: {
            if (HAL_GPIO_ReadPin(_misoPort, _misoPin) == GPIO_PIN_RESET) break;
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

            _state = State::POLL_D2;

            uint8_t buffer[3];
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            // read data, opcode 0x00
            if (HAL_SPI_Transmit(_hspi, (uint8_t *)(const uint8_t[]){0x00}, 1, _timeout) != HAL_OK) _state = State::ERROR;
            if (HAL_SPI_Receive(_hspi, buffer, 3, _timeout) != HAL_OK) _state = State::ERROR;
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
            _d1 = ((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2]);

            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            // convert D2 temperature, the value of the rate enum is the opcode of the corresponding conversion plus 0x10
            uint8_t command = (uint8_t)rate + 0x10;
            if (HAL_SPI_Transmit(_hspi, &command, 1, _timeout) != HAL_OK) _state = State::ERROR;
            break;
        }

        case State::POLL_D2: {
            if (HAL_GPIO_ReadPin(_misoPort, _misoPin) == GPIO_PIN_RESET) break;
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

            _state = State::COMPLETE;
            uint8_t buffer[3];
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            // read data, opcode 0x00
            if (HAL_SPI_Transmit(_hspi, (uint8_t *)(const uint8_t[]){0x00}, 1, _timeout) != HAL_OK) _state = State::ERROR;
            if (HAL_SPI_Receive(_hspi, buffer, 3, _timeout) != HAL_OK) _state = State::ERROR;
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
            _d2 = ((uint32_t)buffer[0] << 16) | ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[2]);

            // calculate temperature, pressure, and altitude, see datasheet for formulas used
            int64_t dt = _d2 - (int64_t)_coefficients[5] * 256;
            int64_t t = 2000 + dt * (int64_t)_coefficients[6] / 8388608;
            int64_t off = (int64_t)_coefficients[2] * 131072 + ((int64_t)_coefficients[4] * dt) / 64;
            int64_t sens = (int64_t)_coefficients[1] * 65536 + ((int64_t)_coefficients[3] * dt) / 128;

            // low temperature correction
            int64_t t2 = 0;
            int64_t off2 = 0;
            int64_t sens2 = 0;
            if (t < 2000) {
                int64_t a = t - 2000;
                t2 = dt * dt / 2147483648;
                off2 = 61 * a * a / 16;
                sens2 = 2 * a * a;

                if (t < -1500) {
                    int64_t b = t + 1500;
                    off2 += 15 * b * b;
                    sens2 += 8 * b * b;
                }
            }
            t -= t2;
            off -= off2;
            sens -= sens2;

            _data.temperature = t / 100.0;
            _data.pressure = (_d1 * sens / 2097152 - off) / 32768.0;
            _data.altitude = 44307.7 * (1 - std::pow(_data.pressure / 100.0 / _seaLevelPressure, 0.190284));
            break;
        }

        default:
            break;
    }
    return _state;
}

AltimeterMs5607Spi::Data AltimeterMs5607Spi::GetData() {
    _state = State::IDLE;
    return _data;
}
