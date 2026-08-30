#include "texture.h"
#include <stdlib.h>
#include <string.h>

static uint32_t block_bytes_for_format(CtexFormat fmt) {
    switch (fmt) {
        case CTEX_FORMAT_BC1: return 8;
        case CTEX_FORMAT_BC3: return 16;
        case CTEX_FORMAT_BC5: return 16;
        default:              return 0;
    }
}

static size_t expected_level_size(CtexFormat fmt, uint32_t w, uint32_t h) {
    if (fmt == CTEX_FORMAT_RGBA8) {
        return (size_t)w * h * 4;
    }
    uint32_t blockBytes = block_bytes_for_format(fmt);
    uint32_t bw = (w + 3) / 4;
    uint32_t bh = (h + 3) / 4;
    return (size_t)bw * bh * blockBytes;
}

static void *texture_load(const char *vpath, const void *data, size_t size) {
    (void)vpath;
    if (!data || size < sizeof(CtexHeader)) return NULL;

    const CtexHeader *header = (const CtexHeader *)data;
    if (header->magic != CTEX_MAGIC || header->version != CTEX_VERSION) return NULL;
    if (header->width == 0 || header->height == 0) return NULL;
    if (header->mipCount == 0 || header->mipCount > CTEX_MAX_MIPS) return NULL;
    if (header->format > CTEX_FORMAT_BC5) return NULL;

    size_t sizeTableBytes = (size_t)header->mipCount * sizeof(uint32_t);
    if (size < sizeof(CtexHeader) + sizeTableBytes) return NULL;

    const uint32_t *sizeTable = (const uint32_t *)((const uint8_t *)data + sizeof(CtexHeader));
    const uint8_t  *payload   = (const uint8_t *)sizeTable + sizeTableBytes;

    uint32_t w = header->width, h = header->height;
    size_t payloadTotal = 0;
    for (uint32_t i = 0; i < header->mipCount; i++) {
        size_t expected = expected_level_size((CtexFormat)header->format, w, h);
        if (sizeTable[i] != expected) return NULL;
        payloadTotal += sizeTable[i];
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    if (size != sizeof(CtexHeader) + sizeTableBytes + payloadTotal) return NULL;

    TextureAsset *tex = calloc(1, sizeof(TextureAsset));
    if (!tex) return NULL;

    tex->rawData = malloc(payloadTotal);
    if (!tex->rawData) { free(tex); return NULL; }
    memcpy(tex->rawData, payload, payloadTotal);

    tex->width    = header->width;
    tex->height   = header->height;
    tex->format   = (CtexFormat)header->format;
    tex->mipCount = header->mipCount;

    w = header->width;
    h = header->height;
    size_t offset = 0;
    for (uint32_t i = 0; i < header->mipCount; i++) {
        tex->mips[i].data     = tex->rawData + offset;
        tex->mips[i].width    = w;
        tex->mips[i].height   = h;
        tex->mips[i].byteSize = sizeTable[i];
        offset += sizeTable[i];
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    return tex;
}

static void texture_free(void *asset_data) {
    TextureAsset *tex = asset_data;
    if (!tex) return;
    free(tex->rawData);
    free(tex);
}

AssetResult texture_asset_type_init(AssetManager *mgr) {
    return asset_type_register(mgr, ASSET_TYPE_Texture, texture_load, texture_free);
}