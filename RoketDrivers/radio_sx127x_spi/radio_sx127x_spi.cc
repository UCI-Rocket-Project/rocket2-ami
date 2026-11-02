#include "radio_sx127x_spi.h"

#include <cstring>

RadioSx127xSpi::RadioSx127xSpi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *rstPort, uint16_t rstPin, uint8_t syncWord, RfPort rfPort, unsigned int frequency,
                               unsigned int transmitPower, RampTime rampTime, Bandwidth bandwidth, CodingRate codingRate, SpreadingFactor spreadingFactor, unsigned int preambleLength, bool crcEnable,
                               unsigned int txTimeout, unsigned int rxTimeout, unsigned int serialTimeout)
    : _hspi(hspi),
      _csPort(csPort),
      _csPin(csPin),
      _rstPort(rstPort),
      _rstPin(rstPin),
      _syncWord(syncWord),
      _rfPort(rfPort),
      _frequency(frequency),
      _transmitPower(transmitPower),
      _rampTime(rampTime),
      _bandwidth(bandwidth),
      _codingRate(codingRate),
      _spreadingFactor(spreadingFactor),
      _preambleLength(preambleLength),
      _crcEnable(crcEnable),
      _txTimeout(txTimeout),
      _rxTimeout(rxTimeout),
      _serialTimeout(serialTimeout) {}

