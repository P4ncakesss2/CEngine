#include "cvar.h"
#include <stdlib.h>

Cvar *g_cvar_list = NULL;

Cvar *cvar_find(const char *name) {
    for (Cvar *c = g_cvar_list; c; c = c->next)
        if (strcmp(c->name, name) == 0)
            return c;
    return NULL;
}

int cvar_set(const char *name, const char *value_str) {
    Cvar *c = cvar_find(name);
    if (!c) return 0;

    switch (c->type) {
        case CVAR_INT:
            c->value.i = atoi(value_str);
            break;
        case CVAR_FLOAT:
            c->value.f = (float)atof(value_str);
            break;
        case CVAR_STRING:
            free(c->value.s);
            c->value.s = strdup(value_str);
            break;
    }
    return 1;
}