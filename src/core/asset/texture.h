#ifndef TEXTURE_ASSET_H
#define TEXTURE_ASSET_H

#include "asset.h"
#include <stdint.h>

typedef enum {
    CTEX_FORMAT_RGBA8 = 0,
    CTEX_FORMAT_BC1,
    CTEX_FORMAT_BC3,
    CTEX_FORMAT_BC5,
} CtexFormat;

#define CTEX_MAX_MIPS 16

typedef struct {
    uint8_t     *data;
    uint32_t     width;
    uint32_t     height;
    uint32_t     byteSize;
} TextureMip;

typedef struct {
    uint8_t     *rawData;
    TextureMip   mips[CTEX_MAX_MIPS];
    uint32_t     mipCount;
    uint32_t     width;
    uint32_t     height;
    CtexFormat   format;
} TextureAsset;

#define CTEX_MAGIC   0x58455443u
#define CTEX_VERSION 3u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t mipCount;
} CtexHeader;

AssetResult texture_asset_type_init(AssetManager *mgr);

#endif