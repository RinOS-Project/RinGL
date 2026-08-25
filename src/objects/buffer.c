/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int ringl_buffer_target_valid(uint32_t target)
{
    return target == RINGL_ARRAY_BUFFER || target == RINGL_ELEMENT_ARRAY_BUFFER;
}

static int ringl_buffer_usage_valid(uint32_t usage)
{
    return usage == RINGL_STREAM_DRAW || usage == RINGL_STATIC_DRAW ||
           usage == RINGL_DYNAMIC_DRAW;
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

static RinGLBufferObject* ringl_bound_buffer_object(RinGLContext* context,
                                                    uint32_t target)
{
    uint32_t* binding = ringl_buffer_binding_for_target(context, target);
    uint32_t index;

    if (binding == NULL || *binding == 0u)
        return NULL;
    if (ringl_object_lookup(context, *binding, RINGL_OBJECT_BUFFER) == NULL)
        return NULL;

    index = ringl_object_slot_index(*binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->buffers[index];
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

        /* Attribute descriptors and element bindings can live in an
         * inactive VAO. Detach every reference before the numeric name is
         * released so a later allocation cannot turn stale state into a
         * reference to an unrelated buffer. */
        ringl_vertex_array_detach_buffer(context, name);

        slot_index = ringl_object_slot_index(name);
        if (slot_index < RINGL_OBJECT_SLOT_COUNT) {
            ringl_backend_destroy_object(context,
                                         context->buffers[slot_index].ringpu_handle);
            free(context->buffers[slot_index].shadow_bytes);
            memset(&context->buffers[slot_index], 0,
                   sizeof(context->buffers[slot_index]));
        }

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

static void ringl_buffer_data_impl(uint32_t target,
                                   int64_t size_bytes,
                                   const void* data,
                                   uint64_t data_size,
                                   int data_size_known,
                                   uint32_t usage)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLBufferObject* object;
    uint64_t new_handle = 0u;
    uint8_t* new_shadow = NULL;

    if (context == NULL)
        return;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (!ringl_buffer_usage_valid(usage)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (size_bytes < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if ((uint64_t)size_bytes > (uint64_t)SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (data_size_known &&
        ((data == NULL && data_size != 0u) ||
         (data != NULL && (uint64_t)size_bytes > data_size))) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    object = ringl_bound_buffer_object(context, target);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (size_bytes > 0) {
        new_shadow = malloc((size_t)size_bytes);
        if (new_shadow == NULL) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        if (data != NULL)
            memcpy(new_shadow, data, (size_t)size_bytes);
        else
            memset(new_shadow, 0, (size_t)size_bytes);

        if (ringl_backend_create_buffer(context, (uint64_t)size_bytes,
                                        &new_handle) != 0 ||
            new_handle == 0u) {
            free(new_shadow);
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }

        /* Keep GPU storage and the robust CPU shadow identical. Zero-filled
         * storage for a NULL source is intentional in RinGL's bounded profile. */
        if (ringl_backend_upload_buffer(context, new_handle, 0u, new_shadow,
                                        (uint64_t)size_bytes) != 0) {
            ringl_backend_destroy_object(context, new_handle);
            free(new_shadow);
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
    }

    ringl_backend_destroy_object(context, object->ringpu_handle);
    free(object->shadow_bytes);
    object->ringpu_handle = new_handle;
    object->size_bytes = (uint64_t)size_bytes;
    object->shadow_bytes = new_shadow;
    object->usage = usage;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_buffer_data(uint32_t target,
                       int64_t size_bytes,
                       const void* data,
                       uint32_t usage)
{
    ringl_buffer_data_impl(target, size_bytes, data, 0u, 0, usage);
}

void ringl_buffer_data_from_bytes(uint32_t target,
                                  int64_t size_bytes,
                                  const void* data,
                                  uint64_t data_size,
                                  uint32_t usage)
{
    ringl_buffer_data_impl(target, size_bytes, data, data_size, 1, usage);
}

static void ringl_buffer_sub_data_impl(uint32_t target,
                                       int64_t offset_bytes,
                                       int64_t size_bytes,
                                       const void* data,
                                       uint64_t data_size,
                                       int data_size_known)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLBufferObject* object;
    uint8_t* replacement_shadow;
    uint64_t replacement_handle = 0u;

    if (context == NULL)
        return;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (offset_bytes < 0 || size_bytes < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (size_bytes > 0 && data == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (data_size_known &&
        ((data == NULL && data_size != 0u) ||
         (data != NULL && (uint64_t)size_bytes > data_size))) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    object = ringl_bound_buffer_object(context, target);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint64_t)offset_bytes > object->size_bytes ||
        (uint64_t)size_bytes > object->size_bytes - (uint64_t)offset_bytes) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (size_bytes == 0)
        return;

    /* RinGPU uploads may fail after a backend-specific operation. Build a
     * complete replacement instead of mutating the current backing object,
     * keeping the old logical buffer and its CPU shadow intact on failure. */
    if (object->ringpu_handle == 0u || object->shadow_bytes == NULL ||
        object->size_bytes > (uint64_t)SIZE_MAX) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    replacement_shadow = malloc((size_t)object->size_bytes);
    if (replacement_shadow == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    memcpy(replacement_shadow, object->shadow_bytes,
           (size_t)object->size_bytes);
    memcpy(replacement_shadow + (size_t)offset_bytes, data,
           (size_t)size_bytes);

    if (ringl_backend_create_buffer(context, object->size_bytes,
                                    &replacement_handle) != 0 ||
        replacement_handle == 0u) {
        free(replacement_shadow);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (ringl_backend_upload_buffer(context, replacement_handle, 0u,
                                    replacement_shadow,
                                    object->size_bytes) != 0) {
        ringl_backend_destroy_object(context, replacement_handle);
        free(replacement_shadow);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }

    ringl_backend_destroy_object(context, object->ringpu_handle);
    free(object->shadow_bytes);
    object->ringpu_handle = replacement_handle;
    object->shadow_bytes = replacement_shadow;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_buffer_sub_data(uint32_t target,
                            int64_t offset_bytes,
                            int64_t size_bytes,
                            const void* data)
{
    ringl_buffer_sub_data_impl(target, offset_bytes, size_bytes, data, 0u, 0);
}

void ringl_buffer_sub_data_from_bytes(uint32_t target,
                                      int64_t offset_bytes,
                                      int64_t size_bytes,
                                      const void* data,
                                      uint64_t data_size)
{
    ringl_buffer_sub_data_impl(target, offset_bytes, size_bytes, data,
                               data_size, 1);
}

uint64_t ringl_get_buffer_size(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLBufferObject* object;

    if (context == NULL)
        return 0u;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }

    object = ringl_bound_buffer_object(context, target);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return 0u;
    }
    return object->size_bytes;
}

uint32_t ringl_get_buffer_usage(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLBufferObject* object;

    if (context == NULL)
        return 0u;
    if (!ringl_buffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }

    object = ringl_bound_buffer_object(context, target);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return 0u;
    }
    return object->usage;
}

void ringl_buffer_objects_destroy_all(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL)
        return;

    for (index = 0; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_BUFFER ||
            context->objects[index].state == RINGL_OBJECT_FREE) {
            continue;
        }
        ringl_backend_destroy_object(context, context->buffers[index].ringpu_handle);
        context->buffers[index].ringpu_handle = 0u;
        free(context->buffers[index].shadow_bytes);
        context->buffers[index].shadow_bytes = NULL;
    }
}
