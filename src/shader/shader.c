/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stdlib.h>
#include <string.h>

#define RINGL_MAX_SHADER_SOURCE_BYTES UINT64_C(16777216)

static int ringl_shader_type_valid(uint32_t type)
{
    return type == RINGL_VERTEX_SHADER || type == RINGL_FRAGMENT_SHADER;
}

static RinGLShaderObject* ringl_shader_object(RinGLContext* context,
                                               uint32_t shader)
{
    uint32_t index;
    if (ringl_object_lookup(context, shader, RINGL_OBJECT_SHADER) == NULL)
        return NULL;
    index = ringl_object_slot_index(shader);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->shaders[index];
}

uint32_t ringl_create_shader(uint32_t shader_type)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot;
    RinGLShaderObject* object;
    uint32_t shader;

    if (context == NULL)
        return 0u;
    if (!ringl_shader_type_valid(shader_type)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }

    shader = ringl_object_allocate(context, RINGL_OBJECT_SHADER);
    if (shader == 0u) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return 0u;
    }

    slot = ringl_object_lookup(context, shader, RINGL_OBJECT_SHADER);
    object = ringl_shader_object(context, shader);
    if (slot == NULL || object == NULL) {
        ringl_object_release(context, shader, RINGL_OBJECT_SHADER);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return 0u;
    }
    object->shader_type = shader_type;
    ringl_object_promote(slot);
    return shader;
}

void ringl_delete_shader(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;

    if (context == NULL || shader == 0u)
        return;
    object = ringl_shader_object(context, shader);
    if (object == NULL)
        return;
    free(object->source);
    memset(object, 0, sizeof(*object));
    ringl_object_release(context, shader, RINGL_OBJECT_SHADER);
}

int ringl_is_shader(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;
    if (context == NULL || shader == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, shader, RINGL_OBJECT_SHADER);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

void ringl_shader_source(uint32_t shader, const char* source, int64_t length)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    uint64_t source_length64;
    size_t source_length;
    char* copy;

    if (context == NULL)
        return;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (source == NULL || length < -1) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    if (length < 0) {
        source_length = strlen(source);
        source_length64 = (uint64_t)source_length;
    } else {
        source_length64 = (uint64_t)length;
        if (source_length64 > RINGL_MAX_SHADER_SOURCE_BYTES) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        source_length = (size_t)source_length64;
        if ((uint64_t)source_length != source_length64) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
    }

    if (source_length64 > RINGL_MAX_SHADER_SOURCE_BYTES) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }

    copy = malloc(source_length + 1u);
    if (copy == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    memcpy(copy, source, source_length);
    copy[source_length] = '\0';

    free(object->source);
    object->source = copy;
    object->source_length = source_length64;
}

uint32_t ringl_get_shader_type(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return 0u;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->shader_type;
}

uint64_t ringl_get_shader_source_length(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return 0u;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->source_length;
}

void ringl_shader_objects_destroy_all(RinGLContext* context)
{
    uint32_t index;
    if (context == NULL)
        return;
    for (index = 0; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_SHADER ||
            context->objects[index].state == RINGL_OBJECT_FREE)
            continue;
        free(context->shaders[index].source);
        context->shaders[index].source = NULL;
        context->shaders[index].source_length = 0u;
    }
}
