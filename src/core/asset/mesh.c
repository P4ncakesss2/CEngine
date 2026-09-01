#include "mesh.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const vec2 kFaceUVs[4] = {
    {1.0f, 0.0f},
    {0.0f, 0.0f},
    {0.0f, 1.0f},
    {1.0f, 1.0f},
};

static const uint32_t kFacePattern[6] = { 0, 1, 2, 2, 3, 0 };

static const vec3 kFaceNormals[6] = {
    { 0.0f,  0.0f, -1.0f},
    { 0.0f,  0.0f,  1.0f},
    { 1.0f,  0.0f,  0.0f},
    {-1.0f,  0.0f,  0.0f},
    { 0.0f,  1.0f,  0.0f},
    { 0.0f, -1.0f,  0.0f},
};

static MeshAsset *make_cube(void) {
    MeshAsset *mesh = malloc(sizeof(MeshAsset));
    if (!mesh) return NULL;

    mesh->vertex_count = 24;
    mesh->vertices = malloc(24 * sizeof(MeshVertex));
    if (!mesh->vertices) { free(mesh); return NULL; }

    mesh->index_count = 36;
    mesh->indices = malloc(36 * sizeof(uint32_t));
    if (!mesh->indices) { free(mesh->vertices); free(mesh); return NULL; }

    const float half = 0.5f;

    for (int f = 0; f < 6; f++) {
        vec3 n;
        glm_vec3_copy((float *)kFaceNormals[f], n);

        vec3 refUp = { 0.0f, 1.0f, 0.0f };
        if (fabsf(n[1]) > 0.99f) {
            refUp[0] = 0.0f; refUp[1] = 0.0f; refUp[2] = 1.0f;
        }

        vec3 viewDir = { -n[0], -n[1], -n[2] };

        vec3 right;
        glm_vec3_cross(refUp, viewDir, right);
        glm_vec3_normalize(right);

        vec3 texUp;
        glm_vec3_cross(n, right, texUp);
        glm_vec3_normalize(texUp);

        vec3 center = { n[0] * half, n[1] * half, n[2] * half };

        static const float kSx[4] = { -1.0f,  1.0f, 1.0f, -1.0f };
        static const float kSy[4] = { -1.0f, -1.0f, 1.0f,  1.0f };

        for (int c = 0; c < 4; c++) {
            float sx = kSx[c] * half;
            float sy = kSy[c] * half;

            MeshVertex *vert = &mesh->vertices[f * 4 + c];
            vert->position[0] = center[0] + right[0] * sx + texUp[0] * sy;
            vert->position[1] = center[1] + right[1] * sx + texUp[1] * sy;
            vert->position[2] = center[2] + right[2] * sx + texUp[2] * sy;

            glm_vec3_copy(n, vert->normal);
            glm_vec2_copy((float *)kFaceUVs[c], vert->uv1);
        }

        for (int t = 0; t < 6; t++) {
            mesh->indices[f * 6 + t] = (uint32_t)(f * 4) + kFacePattern[t];
        }
    }

    return mesh;
}

