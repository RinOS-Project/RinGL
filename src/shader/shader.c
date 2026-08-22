/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_parser.h"

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

static void ringl_shader_discard_artifacts(RinGLContext* context,
                                           RinGLShaderObject* object)
{
    if (object->ringpu_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_backend_destroy_object(context, object->ringpu_module);
        object->ringpu_module = 0u;
    }
    free(object->rsh1);
    object->rsh1 = NULL;
    object->rsh1_size = 0u;
}

static void ringl_shader_reset_compile_state(RinGLContext* context,
                                             RinGLShaderObject* object)
{
    ringl_shader_discard_artifacts(context, object);
    object->compile_status = RINGL_FALSE;
    object->declaration_count = 0u;
    object->statement_count = 0u;
    object->attribute_count = 0u;
    object->sampler_uniform_count = 0u;
    memset(object->sampler_uniform_names, 0,
           sizeof(object->sampler_uniform_names));
    object->info_log[0] = '\0';
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
    ringl_shader_reset_compile_state(context, object);
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
    ringl_shader_discard_artifacts(context, object);
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
    ringl_shader_reset_compile_state(context, object);
}

void ringl_compile_shader(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    RinGLGlslParseResult result;
    int parse_result;

    if (context == NULL)
        return;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    ringl_shader_reset_compile_state(context, object);
    if (object->source == NULL) {
        (void)strncpy(object->info_log, "no shader source", sizeof(object->info_log) - 1u);
        object->info_log[sizeof(object->info_log) - 1u] = '\0';
        return;
    }

    parse_result = ringl_glsl_parse(object->shader_type, object->source,
                                    (size_t)object->source_length, &result);
    if (parse_result != 0 || !result.ok) {
        (void)strncpy(object->info_log, result.diagnostic,
                      sizeof(object->info_log) - 1u);
        object->info_log[sizeof(object->info_log) - 1u] = '\0';
        return;
    }

    object->compile_status = RINGL_TRUE;
    object->declaration_count = result.declaration_count;
    object->statement_count = result.statement_count;
    object->attribute_count = result.attribute_count;
    object->sampler_uniform_count = result.sampler_uniform_count;
    memcpy(object->sampler_uniform_names, result.sampler_uniform_names,
           sizeof(object->sampler_uniform_names));
}

uint32_t ringl_get_shader_compile_status(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return RINGL_FALSE;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return RINGL_FALSE;
    }
    return object->compile_status;
}

uint64_t ringl_get_shader_info_log(uint32_t shader, char* buffer,
                                   uint64_t buffer_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    size_t length;
    size_t copy_length;

    if (context == NULL)
        return 0u;
    object = ringl_shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    length = strlen(object->info_log);
    if (buffer == NULL || buffer_size == 0u)
        return (uint64_t)length;
    copy_length = length;
    if (copy_length >= buffer_size)
        copy_length = (size_t)buffer_size - 1u;
    memcpy(buffer, object->info_log, copy_length);
    buffer[copy_length] = '\0';
    return (uint64_t)length;
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
        free(context->shaders[index].rsh1);
        if (context->shaders[index].ringpu_module != 0u)
            ringl_backend_destroy_object(context,
                                         context->shaders[index].ringpu_module);
        memset(&context->shaders[index], 0, sizeof(context->shaders[index]));
    }
}
