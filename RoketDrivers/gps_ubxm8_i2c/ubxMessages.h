#pragma once
#include <stdint.h>

enum class GPSFixType : uint8_t {
    NO_FIX,
    DEAD_RECKONING,
    FIX_2D,
    FIX_3D,
    GNSS_AND_DEAD_RECKONING,
    TIME_ONLY_FIX
};

static uint8_t PVT_MESSAGE[4] = {0x01, 0x07, 0x00, 0x00};

// https://content.u-blox.com/sites/default/files/products/documents/u-blox8-M8_ReceiverDescrProtSpec_UBX-13003221.pdf
#pragma pack(push, 1)
struct UBX_NAV_PVT_PAYLOAD {
    uint32_t iTOW;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
    uint8_t valid;
    uint32_t tAcc;
    int32_t nano;
    GPSFixType fixType;
    uint8_t flags;
    uint8_t flags2;
    uint8_t numSV;
    int32_t lon;
    int32_t lat;
    int32_t height;
    int32_t hMSL;
    uint32_t hAcc;
    uint32_t vAcc;
    int32_t velN;
    int32_t velE;
    int32_t velD;
    int32_t gSpeed;
    int32_t headMot;
    uint32_t sAcc;
    uint32_t headAcc;
    uint16_t pDOP;
    uint8_t reserved1[6];
    int32_t headVeh;
    int16_t magDec;
    uint16_t magAcc;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct UCIRP_GPS_PAYLOAD {  // our custom struct
    GPSFixType gpsFix;
    double ecefX;
    double ecefY;
    double ecefZ;
    double positionAcc;
    double ecefVX;
    double ecefVY;
    double ecefVZ;
    double speedAcc;
};
#pragma pack(pop)
