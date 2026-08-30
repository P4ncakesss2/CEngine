#ifndef TOOL_ASSET_BAKER_H
#define TOOL_ASSET_BAKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BAKE_KIND_NONE = 0,
    BAKE_KIND_PASSTHROUGH,
    BAKE_KIND_TEXTURE,
    BAKE_KIND_MESH_OBJ, 
} AssetBakeKind;

AssetBakeKind asset_bake_classify(const char *filename, const char **out_new_ext);

bool bake_texture_from_file(const char *path, void **out_data, size_t *out_size);
bool bake_mesh_from_obj_file(const char *path, void **out_data, size_t *out_size);

#endif