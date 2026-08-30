#ifndef PAK_FORMAT_H
#define PAK_FORMAT_H

#include <stdint.h>

#define PAK_MAGIC    0x4B415050u
#define PAK_VERSION  2u
#define PAK_MAX_NAME 56

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t file_count;
    uint32_t index_offset;
} PakHeader;

typedef struct {
    char     name[PAK_MAX_NAME];
    uint32_t offset;
    uint32_t size;
    uint32_t compressed_size;
} PakEntry;

#endif