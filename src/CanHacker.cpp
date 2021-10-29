#include <stdio.h>
#include <inttypes.h>
#include <cstring>
#include <arpa/inet.h>

#include "CanHacker.h"
#include "common.h"

CanHacker::CanHacker () {}

CanHacker::~CanHacker () {}

int CanHacker::parseTransmit(char (&buffer)[CANET_SIZE], struct can_frame *frame) {
    uint8_t FRAME_INFO = buffer[0];
    bool is_extended_id = FRAME_INFO >> 7 & 1;
    bool is_remote_frame = FRAME_INFO >> 6 & 1;
    frame->can_dlc = FRAME_INFO & 0xf;

    canid_t FRAME_ID;
    memcpy(&FRAME_ID, &buffer[1], 4);
    FRAME_ID = ntohl(FRAME_ID);
    FRAME_ID &= is_extended_id ? 0x1fffffff : 0x000007ff;
    if (is_remote_frame)
        FRAME_ID |= CAN_RTR_FLAG;
    if (is_extended_id)
        FRAME_ID |= CAN_EFF_FLAG;
    frame->can_id = FRAME_ID;

    if (!is_remote_frame) {
        for (int i = 0; i < frame->can_dlc; i++)
            frame->data[i] = buffer[5 + i];
    }

    return 0;
}

int CanHacker::createTransmit(struct can_frame *frame, char (&buffer)[CANET_SIZE]) {
    bool is_extended_id = frame->can_id & CAN_EFF_FLAG;
    bool is_remote_frame = frame->can_id & CAN_RTR_FLAG;
    bool is_error_frame = frame->can_id & CAN_EFF_FLAG;
    uint8_t FRAME_INFO = frame->can_dlc | is_extended_id << 7 | is_remote_frame << 6;
    uint32_t FRAME_ID = frame->can_id;
    if (is_error_frame) {
        FRAME_ID &= CAN_ERR_MASK | CAN_ERR_FLAG;
    } else {
        FRAME_ID &= is_extended_id ? CAN_EFF_MASK :CAN_SFF_MASK;
    }
    FRAME_ID = htonl(FRAME_ID);
    uint64_t DATA{0};
    for (int i = 7; i >= 0; --i)
        DATA |= (uint64_t)frame->data[i] << 8 * i;
    memcpy(buffer, &FRAME_INFO, sizeof(FRAME_INFO));
    memcpy(&buffer[1], &FRAME_ID, sizeof(FRAME_ID));
    memcpy(&buffer[5], &DATA, sizeof(DATA));
    return 0;
}
