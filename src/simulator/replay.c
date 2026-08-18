#include "replay.h"

#include <stdio.h>
#include <stdlib.h>

static char *g_json = NULL;

bool replay_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_json = malloc(n + 1);
    if (!g_json) { fclose(f); return false; }
    size_t rd = fread(g_json, 1, n, f);
    g_json[rd] = '\0';
    fclose(f);
    return true;
}

const char *replay_json(void)
{
    return g_json ? g_json : "{}";
}