// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define NOISE_FLAG 0xFF
#define DATA_FLAG 0x01
#define IPv4_FLAG 0x0800

// Custom frame header
struct FrameHeader {
    uint8_t dest_id[6];                   // destination identifier (MAC-style)
    uint8_t source_id[6];                 // source identifier (MAC-style)
    uint16_t ether_type = IPv4_FLAG;      // type of the next layer, like 0x0800 for IPv4
    uint8_t payload_type = DATA_FLAG;     // type of payload, like 0x01 for data or 0XFF for noise
    uint32_t length;                      // length of payload
};

// Function to create a noise frame
inline void create_noise_frame(FrameHeader& frameHeader) {
    memset(frameHeader.dest_id, 0, sizeof(frameHeader.dest_id));
    memset(frameHeader.source_id, 0, sizeof(frameHeader.source_id));
    frameHeader.payload_type = 0xFF; // Noise type
    frameHeader.length = 0;
    memset(frameHeader.payload, 0, sizeof(frameHeader.payload));
}

// Function to check if a frame is a noise frame
inline bool is_noise_frame(const FrameHeader& frameHeader) {
    return frameHeader.payload_type == 0xFF;
}

#endif