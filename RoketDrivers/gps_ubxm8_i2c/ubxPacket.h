#pragma once

#include <stdint.h>

#define GPS_PACKET_READER_PAYLOAD_SIZE 512

enum class UBXPacketUpdateResult {
    UPDATE_OK,
    CHECKSUM_FAILED,
    PACKET_ALREADY_COMPLETE,
    PACKET_INDEX_OVERFLOW  // indicates internal error with packet reader
};

/**
 * reads a ubx payload byte-by-byte into an internal buffer. The size of this is configurable with
 * the macro GPS_PACKET_READER_PAYLOAD_SIZE
 */
class UBXPacketReader {
  public:
    UBXPacketReader();
    UBXPacketUpdateResult update(uint8_t byte);
    bool isComplete();
    bool isInProgress();
    void* getPayload();
    uint8_t getPayloadLength();
    uint8_t getMessageClass();
    uint8_t getMessageId();
    void reset();

  private:
    uint8_t payload[GPS_PACKET_READER_PAYLOAD_SIZE];
    uint8_t payloadLength;
    uint8_t packetIndex;
    uint8_t messageClass;
    uint8_t messageId;
    uint8_t ckA;
    uint8_t ckB;
    bool complete;
    bool inProgress;
};