static MeshAsset *make_sphere(uint32_t rings, uint32_t segments) {
    MeshAsset *mesh = malloc(sizeof(MeshAsset));
    if (!mesh) return NULL;

    mesh->vertex_count = (rings + 1) * (segments + 1);
    mesh->vertices = malloc(mesh->vertex_count * sizeof(MeshVertex));
    if (!mesh->vertices) { free(mesh); return NULL; }

    uint32_t vi = 0;
    for (uint32_t r = 0; r <= rings; r++) {
        float v = (float)r / (float)rings;
        float phi = v * (float)M_PI;
        for (uint32_t s = 0; s <= segments; s++) {
            float u = (float)s / (float)segments;
            float theta = u * 2.0f * (float)M_PI;
            vec3 pos = {
                sinf(phi) * cosf(theta) * 0.5f,
                cosf(phi) * 0.5f,
                sinf(phi) * sinf(theta) * 0.5f,
            };
            glm_vec3_copy(pos, mesh->vertices[vi].position);
            glm_vec3_normalize_to(pos, mesh->vertices[vi].normal);
            mesh->vertices[vi].uv1[0] = u;
            mesh->vertices[vi].uv1[1] = v;
            vi++;
        }
    }

    mesh->index_count = rings * segments * 6;
    mesh->indices = malloc(mesh->index_count * sizeof(uint32_t));
    if (!mesh->indices) { free(mesh->vertices); free(mesh); return NULL; }

    uint32_t ii = 0;
    for (uint32_t r = 0; r < rings; r++) {
        for (uint32_t s = 0; s < segments; s++) {
            uint32_t a = r * (segments + 1) + s;
            uint32_t b = a + segments + 1;
            mesh->indices[ii++] = a; mesh->indices[ii++] = a + 1; mesh->indices[ii++] = b;
            mesh->indices[ii++] = a + 1; mesh->indices[ii++] = b + 1; mesh->indices[ii++] = b;
        }
    }

    return mesh;
}

static MeshAsset *load_cmsh(const void *data, size_t size) {
    if (!data || size < sizeof(CmshHeader)) return NULL;

    const CmshHeader *header = (const CmshHeader *)data;
    if (header->magic != CMSH_MAGIC || header->version != CMSH_VERSION) return NULL;
    if (header->vertex_count == 0 || header->index_count == 0) return NULL;

    size_t vtx_bytes = (size_t)header->vertex_count * sizeof(CmshVertex);
    size_t idx_bytes = (size_t)header->index_count * sizeof(uint32_t);
    if (size != sizeof(CmshHeader) + vtx_bytes + idx_bytes) return NULL;

    MeshAsset *mesh = malloc(sizeof(MeshAsset));
    if (!mesh) return NULL;

    mesh->vertex_count = header->vertex_count;
    mesh->index_count  = header->index_count;
    mesh->vertices = malloc(vtx_bytes);
    mesh->indices  = malloc(idx_bytes);
    if (!mesh->vertices || !mesh->indices) {
        free(mesh->vertices);
        free(mesh->indices);
        free(mesh);
        return NULL;
    }

    const uint8_t *cursor = (const uint8_t *)data + sizeof(CmshHeader);

    memcpy(mesh->vertices, cursor, vtx_bytes);
    cursor += vtx_bytes;
    memcpy(mesh->indices, cursor, idx_bytes);

    return mesh;
}

static MeshAsset *make_plane(void) {
    MeshAsset *mesh = malloc(sizeof(MeshAsset));
    if (!mesh) return NULL;

    mesh->vertex_count = 4;
    mesh->vertices = malloc(mesh->vertex_count * sizeof(MeshVertex));
    if (!mesh->vertices) { 
        free(mesh); 
        return NULL; 
    }

    mesh->index_count = 6;
    mesh->indices = malloc(mesh->index_count * sizeof(uint32_t));
    if (!mesh->indices) { 
        free(mesh->vertices); 
        free(mesh); 
        return NULL; 
    }

    const float half = 1.0f;

    vec3 positions[4] = {
        { half, 0.0f, -half},
        {-half, 0.0f, -half}, 
        {-half, 0.0f,  half}, 
        { half, 0.0f,  half}
    };
    
    vec3 normal = { 0.0f, 1.0f, 0.0f };

    for (int i = 0; i < 4; i++) {
        glm_vec3_copy(positions[i], mesh->vertices[i].position);
        glm_vec3_copy(normal, mesh->vertices[i].normal);
        glm_vec2_copy((float *)kFaceUVs[i], mesh->vertices[i].uv1);
    }

    for (int i = 0; i < 6; i++) {
        mesh->indices[i] = kFacePattern[i];
    }

    return mesh;
}

