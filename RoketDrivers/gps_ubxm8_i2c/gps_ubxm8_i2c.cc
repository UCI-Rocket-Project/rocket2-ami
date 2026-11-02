#include "gps_ubxm8_i2c.h"

#include <algorithm>
#include <cmath>

#include "coordHelpers.h"
#include "ubxMessages.h"
#include "ubxPacket.h"

#define I2C_BUFFER_SIZE 1024
#define GPS_I2C_TIMEOUT 100

/**
 * @param ubxMessage one of the messages from `ubxMessages.h` to send.
 */
GpsUbxM8I2c::GpsUbxM8I2c(GPIO_TypeDef* gpioResetPort, uint16_t gpioResetPin, I2C_HandleTypeDef* i2c, uint8_t* ubxMessage) {
    _packetReader = UBXPacketReader();
    _state = GpsUbxM8I2c::State::REQUEST_NOT_SENT;
    _gpioResetPin = gpioResetPin;
    _gpioResetPort = gpioResetPort;
    _i2c = i2c;
    _ubxMessage = ubxMessage;
}

void GpsUbxM8I2c::Init() { HAL_GPIO_WritePin(_gpioResetPort, _gpioResetPin, GPIO_PIN_SET); }

const GpsUbxM8I2c::State GpsUbxM8I2c::GetState() { return _state; }

/**
 * @brief Sends a request for position data if none are currently pending, checks data available,
 * and returns a status indicator.
 *
 * @return a GpsUbxM8I2c::PollResult object. See the definition of that enum for details.
 */
const GpsUbxM8I2c::PollResult GpsUbxM8I2c::PollUpdate() {
    if (_state == GpsUbxM8I2c::State::REQUEST_NOT_SENT) {
        if(!sendUBX(_ubxMessage)) {
            return GpsUbxM8I2c::PollResult::REQUEST_SEND_FAILED;
        }
        _state = GpsUbxM8I2c::State::POLLING_RESPONSE;
    }

    if (_state == GpsUbxM8I2c::State::POLLING_RESPONSE) {
        uint8_t lenBytes[2];
        uint16_t dataLen = 0;
        uint8_t buffer[I2C_BUFFER_SIZE];

        HAL_StatusTypeDef dataLenReadStatus = HAL_I2C_Mem_Read(_i2c, 0x42 << 1, 0xFD, 1, lenBytes, 2, GPS_I2C_TIMEOUT);
        if (dataLenReadStatus != HAL_OK) {
            return GpsUbxM8I2c::PollResult::DATA_LEN_POLL_FAILED;
        }

        dataLen = lenBytes[0] << 8 | lenBytes[1];
        if (dataLen == 0) {
            return GpsUbxM8I2c::PollResult::NO_DATA;
        }

        if (dataLen > I2C_BUFFER_SIZE) {
            dataLen = I2C_BUFFER_SIZE;
        }
        HAL_StatusTypeDef rcvStatus = HAL_I2C_Master_Receive(_i2c, 0x42 << 1, buffer, dataLen, GPS_I2C_TIMEOUT);
        if (rcvStatus != HAL_OK) {
            return GpsUbxM8I2c::PollResult::DATA_RECEIVE_I2C_FAILED;
        }

        if (_packetReader.isInProgress()) {
            for (uint16_t i = 0; i < dataLen; i++) {
                UBXPacketUpdateResult res = _packetReader.update(buffer[i]);
                if (res == UBXPacketUpdateResult::CHECKSUM_FAILED) {
                    _packetReader.reset();
                    return GpsUbxM8I2c::PollResult::DATA_RECEIVE_CHECKSUM_FAILED;
                }
                if (_packetReader.isComplete()) {
                    _state = GpsUbxM8I2c::State::RESPONSE_READY;
                    return GpsUbxM8I2c::PollResult::POLL_JUST_FINISHED;
                }
            }
            return GpsUbxM8I2c::PollResult::RECEIVE_IN_PROGRESS;
        } else {
            for (uint16_t i = 0; i < dataLen; i++) {
                if (buffer[i] == 0xB5 && buffer[i + 1] == 0x62) {
                    i += 2;  // skip the header
                    while (i < dataLen && !_packetReader.isComplete()) {
                        UBXPacketUpdateResult res = _packetReader.update(buffer[i]);
                        if (res != UBXPacketUpdateResult::UPDATE_OK) {
                            _packetReader.reset();
                            return GpsUbxM8I2c::PollResult::DATA_RECEIVE_CHECKSUM_FAILED;
                        }
                        i++;
                    }
                }
            }

            if (_packetReader.isComplete()) {
                _state = GpsUbxM8I2c::State::RESPONSE_READY;
                return GpsUbxM8I2c::PollResult::POLL_JUST_FINISHED;
            }

            if (!_packetReader.isInProgress()) {
                return GpsUbxM8I2c::PollResult::NO_UBX_DATA;
            }

            return GpsUbxM8I2c::PollResult::RECEIVE_IN_PROGRESS;
        }
    }
    return GpsUbxM8I2c::PollResult::POLL_ALREADY_FINISHED;
}

