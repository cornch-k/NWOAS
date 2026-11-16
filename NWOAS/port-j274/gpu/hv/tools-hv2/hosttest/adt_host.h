/* SPDX-License-Identifier: MIT */
#ifndef ADT_HOST_H
#define ADT_HOST_H
#include <stddef.h>
#include <stdint.h>
extern const uint8_t *adt_blob;
extern size_t adt_blob_len;
int adt_load(const char *path);
int adt_first_prop(int node);
int adt_nprops(int node);
int adt_next_prop(int prop);
const char *adt_prop_name(int prop);
const void *adt_prop_value(int prop, uint32_t *len);
const void *adt_getprop(const void *tree, int node, const char *name, uint32_t *len);
int adt_getprop_copy(const void *tree, int node, const char *name, void *out, size_t length);
int adt_path_offset(const void *tree, const char *path);
void adt_walk(void (*fn)(int, void *), void *ctx);
#endif
