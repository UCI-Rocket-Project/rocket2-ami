#include "ubxPacket.h"

// make a ubxPacket class that has a method to update one byte at a time
UBXPacketReader::UBXPacketReader() {
    payloadLength = 0;
    packetIndex = 0;
    ckA = 0;
    ckB = 0;
    complete = false;
    inProgress = false;
}

UBXPacketUpdateResult UBXPacketReader::update(uint8_t newByte) {
    if (complete) {
        return UBXPacketUpdateResult::PACKET_ALREADY_COMPLETE;
    }
    inProgress = true;
    int currentPacketIndex = packetIndex;
    packetIndex++;
    if (currentPacketIndex == 0) {
        messageClass = newByte;
    } else if (currentPacketIndex == 1) {
        messageId = newByte;
    } else if (currentPacketIndex == 2) {
        payloadLength = newByte;
    } else if (currentPacketIndex == 3) {
        payloadLength |= (uint16_t)newByte << 8;
    } else if (currentPacketIndex < 4 + payloadLength) {
        payload[currentPacketIndex - 4] = newByte;
    } else if (currentPacketIndex == 4 + payloadLength) {
        if (ckA != newByte) {
            return UBXPacketUpdateResult::CHECKSUM_FAILED;
        }
        return UBXPacketUpdateResult::UPDATE_OK;  // return early to avoid updating checksums
    } else if (currentPacketIndex == 5 + payloadLength) {
        if (ckB != newByte) {
            return UBXPacketUpdateResult::CHECKSUM_FAILED;
        } else {
            complete = true;
            inProgress = false;
        }
        return UBXPacketUpdateResult::UPDATE_OK;  // return early to avoid updating checksums
    } else {
        // should never get here, but return a unique value for debugging just in case
        return UBXPacketUpdateResult::PACKET_INDEX_OVERFLOW;
    }

    ckA = ckA + newByte;
    ckB = ckB + ckA;
    return UBXPacketUpdateResult::UPDATE_OK;
}

void* UBXPacketReader::getPayload() { return (void*)payload; }

uint8_t UBXPacketReader::getPayloadLength() { return payloadLength; }

uint8_t UBXPacketReader::getMessageClass() { return messageClass; }

uint8_t UBXPacketReader::getMessageId() { return messageId; }

bool UBXPacketReader::isComplete() { return complete; }

bool UBXPacketReader::isInProgress() { return inProgress; }

void UBXPacketReader::reset() {
    payloadLength = 0;
    packetIndex = 0;
    ckA = 0;
    ckB = 0;
    complete = false;
    inProgress = false;
}
