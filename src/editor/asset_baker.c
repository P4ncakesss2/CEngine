#include "asset_baker.h"

#include "asset/texture.h"
#include "asset/mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#define FAST_OBJ_IMPLEMENTATION
#include "external/fast_obj.h"

#define STB_DXT_IMPLEMENTATION
#include "external/stb_dxt.h"

static const char *ext_of(const char *filename) {
    const char *dot = strrchr(filename, '.');
    return (dot && dot != filename) ? dot + 1 : "";
}

static bool ext_ieq(const char *ext, const char *candidate) {
#ifdef _WIN32
    return _stricmp(ext, candidate) == 0;
#else
    return strcasecmp(ext, candidate) == 0;
#endif
}

AssetBakeKind asset_bake_classify(const char *filename, const char **out_new_ext) {
    const char *ext = ext_of(filename);

    static const char *texExts[] = { "png", "jpg", "jpeg", "tga", "bmp", "psd", "gif", NULL };
    for (int i = 0; texExts[i]; i++) {
        if (ext_ieq(ext, texExts[i])) {
            if (out_new_ext) *out_new_ext = "ctex";
            return BAKE_KIND_TEXTURE;
        }
    }

    if (ext_ieq(ext, "obj")) {
        if (out_new_ext) *out_new_ext = "cmsh";
        return BAKE_KIND_MESH_OBJ;
    }

    if (out_new_ext) *out_new_ext = NULL;
    return BAKE_KIND_PASSTHROUGH;
}

#define CTEX_MIN_BC_DIM 16

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static bool texture_has_alpha(const stbi_uc *pixels, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        if (pixels[i * 4 + 3] != 255) return true;
    }
    return false;
}

static void box_downsample_rgba(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int sy0 = imin(y * 2, sh - 1);
        int sy1 = imin(y * 2 + 1, sh - 1);
        for (int x = 0; x < dw; x++) {
            int sx0 = imin(x * 2, sw - 1);
            int sx1 = imin(x * 2 + 1, sw - 1);
            for (int c = 0; c < 4; c++) {
                int sum = src[(sy0 * sw + sx0) * 4 + c] + src[(sy0 * sw + sx1) * 4 + c]
                        + src[(sy1 * sw + sx0) * 4 + c] + src[(sy1 * sw + sx1) * 4 + c];
                dst[(y * dw + x) * 4 + c] = (uint8_t)(sum / 4);
            }
        }
    }
}

static int block_bytes_for_format_local(CtexFormat fmt) {
    return fmt == CTEX_FORMAT_BC1 ? 8 : 16;
}

static uint8_t *compress_level_bc(const uint8_t *rgba, int w, int h, CtexFormat fmt, size_t *out_size) {
    int blockBytes = block_bytes_for_format_local(fmt);
    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    size_t total = (size_t)bw * bh * blockBytes;

    uint8_t *out = malloc(total);
    if (!out) return NULL;

    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            uint8_t block[4 * 4 * 4];
            for (int y = 0; y < 4; y++) {
                int sy = imin(by * 4 + y, h - 1);
                for (int x = 0; x < 4; x++) {
                    int sx = imin(bx * 4 + x, w - 1);
                    memcpy(&block[(y * 4 + x) * 4], &rgba[(sy * w + sx) * 4], 4);
                }
            }
            uint8_t *dst = out + (size_t)(by * bw + bx) * blockBytes;
            stb_compress_dxt_block(dst, block, fmt == CTEX_FORMAT_BC3, STB_DXT_HIGHQUAL);
        }
    }

    *out_size = total;
    return out;
}

static uint8_t *compress_level_bc5(const uint8_t *rgba, int w, int h, int chanA, int chanB, size_t *out_size) {
    int blockBytes = 16;
    int bw = (w + 3) / 4;
    int bh = (h + 3) / 4;
    size_t total = (size_t)bw * bh * blockBytes;

    uint8_t *out = malloc(total);
    if (!out) return NULL;

    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            uint8_t block[4 * 4 * 2];
            for (int y = 0; y < 4; y++) {
                int sy = imin(by * 4 + y, h - 1);
                for (int x = 0; x < 4; x++) {
                    int sx = imin(bx * 4 + x, w - 1);
                    const uint8_t *src = &rgba[(sy * w + sx) * 4];
                    block[(y * 4 + x) * 2 + 0] = src[chanA];
                    block[(y * 4 + x) * 2 + 1] = src[chanB];
                }
            }
            uint8_t *dst = out + (size_t)(by * bw + bx) * blockBytes;
            stb_compress_bc5_block(dst, block);
        }
    }

    *out_size = total;
    return out;
}

