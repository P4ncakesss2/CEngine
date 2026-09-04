#ifndef CVAR_H
#define CVAR_H

#include <string.h>

typedef enum {
    CVAR_INT,
    CVAR_FLOAT,
    CVAR_STRING
} CvarType;

typedef struct Cvar {
    const char *name;
    CvarType type;
    union {
        int i;
        float f;
        char *s;
    } value;
    struct Cvar *next;
} Cvar;

extern Cvar *g_cvar_list; 

#define DEFINE_CVAR_INT(var_name, str_name, default_val) \
    Cvar var_name = { str_name, CVAR_INT, { .i = (default_val) }, NULL }; \
    __attribute__((constructor)) static void _register_##var_name(void) { \
        var_name.next = g_cvar_list; \
        g_cvar_list = &var_name; \
    }

#define DEFINE_CVAR_FLOAT(var_name, str_name, default_val) \
    Cvar var_name = { str_name, CVAR_FLOAT, { .f = (default_val) }, NULL }; \
    __attribute__((constructor)) static void _register_##var_name(void) { \
        var_name.next = g_cvar_list; \
        g_cvar_list = &var_name; \
    }

#define DEFINE_CVAR_STRING(var_name, str_name, default_val) \
    Cvar var_name = { str_name, CVAR_STRING, { .s = NULL }, NULL }; \
    __attribute__((constructor)) static void _register_##var_name(void) { \
        var_name.value.s = strdup(default_val); \
        var_name.next = g_cvar_list; \
        g_cvar_list = &var_name; \
    }

Cvar *cvar_find(const char *name);
int   cvar_set(const char *name, const char *value_str);

#endif /* CVAR_H */