bool RadioSx127xSpi::Reset() {
    HAL_GPIO_WritePin(_rstPort, _rstPin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(_rstPort, _rstPin, GPIO_PIN_SET);

    uint8_t lol;
    volatile uint8_t lol2;
    lol2 = ReadRegister(0x01,&lol);

    return true;
}

bool RadioSx127xSpi::Init() {
    _state = State::IDLE;

    // sleep mode
    if (!WriteRegister(0x01, 0b00000000)) return false;

    // LoRa mode, set frequency mode
    if (!WriteRegister(0x01, 0b10000000)) return false;

    // set carrier frequency
    uint32_t f = (uint64_t)_frequency * 524288ULL / 32000000ULL;
    if (!WriteRegister(0x06, (uint8_t)(f >> 16))) return false;
    if (!WriteRegister(0x07, (uint8_t)(f >> 8))) return false;
    if (!WriteRegister(0x08, (uint8_t)f)) return false;

    // set RF port, transmit power
    if (_rfPort == RfPort::RFO) {
        // RFO path: 0–14 dBm typical
        if (_transmitPower > 14) _transmitPower = 14;
        if (!WriteRegister(0x09, 0b01110000 | (uint8_t)_transmitPower)) return false;
    } 
    else if (_rfPort == RfPort::PA_BOOST) {
        if (_transmitPower <= 17) {
            // Normal PA_BOOST mode: 2–17 dBm
            if (!WriteRegister(0x09, 0b10000000 | (uint8_t)(_transmitPower - 2))) return false;

            // Disable high power mode
            if (!WriteRegister(0x4D, 0x04)) return false;
        } else {
            // High-power (+20 dBm) mode
            // PA_BOOST must be used, and High Power Boost enabled
            if (_transmitPower > 20) _transmitPower = 20;

            // RegPaDac (0x4D): 0x87 enables +20 dBm operation
            if (!WriteRegister(0x4D, 0x87)) return false;

            // RegPaConfig (0x09): max power + output power
            // MaxPower bits = 111 (max), OutputPower = (Power - 17)
            if (!WriteRegister(0x09, 0b10000000 | (uint8_t)(_transmitPower - 17))) return false;
        }
    }

    // set PA ramp time
    if (!WriteRegister(0x0A, (uint8_t)_rampTime)) return false;

    // configure over-current protection
    if (!WriteRegister(0x0B, 0b00111011)) return false;

    // configure LNA
    if (!WriteRegister(0x0C, 0b00100000)) return false;

    // set FIFO TX base address
    if (!WriteRegister(0x0E, 0x00)) return false;

    // set FIFO RX base address
    if (!WriteRegister(0x0F, 0x00)) return false;

    // set bandwidth, set coding rate, use implicit header mode
    uint8_t headerMode = 0x00; // explicit mode
    if (!WriteRegister(0x1D, headerMode | ((uint8_t)_bandwidth << 4) | ((uint8_t)_codingRate << 1))) return false;

    // set spreading factor, use non-continuous mode, set CRC, set RX timeout MSB
    if (!WriteRegister(0x1E, ((uint8_t)_spreadingFactor << 4) | ((uint8_t)_crcEnable << 2) | ((uint8_t)(0x3 & (_rxTimeout >> 8))))) return false;

    // set RX timeout LSB
    if (!WriteRegister(0x1F, (uint8_t)_rxTimeout)) return false;

    // set preamble length
    if (!WriteRegister(0x20, (uint8_t)(_preambleLength >> 8))) return false;
    if (!WriteRegister(0x21, (uint8_t)_preambleLength)) return false;

    if (_spreadingFactor == SpreadingFactor::SF6) {
        // LoRa detection optimize
        if (!WriteRegister(0x31, 0x05)) return false;

        // LoRa detection threshold
        if (!WriteRegister(0x37, 0x0C)) return false;
    } else {
        // LoRa detection optimize
        if (!WriteRegister(0x31, 0x03)) return false;

        // LoRa detection threshold
        if (!WriteRegister(0x37, 0x0A)) return false;
    }

    // set sync word
    if (!WriteRegister(0x39, (uint8_t)_syncWord)) return false;

    // standby mode
    if (!WriteRegister(0x01, 0b10000001)) return false;

    return true;
}

void RadioSx127xSpi::Transmit(const uint8_t *payload, uint8_t payloadLength) {
    _state = State::TX_START;
    _payload = (uint8_t *)payload;
    _payloadLength = payloadLength;
}

void RadioSx127xSpi::Receive(uint8_t *payload, uint8_t payloadLength, int *rssi) {
    _state = State::RX_START;
    _payload = (uint8_t *)payload;
    _payloadLength = payloadLength;
    _rssi = rssi;
}

RadioSx127xSpi::State RadioSx127xSpi::Update() {
    switch (_state) {
        case State::TX_START: {
            _state = State::TX_IN_PROGRESS;

            // standby mode
            if (!WriteRegister(0x01, 0b10000001)) _state = State::ERROR;
            uint8_t lol;
            volatile uint8_t lol2;
            lol2 = ReadRegister(0x01,&lol);

            // clear all IRQ
            if (!WriteRegister(0x12, 0xFF)) _state = State::ERROR;
            lol = 0;
            lol2 = ReadRegister(0x12,&lol);

            // set payload length
            if (!WriteRegister(0x22, _payloadLength)) _state = State::ERROR;
            lol = 0;
            lol2 = ReadRegister(0x22,&lol);
            
            // set FIFO address pointer
            if (!WriteRegister(0x0D, 0x00)) _state = State::ERROR;

            // write payload to FIFO
            uint8_t payload[_payloadLength + 1];
            payload[0] = 0x80;
            std::memcpy(payload + 1, _payload, _payloadLength);
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
            if (HAL_SPI_Transmit(_hspi, payload, _payloadLength + 1, _serialTimeout) != HAL_OK) _state = State::ERROR;
            HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

            // start TX
            if (!WriteRegister(0x01, 0b10000011)) _state = State::ERROR;
            HAL_Delay(10);
            ReadRegister(0x01,&lol);

            _txStartTime = HAL_GetTick();

            break;
        }

        case State::TX_IN_PROGRESS: {
            uint8_t irqFlags = 0x00;
            // read IRQs
            if (!ReadRegister(0x12, &irqFlags)) _state = State::ERROR;

            if ((irqFlags & 0x08) != 0) {
                _state = State::TX_COMPLETE;
                // clear TxDone IRQ
                if (!WriteRegister(0x12, 0x08)) _state = State::ERROR;
            } else if (HAL_GetTick() - _txStartTime > _txTimeout) {
                _state = State::TX_TIMEOUT;
            }

            break;
        }

        case State::RX_START: {
            _state = State::RX_IN_PROGRESS;

            // standby mode
            if (!WriteRegister(0x01, 0b10000001)) _state = State::ERROR;

            // clear all IRQ
            if (!WriteRegister(0x12, 0xFF)) _state = State::ERROR;

            // set payload length
            if (!WriteRegister(0x22, _payloadLength)) _state = State::ERROR;

            // start RX single
            if (!WriteRegister(0x01, 0b10000110)) _state = State::ERROR;

            break;
        }

        case State::RX_IN_PROGRESS: {
            uint8_t irqFlags = 0x00;
            // read IRQs
            if (!ReadRegister(0x12, &irqFlags)) _state = State::ERROR;

            if ((irqFlags & 0x40) != 0) {
                _state = State::RX_COMPLETE;

                // standby mode
                if (!WriteRegister(0x01, 0b10000001)) _state = State::ERROR;

                // clear RxDone IRQ
                if (!WriteRegister(0x12, 0x40)) _state = State::ERROR;

                // check valid CRC
                if ((irqFlags & 0x20) != 0) {
                    _state = State::RX_CRC_ERROR;

                    // clear CRC Error IRQ
                    if (!WriteRegister(0x12, 0x20)) _state = State::ERROR;

                    break;
                }

                // set FIFO address pointer
                if (!WriteRegister(0x0D, 0x00)) _state = State::ERROR;

                // read payload from FIFO
                uint8_t payload[_payloadLength + 1];
                payload[0] = 0x00;
                uint8_t buffer[_payloadLength + 1];
                HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
                if (HAL_SPI_TransmitReceive(_hspi, payload, buffer, _payloadLength + 1, _serialTimeout) != HAL_OK) _state = State::ERROR;
                HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
                std::memcpy(_payload, buffer + 1, _payloadLength);

                // read RSSI
                uint8_t data;
                if (!ReadRegister(0x1A, &data)) _state = State::ERROR;
                *_rssi = data - 137;
            } else if (irqFlags & 0x80) {
                _state = State::RX_TIMEOUT;

                // clear RxTimeout IRQ
                if (!WriteRegister(0x12, 0x80)) _state = State::ERROR;
            }
            break;
        }

        default:
            break;
    }

    return _state;
}

bool RadioSx127xSpi::ReadRegister(uint8_t address, uint8_t *data) {
    bool opStatus = true;
    uint8_t payload[2] = {(uint8_t)address, 0x00};
    uint8_t buffer[2] = {0};
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive(_hspi, payload, buffer, 2, _serialTimeout) != HAL_OK) opStatus = false;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);
    *data = buffer[1];
    return opStatus;
}

bool RadioSx127xSpi::WriteRegister(uint8_t address, uint8_t data) {
    bool opStatus = true;
    uint8_t payload[2] = {(uint8_t)(address | 0x80), data};
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_RESET);
    if (HAL_SPI_Transmit(_hspi, payload, 2, _serialTimeout) != HAL_OK) opStatus = false;
    HAL_GPIO_WritePin(_csPort, _csPin, GPIO_PIN_SET);

    return opStatus;
}