/**
 * @brief Returns GPS position solution info. This will only give valid data
 * if it's called after `pollUpdate` returns a POLL_JUST_FINISHED response,
 * which will put the GPS's state in RESPONSE_READY mode. If you call this before that,
 * the data in the struct is undefined.
 *
 * After calling this and using the info returned, getting the next
 * packet requires you to call `reset` and `pollUpdate` again.
 *
 * @retval pointer to payload. Can be casted to a struct in ubxMessages.
 */
const UCIRP_GPS_PAYLOAD GpsUbxM8I2c::GetSolution() { 
    UBX_NAV_PVT_PAYLOAD pvtPayload = *(UBX_NAV_PVT_PAYLOAD*)_packetReader.getPayload();
    return ConvertPayloadToECEF(pvtPayload);
}

/**
 * @brief Puts the GPS state back to its initial value so that `pollUpdate` knows
 * it needs to send a new data request.
 */
void GpsUbxM8I2c::Reset() {
    _state = GpsUbxM8I2c::State::REQUEST_NOT_SENT;
    _packetReader.reset();
}

UCIRP_GPS_PAYLOAD GpsUbxM8I2c::ConvertPayloadToECEF(const UBX_NAV_PVT_PAYLOAD& pvtPayload) {
    UCIRP_GPS_PAYLOAD payload;
    payload.gpsFix = pvtPayload.fixType;
    payload.positionAcc = std::max((double)pvtPayload.hAcc / 1000, (double)pvtPayload.vAcc / 1000);
    payload.speedAcc = (double)pvtPayload.sAcc / 1000;

    double lat = (double)pvtPayload.lat / 1e7;
    double lon = (double)pvtPayload.lon / 1e7;
    double alt = (double)pvtPayload.height / 1000;

    Gps2Ecef(lat, lon, alt, payload.ecefX, payload.ecefY, payload.ecefZ);

    // convert velocity to ECEF about the lat and lon
    double velN = (double)pvtPayload.velN / 1000;
    double velE = (double)pvtPayload.velE / 1000;
    double velD = (double)pvtPayload.velD / 1000;

    Ned2EcefVel(lat, lon, alt, velN, velE, velD, payload.ecefVX, payload.ecefVY, payload.ecefVZ);

    return payload;
}

/**
 * @brief Sends a UBX protocol message over i2c
 *
 * @param message Array containing
 *                {messageClass, messageID, payloadLenHighBits, payloadLenLowBits},
 *                and if the payload length >0, all the payload bytes. Note: you don't need to
 *                include the UBX start sequence in this or the checksums.
 *
 * @param len Length of `message`
 * @param i2c The HAL i2c handle to use for transmission.
 */
bool GpsUbxM8I2c::sendUBX(uint8_t* message) {
    static uint8_t magicBytes[2] = {0xB5, 0x62};
    uint16_t len = 4 + ((uint16_t)message[2] << 8) + message[3];

    uint8_t CK_A{0}, CK_B{0};

    HAL_StatusTypeDef status;
    status = HAL_I2C_Master_Transmit(_i2c, 0x42 << 1, magicBytes, 2, GPS_I2C_TIMEOUT);
    if (status != HAL_OK) {
        return false;
    }

    for (uint16_t i = 0; i < len; i++) {
        CK_A = CK_A + message[i];
        CK_B = CK_B + CK_A;
    }

    uint8_t CK[2] = {CK_A, CK_B};
    status = HAL_I2C_Master_Transmit(_i2c, 0x42 << 1, message, len, GPS_I2C_TIMEOUT);
    if (status != HAL_OK) {
        return false;
    }
    status = HAL_I2C_Master_Transmit(_i2c, 0x42 << 1, CK, 2, GPS_I2C_TIMEOUT);
    if (status != HAL_OK) {
        return false;
    }
    return true;
}
