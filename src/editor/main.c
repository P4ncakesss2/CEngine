#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <strings.h>
#endif

#include "ecs/ecs.h"
#include "asset/asset.h"
#include "asset_baker.h"
#include "pak/pak_writer.h"
#include "scenes.h"

#define CBAKE_MAX_PATH 4096

static void normalize_slashes(char *path)
{
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
}

#ifdef _WIN32
typedef struct { HANDLE handle; WIN32_FIND_DATAA data; bool has_entry; } DirIter;

static bool dir_iter_open(DirIter *it, const char *directory) {
    char search[CBAKE_MAX_PATH];
    snprintf(search, sizeof(search), "%s/*", directory);
    it->handle = FindFirstFileA(search, &it->data);
    it->has_entry = (it->handle != INVALID_HANDLE_VALUE);
    return it->has_entry;
}
static const char *dir_iter_name(DirIter *it) { return it->data.cFileName; }
static void dir_iter_advance(DirIter *it) { it->has_entry = FindNextFileA(it->handle, &it->data) != 0; }
static void dir_iter_close(DirIter *it) { if (it->handle != INVALID_HANDLE_VALUE) FindClose(it->handle); }
static bool path_is_directory(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
#else
typedef struct { DIR *dir; struct dirent *entry; bool has_entry; } DirIter;

static bool dir_iter_open(DirIter *it, const char *directory) {
    it->dir = opendir(directory);
    if (!it->dir) { it->has_entry = false; return false; }
    it->entry = readdir(it->dir);
    it->has_entry = (it->entry != NULL);
    return it->has_entry;
}
static const char *dir_iter_name(DirIter *it) { return it->entry->d_name; }
static void dir_iter_advance(DirIter *it) { it->entry = readdir(it->dir); it->has_entry = (it->entry != NULL); }
static void dir_iter_close(DirIter *it) { if (it->dir) closedir(it->dir); }
static bool path_is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

static bool read_file(const char *path, void **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    void *data = malloc(size ? (size_t)size : 1);
    if (!data) { fclose(f); return false; }
    if (size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data); fclose(f); return false;
    }
    fclose(f);
    *out_data = data;
    *out_size = (size_t)size;
    return true;
}

static void replace_extension(char *name, size_t bufsize, const char *new_ext)
{
    char *dot = strrchr(name, '.');
    size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    if (base_len + 1 + strlen(new_ext) + 1 > bufsize) return;
    snprintf(name + base_len, bufsize - base_len, ".%s", new_ext);
}

static bool bake_or_read_file(const char *full_path, char *out_name,
                               size_t out_name_bufsize, void **out_data, size_t *out_size)
{
    const char *new_ext = NULL;
    AssetBakeKind kind = asset_bake_classify(out_name, &new_ext);

    bool ok;
    switch (kind) {
        case BAKE_KIND_TEXTURE:  ok = bake_texture_from_file(full_path, out_data, out_size); break;
        case BAKE_KIND_MESH_OBJ: ok = bake_mesh_from_obj_file(full_path, out_data, out_size); break;
        case BAKE_KIND_PASSTHROUGH:
        case BAKE_KIND_NONE:
        default:
            return read_file(full_path, out_data, out_size);
    }

    if (!ok) {
        fprintf(stderr, "ERROR: failed to bake asset: %s\n", full_path);
        return false;
    }
    if (new_ext) replace_extension(out_name, out_name_bufsize, new_ext);
    return true;
}

typedef struct {
    char full[CBAKE_MAX_PATH];
    char rel[CBAKE_MAX_PATH];
} SourceFile;

#define BAKE_MAX_FILES 4096
static SourceFile g_files[BAKE_MAX_FILES];
static int g_file_count = 0;

static void collect_files(const char *root, const char *dir)
{
    DirIter it;
    if (!dir_iter_open(&it, dir)) return;

    while (it.has_entry) {
        const char *name = dir_iter_name(&it);
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || name[0] == '.') {
            dir_iter_advance(&it);
            continue;
        }

        char full[CBAKE_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", dir, name);

        if (path_is_directory(full)) {
            collect_files(root, full);
        } else if (g_file_count < BAKE_MAX_FILES) {
            SourceFile *f = &g_files[g_file_count++];
            snprintf(f->full, sizeof(f->full), "%s", full);
            const char *rel = full + strlen(root);
            while (*rel == '/' || *rel == '\\') rel++;
            snprintf(f->rel, sizeof(f->rel), "%s", rel);
            normalize_slashes(f->rel);
        }

        dir_iter_advance(&it);
    }
    dir_iter_close(&it);
}

