/* SPDX-License-Identifier: MIT */
/* NWOAS gpu-hv2 offline tests: minimal Apple Device Tree reader for a raw ADT
 * blob (same on-disk format m1n1 parses: node = {u32 nprops, u32 nchildren},
 * property = {char name[32], u32 size (bit31 = flag), value padded to 4}). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adt_host.h"

const uint8_t *adt_blob;
size_t adt_blob_len;

int adt_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);
    adt_blob = b; adt_blob_len = (size_t)n;
    return 0;
}

static uint32_t rd32(size_t off) { uint32_t v; memcpy(&v, adt_blob + off, 4); return v; }

/* offset of the first property of a node */
int adt_first_prop(int node) { return node + 8; }
int adt_nprops(int node) { return (int)rd32((size_t)node); }
int adt_nchildren(int node) { return (int)rd32((size_t)node + 4); }
int adt_next_prop(int prop) { return prop + 36 + (int)((rd32((size_t)prop + 32) & 0x7fffffff) + 3) / 4 * 4; }
const char *adt_prop_name(int prop) { return (const char *)adt_blob + prop; }
const void *adt_prop_value(int prop, uint32_t *len) { *len = rd32((size_t)prop + 32) & 0x7fffffff; return adt_blob + prop + 36; }

static int skip_node(int node)
{
    int p = adt_first_prop(node);
    for (int i = 0; i < adt_nprops(node); i++) p = adt_next_prop(p);
    int c = p;
    for (int i = 0; i < adt_nchildren(node); i++) c = skip_node(c);
    return c;
}

int adt_first_child(int node)
{
    int p = adt_first_prop(node);
    for (int i = 0; i < adt_nprops(node); i++) p = adt_next_prop(p);
    return p;
}
int adt_next_sibling(int node) { return skip_node(node); }

const void *adt_getprop(const void *tree, int node, const char *name, uint32_t *len)
{
    (void)tree;
    int p = adt_first_prop(node);
    for (int i = 0; i < adt_nprops(node); i++) {
        if (!strncmp(adt_prop_name(p), name, 32)) return adt_prop_value(p, len);
        p = adt_next_prop(p);
    }
    return NULL;
}

int adt_getprop_copy(const void *tree, int node, const char *name, void *out, size_t length)
{
    uint32_t len = 0;
    const void *v = adt_getprop(tree, node, name, &len);
    if (!v || len != length) return -1;
    memcpy(out, v, length);
    return (int)len;
}

static int child_by_name(int node, const char *name)
{
    int c = adt_first_child(node);
    for (int i = 0; i < adt_nchildren(node); i++) {
        uint32_t len = 0;
        const char *n = adt_getprop(NULL, c, "name", &len);
        if (n && !strncmp(n, name, len) && strlen(name) <= len && (strlen(name) == len || n[strlen(name)] == 0))
            return c;
        c = adt_next_sibling(c);
    }
    return -1;
}

int adt_path_offset(const void *tree, const char *path)
{
    (void)tree;
    char buf[256];
    int node = 0;
    strncpy(buf, path, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (char *t = strtok(buf, "/"); t; t = strtok(NULL, "/")) {
        node = child_by_name(node, t);
        if (node < 0) return -1;
    }
    return node;
}

/* Visit every node (depth first). */
static void walk(int node, void (*fn)(int, void *), void *ctx)
{
    fn(node, ctx);
    int c = adt_first_child(node);
    for (int i = 0; i < adt_nchildren(node); i++) { walk(c, fn, ctx); c = adt_next_sibling(c); }
}
void adt_walk(void (*fn)(int, void *), void *ctx) { walk(0, fn, ctx); }
