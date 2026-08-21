/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <string.h>

static int ringl_buffer_target_valid(uint32_t target)
{
    return target == RINGL_ARRAY_BUFFER || target == RINGL_ELEMENT_ARRAY_BUFFER;
}

static uint32_t* ringl_buffer_binding_for_target(RinGLContext* context,
                                                 uint32_t target)
{
    if (target == RINGL_ARRAY_BUFFER)
        return &context->array_buffer;
    if (target == RINGL_ELEMENT_ARRAY_BUFFER)
        return &context->element_array_buffer;
    return NULL;
}

void ringl_gen_buffers(int32_t count, uint32_t* buffers)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (buffers == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i)
        buffers[i] = 0u;

    for (i = 0; i < count; ++i) {
        buffers[i] = ringl_object_allocate(context, RINGL_OBJECT_BUFFER);
        if (buffers[i] == 0u) {
            int32_t rollback;

            for (rollback = 0; rollback < i; ++rollback) {
                ringl_object_release(context, buffers[rollback],
                                     RINGL_OBJECT_BUFFER);
                buffers[rollback] = 0u;
            }
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
    }
}

void ringl_delete_buffers(int32_t count, const uint32_t* buffers)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (buffers == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i) {
        uint32_t name = buffers[i];
        uint32_t slot_index;

        if (name == 0u ||
            ringl_object_lookup(context, name, RINGL_OBJECT_BUFFER) == NULL) {
            continue;
        }

        if (context->array_buffer == name)
            context->array_buffer = 0u;
        if (context->element_array_buffer == name)
            context->element_array_buffer = 0u;

        slot_index = ringl_object_slot_index(name);
        if (slot_index < RINGL_OBJECT_SLOT_COUNT)
            memset(&context->buffers[slot_index], 0,
                   sizeof(context->buffers[slot_index]));

        ringl_object_release(context, name, RINGL_OBJECT_BUFFER);
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
}

void ringl_bind_buffer(uint32_t target, uint32_t buffer)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t* binding;
    RinGLObjectSlot* slot;

    if (context == NULL)
        return;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }

    binding = ringl_buffer_binding_for_target(context, target);
    if (buffer != 0u) {
        slot = ringl_object_lookup(context, buffer, RINGL_OBJECT_BUFFER);
        if (slot == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_object_promote(slot);
    }

    if (*binding != buffer) {
        *binding = buffer;
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
}

int ringl_is_buffer(uint32_t buffer)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;

    if (context == NULL || buffer == 0u)
        return 0;

    slot = ringl_object_lookup_const(context, buffer, RINGL_OBJECT_BUFFER);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

uint32_t ringl_get_bound_buffer(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return 0u;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }

    if (target == RINGL_ARRAY_BUFFER)
        return context->array_buffer;
    return context->element_array_buffer;
}
