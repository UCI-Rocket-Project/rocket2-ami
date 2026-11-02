#pragma once

#include <cstdint>

// if this is shitting itself, just locally replace it with whatever
// header on your machine that can get you `I2C_HandleTypeDef`
#include "main.h"

#include "ubxMessages.h"
#include "ubxPacket.h"

/**
 * Class for interfacing over i2c with any M8 GPS.
 * Before this class is used, the GPS reset pin needs to be pulled high.
 *
 * This class keeps its own finite state machine for abstracting away polling data availability and retrying requests.
 * An example usage is as follows:
 * ```
  int main(void)
  {
      HAL_Init();
  
      SystemClock_Config();

      MX_GPIO_Init();

      MX_I2C1_Init();

      GPS gps(GPS_RST_GPIO_Port, GPS_RST_Pin, &hi2c1, PVT_MESSAGE);
      gps.Init();
      HAL_Delay(1000);

      int lastITOW = 0;
      while(1) {
          volatile GPS::PollResult res = gps.PollUpdate();    
          auto state = gps.GetState();
          if(state == GPS::State::RESPONSE_READY) {
              UBX_NAV_PVT_PAYLOAD sol = *(UBX_NAV_PVT_PAYLOAD*)gps.GetSolution();            
              volatile int diff = sol.iTOW - lastITOW;
              lastITOW = sol.iTOW;
              if((GPSFixType)sol.fixType == GPSFixType::FIX_3D) {
                  HAL_Delay(1000);
              }
              gps.Reset();
          } else {
              HAL_Delay(100);
          }
      }
  }
  ```
 *
 * After a packet is received, you _need_ to call the reset method.
 * If you don't, `pollUpdate` will just do nothing and return a `POLL_ALREADY_FINISHED` response.
 *
 * The datasheet is here:
 * https://content.u-blox.com/sites/default/files/products/documents/u-blox8-M8_ReceiverDescrProtSpec_UBX-13003221.pdf
 * The most relevant parts are "UBX Protocol" (section 32) and "DDC Port" (section 15).
 *
 */
class GpsUbxM8I2c {
  public:
    enum class State {
        REQUEST_NOT_SENT,
        POLLING_RESPONSE,
        RESPONSE_READY
    };
    enum class PollResult {
        POLL_JUST_FINISHED,           // the ubx data has been received on this invocation, and is now ready to be retrieved
        POLL_ALREADY_FINISHED,        // the ubx data was already ready after a previous PollUpdate call
        RECEIVE_IN_PROGRESS,          // we found the UBX start bytes in the incoming stream but haven't read the entire message
        NO_DATA,                      // no data from GPS available
        NO_UBX_DATA,                  // data read successfully but no UBX start bytes found
        DATA_LEN_POLL_FAILED,         // there was a failure in the I2C mem read for getting the quantity of data available
        DATA_RECEIVE_I2C_FAILED,      // there was a failure in the I2C receive to read the available data from the GPS
        DATA_RECEIVE_CHECKSUM_FAILED, // we received UBX data but the checksum didn't match the message.
        REQUEST_SEND_FAILED           // the request to the GPS failed (probably i2c transmit error)
    };
    GpsUbxM8I2c(GPIO_TypeDef* gpioResetPort, uint16_t gpioResetPin, I2C_HandleTypeDef* i2c, uint8_t* ubxMessage);
    void Init();
    const State GetState();
    const PollResult PollUpdate();
    const UCIRP_GPS_PAYLOAD  GetSolution();
    void Reset();

  private:
    UBXPacketReader _packetReader;
    State _state;
    GPIO_TypeDef* _gpioResetPort;
    uint16_t _gpioResetPin;
    I2C_HandleTypeDef* _i2c;
    uint8_t* _ubxMessage;
    bool sendUBX(uint8_t* message);
    UCIRP_GPS_PAYLOAD ConvertPayloadToECEF(const UBX_NAV_PVT_PAYLOAD& pvtPayload);
};
