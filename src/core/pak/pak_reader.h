#ifndef PAK_READER_H
#define PAK_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PakReader PakReader;

PakReader *pak_reader_open(const char *path);
void       pak_reader_close(PakReader *pr);

bool pak_reader_stat(PakReader *pr, const char *name, size_t *out_size);
void *pak_reader_read_alloc(PakReader *pr, const char *name, size_t *out_size);

uint32_t    pak_reader_count(PakReader *pr);
const char *pak_reader_name_at(PakReader *pr, uint32_t index);

#endif