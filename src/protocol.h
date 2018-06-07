#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>

#define FILENAME_MAX_SIZE 512
#define CHUNK_MAX_SIZE 1024

const int32_t packet_type_online = 0;
const int32_t packet_type_modify = 1;
const int32_t packet_type_delete = 2;

struct PacketHead
{
    int32_t type;
    int32_t time;
    char filename[FILENAME_MAX_SIZE];
};

struct ModifyPacket
{
    int64_t file_size;
    int64_t total_size;
    int64_t payload_offset;
    int64_t payload_size;
};

#endif // _PROTOCOL_H_