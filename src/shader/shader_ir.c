/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_lower.h"
#include "texture_lower.h"
#include "varying_lower.h"

#include <stdlib.h>
#include <string.h>

static RinGLShaderObject* shader_object(RinGLContext* context, uint32_t shader)
{
    uint32_t index;
    if (ringl_object_lookup(context, shader, RINGL_OBJECT_SHADER) == NULL)
        return NULL;
    index = ringl_object_slot_index(shader);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->shaders[index];
}

int ringl_lower_shader_rsh1(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    RinGLGlslLowerResult lowered;
    uint8_t* copy;
    int rc;

    if (context == NULL)
        return -1;
    object = shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->compile_status || object->source == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    if (strstr(object->source, "varying") != NULL) {
        rc = ringl_glsl_lower_varying_rsh1(
            object->shader_type, object->source,
            (size_t)object->source_length, &lowered);
    } else if (object->sampler_uniform_count != 0u &&
               strstr(object->source, "texture2D") != NULL) {
        if (object->shader_type != RINGL_FRAGMENT_SHADER) {
            ringl_copy_c_string(
                object->info_log, sizeof(object->info_log),
                "texture2D lowering requires a fragment shader");
            return -1;
        }
        rc = ringl_glsl_lower_texture2d_rsh1(
            object->source, (size_t)object->source_length,
            &object->sampler_uniform_names[0][0],
            sizeof(object->sampler_uniform_names[0]),
            object->sampler_uniform_count,
            &lowered);
    } else {
        rc = ringl_glsl_lower_rsh1(object->shader_type, object->source,
                                   (size_t)object->source_length, &lowered);
    }
    if (rc != 0 || !lowered.ok) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            lowered.diagnostic);
        return -1;
    }

    copy = malloc(lowered.byte_size);
    if (copy == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return -1;
    }
    memcpy(copy, lowered.bytes, lowered.byte_size);

    if (object->ringpu_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_backend_destroy_object(context, object->ringpu_module);
        object->ringpu_module = 0u;
    }
    free(object->rsh1);
    object->rsh1 = copy;
    object->rsh1_size = lowered.byte_size;
    object->info_log[0] = '\0';
    return 0;
}

uint32_t ringl_get_shader_rsh1_size(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return 0u;
    object = shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->rsh1_size;
}

uint32_t ringl_copy_shader_rsh1(uint32_t shader, void* output, uint32_t capacity)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return 0u;
    object = shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    if (object->rsh1_size == 0u)
        return 0u;
    if (output == NULL || capacity < object->rsh1_size) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return object->rsh1_size;
    }
    memcpy(output, object->rsh1, object->rsh1_size);
    return object->rsh1_size;
}

int ringl_realize_shader_module(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    uint64_t module = 0u;
    int rc;

    if (context == NULL)
        return -1;
    object = shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (object->rsh1 == NULL || object->rsh1_size == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    rc = ringl_backend_create_shader_module(context, object->rsh1,
                                            object->rsh1_size, &module);
    if (rc != 0 || module == 0u) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "RinGPU rejected shader module");
        return -1;
    }

    if (object->ringpu_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_backend_destroy_object(context, object->ringpu_module);
    }
    object->ringpu_module = module;
    object->info_log[0] = '\0';
    return 0;
}

uint64_t ringl_get_shader_module(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;

    if (context == NULL)
        return 0u;
    object = shader_object(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->ringpu_module;
}
