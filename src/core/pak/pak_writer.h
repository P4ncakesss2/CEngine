#ifndef PAK_WRITER_H
#define PAK_WRITER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct PakWriter PakWriter;

PakWriter *pak_writer_open(const char *path);
bool pak_writer_add(PakWriter *pw, const char *name, const void *data, size_t size);
bool pak_writer_close(PakWriter *pw);

#endif