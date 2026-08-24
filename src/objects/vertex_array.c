/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <string.h>

static void ringl_vertex_array_copy_descriptor(RinGLVertexAttribState* to,
                                               const RinGLVertexAttribState* from)
{
    /* current_value is context state, not vertex-array state. */
    to->enabled = from->enabled;
    to->size = from->size;
    to->type = from->type;
    to->normalized = from->normalized;
    to->stride = from->stride;
    to->divisor = from->divisor;
    to->buffer = from->buffer;
    to->offset = from->offset;
}

static void ringl_vertex_array_save(RinGLContext* context,
                                    RinGLVertexArrayState* state)
{
    uint32_t index;

    state->element_array_buffer = context->element_array_buffer;
    for (index = 0u; index < RINGL_MAX_VERTEX_ATTRIBS; ++index)
        ringl_vertex_array_copy_descriptor(&state->vertex_attribs[index],
                                           &context->vertex_attribs[index]);
}

static void ringl_vertex_array_restore(RinGLContext* context,
                                       const RinGLVertexArrayState* state)
{
    uint32_t index;

    context->element_array_buffer = state->element_array_buffer;
    for (index = 0u; index < RINGL_MAX_VERTEX_ATTRIBS; ++index)
        ringl_vertex_array_copy_descriptor(&context->vertex_attribs[index],
                                           &state->vertex_attribs[index]);
}

static RinGLVertexArrayState* ringl_vertex_array_state(RinGLContext* context,
                                                        uint32_t array)
{
    uint32_t index;

    if (array == 0u)
        return &context->default_vertex_array;
    if (ringl_object_lookup(context, array, RINGL_OBJECT_VERTEX_ARRAY) == NULL)
        return NULL;
    index = ringl_object_slot_index(array);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->vertex_arrays[index];
}

void ringl_gen_vertex_arrays(int32_t count, uint32_t* arrays)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0 || (count != 0 && arrays == NULL)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i)
        arrays[i] = 0u;
    for (i = 0; i < count; ++i) {
        uint32_t array = ringl_object_allocate(context, RINGL_OBJECT_VERTEX_ARRAY);
        uint32_t index;

        if (array == 0u) {
            int32_t rollback;

            for (rollback = 0; rollback < i; ++rollback)
                ringl_object_release(context, arrays[rollback],
                                     RINGL_OBJECT_VERTEX_ARRAY);
            for (rollback = 0; rollback < i; ++rollback)
                arrays[rollback] = 0u;
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        index = ringl_object_slot_index(array);
        if (index >= RINGL_OBJECT_SLOT_COUNT) {
            ringl_object_release(context, array, RINGL_OBJECT_VERTEX_ARRAY);
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        memset(&context->vertex_arrays[index], 0,
               sizeof(context->vertex_arrays[index]));
        for (uint32_t attrib = 0u; attrib < RINGL_MAX_VERTEX_ATTRIBS; ++attrib) {
            context->vertex_arrays[index].vertex_attribs[attrib].size = 4u;
            context->vertex_arrays[index].vertex_attribs[attrib].type =
                RINGL_FLOAT;
        }
        arrays[i] = array;
    }
}

void ringl_delete_vertex_arrays(int32_t count, const uint32_t* arrays)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0 || (count != 0 && arrays == NULL)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i) {
        uint32_t array = arrays[i];
        uint32_t index;

        if (array == 0u ||
            ringl_object_lookup(context, array, RINGL_OBJECT_VERTEX_ARRAY) == NULL)
            continue;

        index = ringl_object_slot_index(array);
        if (context->vertex_array_binding == array) {
            ringl_vertex_array_save(context, &context->vertex_arrays[index]);
            ringl_vertex_array_restore(context, &context->default_vertex_array);
            context->vertex_array_binding = 0u;
        }
        if (index < RINGL_OBJECT_SLOT_COUNT)
            memset(&context->vertex_arrays[index], 0,
                   sizeof(context->vertex_arrays[index]));
        ringl_object_release(context, array, RINGL_OBJECT_VERTEX_ARRAY);
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

void ringl_bind_vertex_array(uint32_t array)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot = NULL;
    RinGLVertexArrayState* old_state;
    RinGLVertexArrayState* new_state;

    if (context == NULL)
        return;
    if (array != 0u) {
        slot = ringl_object_lookup(context, array, RINGL_OBJECT_VERTEX_ARRAY);
        if (slot == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
    }
    if (context->vertex_array_binding == array)
        return;

    old_state = ringl_vertex_array_state(context, context->vertex_array_binding);
    new_state = ringl_vertex_array_state(context, array);
    if (old_state == NULL || new_state == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    ringl_vertex_array_save(context, old_state);
    if (slot != NULL)
        ringl_object_promote(slot);
    ringl_vertex_array_restore(context, new_state);
    context->vertex_array_binding = array;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

int ringl_is_vertex_array(uint32_t array)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;

    if (context == NULL || array == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, array, RINGL_OBJECT_VERTEX_ARRAY);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

uint32_t ringl_get_bound_vertex_array(void)
{
    RinGLContext* context = ringl_get_current_context();

    return context == NULL ? 0u : context->vertex_array_binding;
}

void ringl_vertex_array_detach_buffer(RinGLContext* context, uint32_t buffer)
{
    uint32_t array;
    uint32_t attrib;
    int changed = 0;

    if (context == NULL || buffer == 0u)
        return;

    if (context->element_array_buffer == buffer) {
        context->element_array_buffer = 0u;
        changed = 1;
    }
    for (attrib = 0u; attrib < RINGL_MAX_VERTEX_ATTRIBS; ++attrib) {
        RinGLVertexAttribState* descriptor = &context->vertex_attribs[attrib];

        if (descriptor->buffer != buffer)
            continue;
        descriptor->buffer = 0u;
        descriptor->offset = 0u;
        descriptor->stride = 0u;
        descriptor->enabled = RINGL_FALSE;
        changed = 1;
    }

    for (array = 0u; array < RINGL_OBJECT_SLOT_COUNT; ++array) {
        RinGLVertexArrayState* state = &context->vertex_arrays[array];

        if (context->objects[array].type != RINGL_OBJECT_VERTEX_ARRAY ||
            context->objects[array].state == RINGL_OBJECT_FREE)
            continue;
        if (state->element_array_buffer == buffer) {
            state->element_array_buffer = 0u;
            changed = 1;
        }
        for (attrib = 0u; attrib < RINGL_MAX_VERTEX_ATTRIBS; ++attrib) {
            RinGLVertexAttribState* descriptor = &state->vertex_attribs[attrib];

            if (descriptor->buffer != buffer)
                continue;
            descriptor->buffer = 0u;
            descriptor->offset = 0u;
            descriptor->stride = 0u;
            descriptor->enabled = RINGL_FALSE;
            changed = 1;
        }
    }

    if (context->default_vertex_array.element_array_buffer == buffer) {
        context->default_vertex_array.element_array_buffer = 0u;
        changed = 1;
    }
    for (attrib = 0u; attrib < RINGL_MAX_VERTEX_ATTRIBS; ++attrib) {
        RinGLVertexAttribState* descriptor =
            &context->default_vertex_array.vertex_attribs[attrib];

        if (descriptor->buffer != buffer)
            continue;
        descriptor->buffer = 0u;
        descriptor->offset = 0u;
        descriptor->stride = 0u;
        descriptor->enabled = RINGL_FALSE;
        changed = 1;
    }

    if (changed) {
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}