static MeshAsset *make_capsule(float radius, float height, uint32_t rings, uint32_t segments) {
    float cylinderHeight = height - 2.0f * radius;
    if (cylinderHeight < 0.0f) cylinderHeight = 0.0f;
    float halfCyl = cylinderHeight * 0.5f;

    uint32_t ringsPerCap = rings;
    uint32_t totalRings = ringsPerCap * 2 + 1;

    uint32_t ringCount = (ringsPerCap + 1) * 2; 
    MeshAsset *mesh = malloc(sizeof(MeshAsset));
    if (!mesh) return NULL;

    mesh->vertex_count = ringCount * (segments + 1);
    mesh->vertices = malloc(mesh->vertex_count * sizeof(MeshVertex));
    if (!mesh->vertices) { free(mesh); return NULL; }

    uint32_t vi = 0;
    for (uint32_t half = 0; half < 2; half++) {
        for (uint32_t r = 0; r <= ringsPerCap; r++) {
            float t = (float)r / (float)ringsPerCap;
            float phi = half == 0 ? (t * 0.5f * (float)M_PI) : (0.5f * (float)M_PI + t * 0.5f * (float)M_PI);

            float ringRadius = sinf(phi) * radius;
            float y = cosf(phi) * radius;
            y += (half == 0) ? halfCyl : -halfCyl;

            float vCoord = (half == 0)
                ? (t * (radius / (radius * 2.0f + cylinderHeight)))
                : (1.0f - (1.0f - t) * (radius / (radius * 2.0f + cylinderHeight)));

            for (uint32_t s = 0; s <= segments; s++) {
                float u = (float)s / (float)segments;
                float theta = u * 2.0f * (float)M_PI;

                vec3 pos = {
                    ringRadius * cosf(theta),
                    y,
                    ringRadius * sinf(theta),
                };
                glm_vec3_copy(pos, mesh->vertices[vi].position);

                vec3 n = {
                    sinf(phi) * cosf(theta),
                    cosf(phi) * (half == 0 ? 1.0f : -1.0f) * (half == 0 ? 1.0f : 1.0f),
                    sinf(phi) * sinf(theta),
                };
                float ny = cosf(half == 0 ? phi : ((float)M_PI - phi));
                n[1] = ny;
                glm_vec3_normalize(n);
                glm_vec3_copy(n, mesh->vertices[vi].normal);

                mesh->vertices[vi].uv1[0] = u;
                mesh->vertices[vi].uv1[1] = vCoord;
                vi++;
            }
        }
    }

    mesh->index_count = (ringCount - 1) * segments * 6;
    mesh->indices = malloc(mesh->index_count * sizeof(uint32_t));
    if (!mesh->indices) { free(mesh->vertices); free(mesh); return NULL; }

    uint32_t ii = 0;
    for (uint32_t r = 0; r < ringCount - 1; r++) {
        for (uint32_t s = 0; s < segments; s++) {
            uint32_t a = r * (segments + 1) + s;
            uint32_t b = a + segments + 1;
            mesh->indices[ii++] = a; mesh->indices[ii++] = a + 1; mesh->indices[ii++] = b;
            mesh->indices[ii++] = a + 1; mesh->indices[ii++] = b + 1; mesh->indices[ii++] = b;
        }
    }

    return mesh;
}

static void *mesh_load(const char *vpath, const void *data, size_t size) {
    if (strcmp(vpath, MESH_PROC_CUBE) == 0)   return make_cube();
    if (strcmp(vpath, MESH_PROC_SPHERE) == 0) return make_sphere(16, 24);
    if (strcmp(vpath, MESH_PROC_PLANE) == 0) return make_plane();
    if (strcmp(vpath, MESH_PROC_CAPSULE) == 0) return make_capsule(0.3f, 1.8f, 8, 24);

    return load_cmsh(data, size);
}

static void mesh_free(void *asset_data) {
    MeshAsset *mesh = asset_data;
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    free(mesh);
}

AssetResult mesh_asset_type_init(AssetManager *mgr) {
    return asset_type_register(mgr, ASSET_TYPE_Mesh, mesh_load, mesh_free);
}