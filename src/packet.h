#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define PACKET_SYNC_WORD 0xABCD
#define OPUS_FRAME_BYTES 40
#define FRAMES_PER_PACKET 2

typedef struct __attribute__((packed))
{
    uint16_t sync_word;
    uint16_t packet_id;
    uint32_t timestamp;
    uint8_t payload[OPUS_FRAME_BYTES * FRAMES_PER_PACKET];
    uint16_t crc16;
} lowkie_packet_t;
#endif // PACKET_H