bool bake_texture_from_file(const char *path, void **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) return false;

    int w, h, channels;
    stbi_uc *pixels = stbi_load(path, &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        fprintf(stderr, "  stbi_load failed for '%s': %s\n", path, stbi_failure_reason());
        return false;
    }

    bool sizeAllowsCompression = (w >= CTEX_MIN_BC_DIM && h >= CTEX_MIN_BC_DIM);

    CtexFormat format;
    int bc5ChanA = 0, bc5ChanB = 1;

    if (!sizeAllowsCompression) {
        format = CTEX_FORMAT_RGBA8;
    } else if (channels <= 1) {
        format = CTEX_FORMAT_BC1;
    } else if (channels == 2) {
        format = CTEX_FORMAT_BC5;
        bc5ChanA = 0; /* R (grey) */
        bc5ChanB = 3; /* A (real alpha) */
    } else if (channels == 3) {
        format = CTEX_FORMAT_BC1;
    } else {
        format = texture_has_alpha(pixels, w, h) ? CTEX_FORMAT_BC3 : CTEX_FORMAT_BC1;
    }

    uint8_t *levelRgba[CTEX_MAX_MIPS];
    int      levelW[CTEX_MAX_MIPS];
    int      levelH[CTEX_MAX_MIPS];
    uint32_t mipCount = 0;

    levelRgba[0] = pixels;
    levelW[0] = w;
    levelH[0] = h;
    mipCount = 1;

    while ((levelW[mipCount - 1] > 1 || levelH[mipCount - 1] > 1) && mipCount < CTEX_MAX_MIPS) {
        int pw = levelW[mipCount - 1];
        int ph = levelH[mipCount - 1];
        int nw = imax(pw / 2, 1);
        int nh = imax(ph / 2, 1);

        uint8_t *down = malloc((size_t)nw * nh * 4);
        if (!down) break;
        box_downsample_rgba(levelRgba[mipCount - 1], pw, ph, down, nw, nh);

        levelRgba[mipCount] = down;
        levelW[mipCount] = nw;
        levelH[mipCount] = nh;
        mipCount++;
    }

    uint8_t *levelOut[CTEX_MAX_MIPS] = {0};
    size_t   levelOutSize[CTEX_MAX_MIPS] = {0};
    bool ok = true;

    for (uint32_t i = 0; i < mipCount && ok; i++) {
        if (format == CTEX_FORMAT_RGBA8) {
            size_t bytes = (size_t)levelW[i] * levelH[i] * 4;
            levelOut[i] = malloc(bytes);
            if (!levelOut[i]) { ok = false; break; }
            memcpy(levelOut[i], levelRgba[i], bytes);
            levelOutSize[i] = bytes;
        } else if (format == CTEX_FORMAT_BC5) {
            levelOut[i] = compress_level_bc5(levelRgba[i], levelW[i], levelH[i], bc5ChanA, bc5ChanB, &levelOutSize[i]);
            if (!levelOut[i]) ok = false;
        } else {
            levelOut[i] = compress_level_bc(levelRgba[i], levelW[i], levelH[i], format, &levelOutSize[i]);
            if (!levelOut[i]) ok = false;
        }
    }

    for (uint32_t i = 1; i < mipCount; i++) free(levelRgba[i]);
    stbi_image_free(pixels);

    if (!ok) {
        for (uint32_t i = 0; i < mipCount; i++) free(levelOut[i]);
        return false;
    }

    size_t sizeTableBytes = (size_t)mipCount * sizeof(uint32_t);
    size_t payloadBytes = 0;
    for (uint32_t i = 0; i < mipCount; i++) payloadBytes += levelOutSize[i];

    size_t total = sizeof(CtexHeader) + sizeTableBytes + payloadBytes;
    uint8_t *buf = malloc(total);
    if (!buf) {
        for (uint32_t i = 0; i < mipCount; i++) free(levelOut[i]);
        return false;
    }

    CtexHeader header = {
        .magic = CTEX_MAGIC,
        .version = CTEX_VERSION,
        .width = (uint32_t)w,
        .height = (uint32_t)h,
        .format = (uint32_t)format,
        .mipCount = mipCount,
    };

    uint8_t *cursor = buf;
    memcpy(cursor, &header, sizeof(header));
    cursor += sizeof(header);

    for (uint32_t i = 0; i < mipCount; i++) {
        uint32_t sz = (uint32_t)levelOutSize[i];
        memcpy(cursor, &sz, sizeof(sz));
        cursor += sizeof(sz);
    }

    for (uint32_t i = 0; i < mipCount; i++) {
        memcpy(cursor, levelOut[i], levelOutSize[i]);
        cursor += levelOutSize[i];
        free(levelOut[i]);
    }

    *out_data = buf;
    *out_size = total;
    return true;
}

