/* SPDX-License-Identifier: MIT */
#ifndef RINGL_OBJECT_TABLE_H
#define RINGL_OBJECT_TABLE_H

#include <stdint.h>

struct RinGLContext;

typedef enum RinGLObjectType {
    RINGL_OBJECT_NONE = 0,
    RINGL_OBJECT_BUFFER = 1,
    RINGL_OBJECT_SHADER = 2,
    RINGL_OBJECT_PROGRAM = 3,
} RinGLObjectType;

typedef enum RinGLObjectState {
    RINGL_OBJECT_FREE = 0,
    RINGL_OBJECT_RESERVED = 1,
    RINGL_OBJECT_LIVE = 2,
} RinGLObjectState;

#define RINGL_OBJECT_SLOT_COUNT 1024u

typedef struct RinGLObjectSlot {
    uint16_t generation;
    uint8_t type;
    uint8_t state;
} RinGLObjectSlot;

uint32_t ringl_object_allocate(struct RinGLContext* context,
                               RinGLObjectType type);
RinGLObjectSlot* ringl_object_lookup(struct RinGLContext* context,
                                     uint32_t name,
                                     RinGLObjectType type);
const RinGLObjectSlot* ringl_object_lookup_const(const struct RinGLContext* context,
                                                 uint32_t name,
                                                 RinGLObjectType type);
void ringl_object_promote(RinGLObjectSlot* slot);
void ringl_object_release(struct RinGLContext* context,
                          uint32_t name,
                          RinGLObjectType type);
uint32_t ringl_object_slot_index(uint32_t name);

#endif /* RINGL_OBJECT_TABLE_H */
