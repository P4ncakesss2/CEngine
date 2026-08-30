#ifndef MESH_ASSET_H
#define MESH_ASSET_H

#include "asset.h"
#include <cglm/cglm.h>
#include <stdint.h>

typedef struct {
    vec3 position;
    vec3 normal;
    vec2 uv1;
} MeshVertex;

typedef struct {
    MeshVertex *vertices;
    uint32_t    vertex_count;
    uint32_t   *indices;
    uint32_t    index_count;
} MeshAsset;

#define MESH_PROC_CUBE   "proc:cube"
#define MESH_PROC_SPHERE "proc:sphere"
#define MESH_PROC_PLANE  "proc:plane"

#define CMSH_MAGIC   0x48534D43u /* 'CMSH' */
#define CMSH_VERSION 1u

typedef struct {
    float position[3];
    float normal[3];
    float uv1[2];
} CmshVertex;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
} CmshHeader;

AssetResult mesh_asset_type_init(AssetManager *mgr);

#endif