static bool pack_cmsh(CmshVertex *vertices, uint32_t vertex_count,
                       uint32_t *indices, uint32_t index_count,
                       void **out_data, size_t *out_size)
{
    size_t vtx_bytes = (size_t)vertex_count * sizeof(CmshVertex);
    size_t idx_bytes = (size_t)index_count * sizeof(uint32_t);
    size_t total = sizeof(CmshHeader) + vtx_bytes + idx_bytes;

    uint8_t *buf = malloc(total);
    if (!buf) return false;

    CmshHeader header = {
        .magic = CMSH_MAGIC,
        .version = CMSH_VERSION,
        .vertex_count = vertex_count,
        .index_count = index_count,
    };

    uint8_t *cursor = buf;
    memcpy(cursor, &header, sizeof(header));   cursor += sizeof(header);
    memcpy(cursor, vertices, vtx_bytes);       cursor += vtx_bytes;
    memcpy(cursor, indices, idx_bytes);

    *out_data = buf;
    *out_size = total;
    return true;
}

bool bake_mesh_from_obj_file(const char *path, void **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) return false;

    fastObjMesh *obj = fast_obj_read(path);
    if (!obj) {
        fprintf(stderr, "  fast_obj_read failed for '%s'\n", path);
        return false;
    }

    uint32_t total_indices = 0;
    for (unsigned int i = 0; i < obj->face_count; i++) {
        unsigned int face_verts = obj->face_vertices[i];
        if (face_verts >= 3) total_indices += (face_verts - 2) * 3;
    }

    if (total_indices == 0) {
        fast_obj_destroy(obj);
        return false;
    }

    CmshVertex *vertices = malloc(total_indices * sizeof(CmshVertex));
    uint32_t   *indices  = malloc(total_indices * sizeof(uint32_t));
    if (!vertices || !indices) {
        free(vertices); free(indices);
        fast_obj_destroy(obj);
        return false;
    }

    uint32_t v_idx = 0;
    uint32_t offset = 0;

    for (unsigned int f = 0; f < obj->face_count; f++) {
        unsigned int face_verts = obj->face_vertices[f];

        for (unsigned int v = 0; v < face_verts - 2; v++) {
            fastObjIndex tri[3] = {
                obj->indices[offset],
                obj->indices[offset + v + 1],
                obj->indices[offset + v + 2]
            };

            for (int k = 0; k < 3; k++) {
                fastObjIndex idx = tri[k];
                CmshVertex *vert = &vertices[v_idx];

                if (idx.p) {
                    vert->position[0] = obj->positions[3 * idx.p + 0];
                    vert->position[1] = obj->positions[3 * idx.p + 1];
                    vert->position[2] = obj->positions[3 * idx.p + 2];
                } else {
                    vert->position[0] = vert->position[1] = vert->position[2] = 0.0f;
                }

                if (idx.n) {
                    vert->normal[0] = obj->normals[3 * idx.n + 0];
                    vert->normal[1] = obj->normals[3 * idx.n + 1];
                    vert->normal[2] = obj->normals[3 * idx.n + 2];
                } else {
                    vert->normal[0] = 0.0f;
                    vert->normal[1] = 1.0f;
                    vert->normal[2] = 0.0f;
                }

                if (idx.t) {
                    vert->uv1[0] = obj->texcoords[2 * idx.t + 0];
                    vert->uv1[1] = 1.0f - obj->texcoords[2 * idx.t + 1];
                } else {
                    vert->uv1[0] = vert->uv1[1] = 0.0f;
                }

                indices[v_idx] = v_idx;
                v_idx++;
            }
        }
        offset += face_verts;
    }

    fast_obj_destroy(obj);

    bool ok = pack_cmsh(vertices, total_indices, indices, total_indices, out_data, out_size);
    free(vertices);
    free(indices);
    return ok;
}