static bool bake_assets(const char *assets_dir, PakWriter *pak)
{
    g_file_count = 0;
    collect_files(assets_dir, assets_dir);

    bool *skip = calloc((size_t)g_file_count, sizeof(bool));
    for (int i = 0; i < g_file_count; i++) {
        char probe[CBAKE_MAX_PATH];
        snprintf(probe, sizeof(probe), "%s", g_files[i].rel);
        const char *new_ext = NULL;
        AssetBakeKind kind = asset_bake_classify(probe, &new_ext);
        if ((kind == BAKE_KIND_TEXTURE || kind == BAKE_KIND_MESH_OBJ) && new_ext) {
            replace_extension(probe, sizeof(probe), new_ext);
            for (int j = 0; j < g_file_count; j++) {
                if (j != i && strcmp(g_files[j].rel, probe) == 0) { skip[j] = true; break; }
            }
        }
    }

    bool ok = true;
    int packed = 0;
    for (int i = 0; i < g_file_count && ok; i++) {
        if (skip[i]) continue;

        char vpath[CBAKE_MAX_PATH];
        snprintf(vpath, sizeof(vpath), "%s", g_files[i].rel);

        void *data = NULL;
        size_t size = 0;
        if (!bake_or_read_file(g_files[i].full, vpath, sizeof(vpath), &data, &size)) {
            ok = false;
            break;
        }
        if (!pak_writer_add(pak, vpath, data, size)) {
            fprintf(stderr, "ERROR: failed to add '%s' to pak\n", vpath);
            ok = false;
        } else {
            printf("asset  %-40s %10zu bytes\n", vpath, size);
            packed++;
        }
        free(data);
    }

    free(skip);
    printf("Baked %d asset(s) from %s\n", packed, assets_dir);
    return ok;
}

static bool bake_scenes(PakWriter *pak)
{
    bool ok = true;
    for (int i = 0; i < kSceneCount; i++) {
        const SceneDef *def = &kScenes[i];

        Ecs world;
        if (ecs_init(&world) != ECS_OK) {
            fprintf(stderr, "ERROR: ecs_init failed for scene '%s'\n", def->name);
            ok = false;
            continue;
        }

        def->build(&world);

        uint8_t *data = NULL;
        size_t size = 0;
        EcsResult res = ecs_serialize(&world, &data, &size);
        ecs_free(&world);

        if (res != ECS_OK) {
            fprintf(stderr, "ERROR: failed to serialize scene '%s' (%s)\n",
                    def->name, ecs_result_str(res));
            ok = false;
            continue;
        }

        char vpath[512];
        snprintf(vpath, sizeof(vpath), "scenes/%s.scn", def->name);

        if (!pak_writer_add(pak, vpath, data, size)) {
            fprintf(stderr, "ERROR: failed to add scene '%s' to pak\n", vpath);
            ok = false;
        } else {
            printf("scene  %-40s %10zu bytes\n", vpath, size);
        }
        free(data);
    }
    printf("Baked %d scene(s)\n", kSceneCount);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <assets_dir> <out.pak>\n", argv[0]);
        return 1;
    }

    const char *assets_dir = argv[1];
    const char *out_pak    = argv[2];

    if (!path_is_directory(assets_dir)) {
        fprintf(stderr, "ERROR: not a directory: %s\n", assets_dir);
        return 1;
    }

    PakWriter *pak = pak_writer_open(out_pak);
    if (!pak) {
        fprintf(stderr, "ERROR: failed to create pak: %s\n", out_pak);
        return 1;
    }

    bool ok = bake_assets(assets_dir, pak);
    ok = bake_scenes(pak) && ok;

    if (!pak_writer_close(pak)) {
        fprintf(stderr, "ERROR: failed to finalize pak: %s\n", out_pak);
        return 1;
    }

    if (!ok) {
        fprintf(stderr, "Build finished with errors: %s\n", out_pak);
        return 1;
    }

    printf("Build complete: %s\n", out_pak);
    return 0;
}