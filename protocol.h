// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <string.h>

#define MAX_FRAME_SIZE 1500
#define HEADER_SIZE sizeof(FrameHeader)
#define MAX_PAYLOAD_SIZE (MAX_FRAME_SIZE - HEADER_SIZE)
#define NOISE_FLAG 0xFFFFFFFF

// Custom frame header
struct FrameHeader {
    uint32_t sender_id;      // unique per server (e.g. hash of IP:port)
    uint32_t seq_number;     // sequence number of the frame
    uint32_t length;         // length of payload in bytes
};

// Frame structure
struct Frame {
    FrameHeader header;
    char payload[MAX_PAYLOAD_SIZE];
};

/**
 * @brief Creates a noise frame by initializing the given frame with noise-specific values.
 * 
 * This function sets the sender ID of the frame's header to the noise flag, 
 * sequence number to 0, length to 0, and fills the payload with zeros.
 * 
 * @param frame Reference to the Frame object to be initialized as a noise frame.
 */
inline void create_noise_frame(Frame& frame) {
    frame.header.sender_id = NOISE_FLAG;
    frame.header.seq_number = 0;
    frame.header.length = 0;
    memset(frame.payload, 0, MAX_PAYLOAD_SIZE);
}

inline bool is_noise_frame(const Frame& frame) {
    return frame.header.sender_id == NOISE_FLAG;
}

#endif