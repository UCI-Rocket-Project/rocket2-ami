#pragma once

#include <cstdint>

#include "main.h" // # includes "stm32xxxx_hal.h"

/**
    868 MHz band (EU) → 863–870 MHz
    915 MHz band (US) → 902–928 MHz
    433 MHz band (Asia) → around 433.175 MHz
 */

class RadioSx127xSpi {
  public:
    enum class State {
        NOT_INIT,
        IDLE,
        TX_START,
        TX_IN_PROGRESS,
        TX_COMPLETE,
        TX_TIMEOUT,
        RX_START,
        RX_IN_PROGRESS,
        RX_COMPLETE,
        RX_CRC_ERROR,
        RX_TIMEOUT,
        ERROR
    };

    enum class RfPort {
        RFO = 0,
        PA_BOOST = 1
    };

    enum class RampTime {
        // 3.4 ms
        RT3M4S = 0,
        // 2 ms
        RT2MS = 1,
        // 1 ms
        RT1MS = 2,
        // 500 us
        RT500US = 3,
        // 250 us
        RT250US = 4,
        // 125 us
        RT125US = 5,
        // 100 us
        RT100US = 6,
        // 62 us
        RT62US = 7,
        // 50 us
        RT50US = 8,
        // 40 us
        RT40US = 9,
        // 31 us
        RT31US = 10,
        // 25 us
        RT25US = 11,
        // 20 us
        RT20US = 12,
        // 15 us
        RT15US = 13,
        // 12 us
        RT12US = 14,
        // 10 us
        RT10US = 15
    };

    enum class Bandwidth {
        // 7.8 kHz
        BW7K8HZ = 0,
        // 10.4 kHz
        BW10K4HZ = 1,
        // 15.6 kHz
        BW15K6HZ = 2,
        // 20.8 kHz
        BW20K8HZ = 3,
        // 31.25 kHz
        BW31K25HZ = 4,
        // 41.7 kHz
        BW41K7HZ = 5,
        // 62.5 kHz
        BW62K5HZ = 6,
        // 125 kHz
        BW125KHZ = 7,
        // 250 kHz
        BW250KHZ = 8,
        // 500 kHz
        BW500KHZ = 9
    };

    enum class CodingRate {
        // 4/5
        CR45 = 1,
        // 4/6
        CR46 = 2,
        // 4/7
        CR47 = 3,
        // 4/8
        CR48 = 4
    };

    enum class SpreadingFactor {
        SF6 = 6,
        SF7 = 7,
        SF8 = 8,
        SF9 = 9,
        SF10 = 10,
        SF11 = 11,
        SF12 = 12
    };

    // main state machine variable for radio
    State _state = State::NOT_INIT;

    /**
     * @param hspi SPI bus handler
     * @param csPort chip select GPIO port
     * @param csPin chip select GPIO pin
     * @param rstPort reset GPIO port
     * @param rstPin reset GPIO pin
     * @param syncWord LoRa sync word, sync word must match transmitter and reciever
     * @param rfPort transponder physical RF port
     * @param frequency RF center frequency in Hz, valid range 137000000-525000000, 779000000-1020000000
     * @param transmitPower transmit power in dBm, valid range 0-15 if using RFO, valid range 2-17 if using PA_BOOST
     * @param rampTime PA ramp time, Controls how fast teh RF output power ramps up when starting a transmission
     * @param bandwidth signal bandwidth, wider bandwidth = faster data rate but less sensitive (shorter range)
     * @param codingRate error coding rate, Forward Error Correction (FEC): how much redundancy is added.
     * @param spreadingFactor spreading factor rate. determines how long and how many times that chirp is repeated per symbol.
     * @param preambleLength preamble length in symbols, valid range 6-65535. Used for synchronization before actual data
     * @param crcEnable enable CRC
     * @param txTimeout TX timeout in milliseconds
     * @param rxTimeout RX timeout in number of symbols, valid range 4-1023, timeout in seconds = rxTimeout * (2 ** spreadingFactor) / bandwidth
     * @param serialTimeout serial bus timeout in milliseconds
     */
    RadioSx127xSpi(SPI_HandleTypeDef *hspi, GPIO_TypeDef *csPort, uint16_t csPin, GPIO_TypeDef *rstPort, uint16_t rstPin, uint8_t syncWord, RfPort rfPort, unsigned int frequency,
                   unsigned int transmitPower, RampTime rampTime, Bandwidth bandwidth, CodingRate codingRate, SpreadingFactor spreadingFactor, unsigned int preambleLength, bool crcEnable,
                   unsigned int txTimeout, unsigned int rxTimeout, unsigned int serialTimeout = 10);

    /**
     * @brief Resets radio
     * @retval Operation status, true for success
     */
    bool Reset();

    /**
     * @brief Initializes state machine, configures radio
     * @retval Operation status, true for success
     */
    bool Init();

    /**
     * @brief Transmits payload
     * @param payload pointer to payload buffer
     * @param payloadLength packet length in bytes, valid range 1-255
     * @retval None
     */
    void Transmit(const uint8_t *payload, uint8_t payloadLength);

    /**
     * @brief Listens and receives one packet
     * @param payload pointer to payload buffer
     * @param rssi received signal strength indication, this is an output
     * @param payloadLength packet length in bytes, valid range 1-255
     * @retval None
     */
    void Receive(uint8_t *payload, uint8_t payloadLength, int *rssi);

    /**
     * @brief Runs main logic, updates state machine
     * @retval Current state after operations
     */
    State Update();

  private:
    SPI_HandleTypeDef *_hspi;
    GPIO_TypeDef *_csPort;
    uint16_t _csPin;
    GPIO_TypeDef *_rstPort;
    uint16_t _rstPin;
    uint8_t _syncWord;
    RfPort _rfPort;
    unsigned int _frequency;
    unsigned int _transmitPower;
    RampTime _rampTime;
    Bandwidth _bandwidth;
    CodingRate _codingRate;
    SpreadingFactor _spreadingFactor;
    unsigned int _preambleLength;
    bool _crcEnable;
    unsigned int _txTimeout;
    unsigned int _rxTimeout;
    unsigned int _serialTimeout;

    uint32_t _txStartTime;
    uint8_t *_payload;
    uint8_t _payloadLength;
    int *_rssi;

    /**
     * @param address register address
     * @param data buffer to save data from register
     * @retval Operation status, true for success
     */
    bool ReadRegister(uint8_t address, uint8_t *data);

    /**
     * @param address register address
     * @param data buffer containing data to write to register
     * @retval Operation status, true for success
     */
    bool WriteRegister(uint8_t address, uint8_t data);
};
