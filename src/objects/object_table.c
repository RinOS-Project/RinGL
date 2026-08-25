/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>

static uint32_t ringl_encode_name(uint32_t index, uint16_t generation)
{
    return ((uint32_t)generation << 16) | (index + 1u);
}

static int ringl_decode_name(uint32_t name,
                             uint32_t* index_out,
                             uint16_t* generation_out)
{
    uint32_t encoded_index;
    uint16_t generation;

    if (name == 0u)
        return 0;

    encoded_index = name & 0xffffu;
    generation = (uint16_t)(name >> 16);
    if (encoded_index == 0u || encoded_index > RINGL_OBJECT_SLOT_COUNT ||
        generation == 0u) {
        return 0;
    }

    if (index_out != NULL)
        *index_out = encoded_index - 1u;
    if (generation_out != NULL)
        *generation_out = generation;
    return 1;
}

uint32_t ringl_object_allocate(RinGLContext* context, RinGLObjectType type)
{
    uint32_t index;

    if (context == NULL || type == RINGL_OBJECT_NONE)
        return 0u;

    for (index = 0; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        RinGLObjectSlot* slot = &context->objects[index];

        if (slot->state != RINGL_OBJECT_FREE)
            continue;

        if (slot->generation == 0u)
            slot->generation = 1u;
        slot->type = (uint8_t)type;
        slot->state = RINGL_OBJECT_RESERVED;
        return ringl_encode_name(index, slot->generation);
    }

    return 0u;
}

RinGLObjectSlot* ringl_object_lookup(RinGLContext* context,
                                     uint32_t name,
                                     RinGLObjectType type)
{
    uint32_t index;
    uint16_t generation;
    RinGLObjectSlot* slot;

    if (context == NULL || !ringl_decode_name(name, &index, &generation))
        return NULL;

    slot = &context->objects[index];
    if (slot->state == RINGL_OBJECT_FREE ||
        slot->generation != generation ||
        slot->type != (uint8_t)type) {
        return NULL;
    }

    return slot;
}

const RinGLObjectSlot* ringl_object_lookup_const(const RinGLContext* context,
                                                 uint32_t name,
                                                 RinGLObjectType type)
{
    uint32_t index;
    uint16_t generation;
    const RinGLObjectSlot* slot;

    if (context == NULL || !ringl_decode_name(name, &index, &generation))
        return NULL;

    slot = &context->objects[index];
    if (slot->state == RINGL_OBJECT_FREE ||
        slot->generation != generation ||
        slot->type != (uint8_t)type) {
        return NULL;
    }

    return slot;
}

void ringl_object_promote(RinGLObjectSlot* slot)
{
    if (slot != NULL && slot->state == RINGL_OBJECT_RESERVED)
        slot->state = RINGL_OBJECT_LIVE;
}

void ringl_object_release(RinGLContext* context,
                          uint32_t name,
                          RinGLObjectType type)
{
    RinGLObjectSlot* slot = ringl_object_lookup(context, name, type);

    if (slot == NULL)
        return;

    slot->state = RINGL_OBJECT_FREE;
    slot->type = RINGL_OBJECT_NONE;
    ++slot->generation;
    if (slot->generation == 0u)
        slot->generation = 1u;
}

uint32_t ringl_object_slot_index(uint32_t name)
{
    uint32_t index = 0u;

    if (!ringl_decode_name(name, &index, NULL))
        return RINGL_OBJECT_SLOT_COUNT;
    return index;
}
