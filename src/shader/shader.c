/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_parser.h"

#include <stdlib.h>
#include <string.h>

#define RINGL_MAX_SHADER_SOURCE_BYTES UINT64_C(16777216)

static void ringl_shader_release_storage(RinGLContext* context,
                                          void** bytes, uint64_t owned_size)
{
    if (context != NULL)
        ringl_context_release_shadow_bytes(context, owned_size);
    if (bytes != NULL)
        free(*bytes);
    if (bytes != NULL)
        *bytes = NULL;
}

static int ringl_shader_type_valid(uint32_t type)
{
    return type == RINGL_VERTEX_SHADER || type == RINGL_FRAGMENT_SHADER;
}

static int ringl_shader_precision_type_valid(uint32_t type)
{
    return type == RINGL_LOW_FLOAT || type == RINGL_MEDIUM_FLOAT ||
           type == RINGL_HIGH_FLOAT || type == RINGL_LOW_INT ||
           type == RINGL_MEDIUM_INT || type == RINGL_HIGH_INT;
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

static RinGLShaderObject* ringl_shader_object_for_api(RinGLContext* context,
                                                       uint32_t shader)
{
    RinGLShaderObject* object = ringl_shader_object(context, shader);

    if (object != NULL && object->delete_pending) {
        return NULL;
    }
    return object;
}

static int ringl_shader_is_attached(RinGLContext* context, uint32_t shader)
{
    uint32_t index;

    if (context == NULL || shader == 0u)
        return 0;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_PROGRAM ||
            context->objects[index].state == RINGL_OBJECT_FREE)
            continue;
        if (context->programs[index].vertex_shader == shader ||
            context->programs[index].fragment_shader == shader ||
            context->programs[index].linked_vertex_shader == shader ||
            context->programs[index].linked_fragment_shader == shader)
            return 1;
    }
    return 0;
}

static void ringl_shader_discard_artifacts(RinGLContext* context,
                                           RinGLShaderObject* object)
{
    if (object->ringpu_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_backend_destroy_object(context, object->ringpu_module);
        object->ringpu_module = 0u;
    }
    ringl_shader_release_storage(context, (void**)&object->rsh1,
                                 object->rsh1_size);
    object->rsh1_size = 0u;
    object->rsh1_sampler_binding_count = 0u;
    memset(object->rsh1_sampler_binding_indices, 0,
           sizeof(object->rsh1_sampler_binding_indices));
}

static void ringl_shader_destroy(RinGLContext* context, uint32_t shader)
{
    RinGLShaderObject* object;

    object = ringl_shader_object(context, shader);
    if (object == NULL)
        return;
    if (object->source != NULL) {
        uint64_t source_size = object->source_length + 1u;
        ringl_shader_release_storage(context, (void**)&object->source,
                                     source_size);
    }
    ringl_shader_discard_artifacts(context, object);
    memset(object, 0, sizeof(*object));
    ringl_object_release(context, shader, RINGL_OBJECT_SHADER);
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
    object->float_uniform_count = 0u;
    object->int_uniform_count = 0u;
    object->bool_uniform_count = 0u;
    object->bvec2_uniform_count = 0u;
    object->bvec3_uniform_count = 0u;
    object->bvec4_uniform_count = 0u;
    object->vec2_uniform_count = 0u;
    object->vec3_uniform_count = 0u;
    object->vec4_uniform_count = 0u;
    object->ivec2_uniform_count = 0u;
    object->ivec3_uniform_count = 0u;
    object->ivec4_uniform_count = 0u;
    object->mat2_uniform_count = 0u;
    object->mat3_uniform_count = 0u;
    object->mat4_uniform_count = 0u;
    object->rsh1_sampler_binding_count = 0u;
    object->uses_standard_derivatives = 0u;
    object->uses_shader_texture_lod = 0u;
    object->uses_webgl_frag_depth = 0u;
    object->uses_webgl_draw_buffers = 0u;
    memset(object->sampler_uniform_names, 0,
           sizeof(object->sampler_uniform_names));
    memset(object->float_uniform_names, 0,
           sizeof(object->float_uniform_names));
    memset(object->int_uniform_names, 0, sizeof(object->int_uniform_names));
    memset(object->bool_uniform_names, 0, sizeof(object->bool_uniform_names));
    memset(object->bvec2_uniform_names, 0, sizeof(object->bvec2_uniform_names));
    memset(object->bvec3_uniform_names, 0, sizeof(object->bvec3_uniform_names));
    memset(object->bvec4_uniform_names, 0, sizeof(object->bvec4_uniform_names));
    memset(object->vec2_uniform_names, 0,
           sizeof(object->vec2_uniform_names));
    memset(object->vec3_uniform_names, 0,
           sizeof(object->vec3_uniform_names));
    memset(object->vec4_uniform_names, 0,
           sizeof(object->vec4_uniform_names));
    memset(object->ivec2_uniform_names, 0, sizeof(object->ivec2_uniform_names));
    memset(object->ivec3_uniform_names, 0, sizeof(object->ivec3_uniform_names));
    memset(object->ivec4_uniform_names, 0, sizeof(object->ivec4_uniform_names));
    memset(object->mat2_uniform_names, 0, sizeof(object->mat2_uniform_names));
    memset(object->mat3_uniform_names, 0, sizeof(object->mat3_uniform_names));
    memset(object->mat4_uniform_names, 0,
           sizeof(object->mat4_uniform_names));
    memset(object->rsh1_sampler_binding_indices, 0,
           sizeof(object->rsh1_sampler_binding_indices));
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
    object->delete_pending = RINGL_TRUE;
    ringl_shader_release_if_delete_pending(context, shader);
}

int ringl_is_shader(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;
    if (context == NULL || shader == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, shader, RINGL_OBJECT_SHADER);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE &&
           !context->shaders[ringl_object_slot_index(shader)].delete_pending;
}

int ringl_shader_is_delete_pending(RinGLContext* context, uint32_t shader)
{
    RinGLShaderObject* object = ringl_shader_object(context, shader);
    return object != NULL && object->delete_pending;
}

void ringl_shader_release_if_delete_pending(RinGLContext* context,
                                            uint32_t shader)
{
    RinGLShaderObject* object = ringl_shader_object(context, shader);

    if (object == NULL || !object->delete_pending ||
        ringl_shader_is_attached(context, shader))
        return;
    ringl_shader_destroy(context, shader);
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
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (source == NULL || length < -1) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    if (length < 0) {
        source_length = 0u;
        while (source_length <= RINGL_MAX_SHADER_SOURCE_BYTES &&
               source[source_length] != '\0') {
            ++source_length;
        }
        if (source_length > RINGL_MAX_SHADER_SOURCE_BYTES) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
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

    if (!ringl_context_reserve_shadow_bytes(context, source_length64 + 1u)) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    copy = malloc(source_length + 1u);
    if (copy == NULL) {
        ringl_context_release_shadow_bytes(context, source_length64 + 1u);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    memcpy(copy, source, source_length);
    copy[source_length] = '\0';

    if (object->source != NULL) {
        uint64_t old_source_size = object->source_length + 1u;
        ringl_shader_release_storage(context, (void**)&object->source,
                                     old_source_size);
    }
    object->source = copy;
    object->source_length = source_length64;
    ringl_shader_reset_compile_state(context, object);
}

void ringl_compile_shader(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    RinGLGlslParseResult* result;
    int parse_result;

    if (context == NULL)
        return;
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    ringl_shader_reset_compile_state(context, object);
    if (object->source == NULL) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "no shader source");
        return;
    }

    /* Parsing is a bounded, caller-independent workspace just like source
     * snapshots and lowered modules. Charge the complete result before the
     * parser touches source so an exhausted context cannot build an
     * unaccounted stack-sized diagnostic/reflection snapshot. */
    result = ringl_context_alloc_temporary(context, sizeof(*result));
    if (result == NULL) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "shader parser workspace exhausted");
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    parse_result = ringl_glsl_parse(object->shader_type, object->source,
                                    (size_t)object->source_length, result);
    if (parse_result != 0 || !result->ok) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            result->diagnostic);
        ringl_context_free_temporary(context, result, sizeof(*result));
        return;
    }
    if (result->uses_standard_derivatives != 0u &&
        context->webgl_standard_derivatives_enabled == RINGL_FALSE) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "GL_OES_standard_derivatives is not enabled");
        ringl_context_free_temporary(context, result, sizeof(*result));
        return;
    }
    if (result->uses_shader_texture_lod != 0u &&
        context->webgl_shader_texture_lod_enabled == RINGL_FALSE) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "GL_EXT_shader_texture_lod is not enabled");
        ringl_context_free_temporary(context, result, sizeof(*result));
        return;
    }
    if (result->uses_webgl_frag_depth != 0u &&
        context->webgl_frag_depth_enabled == RINGL_FALSE) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "GL_EXT_frag_depth is not enabled");
        ringl_context_free_temporary(context, result, sizeof(*result));
        return;
    }
    if (result->uses_webgl_draw_buffers != 0u &&
        context->webgl_draw_buffers_enabled == RINGL_FALSE) {
        ringl_copy_c_string(object->info_log, sizeof(object->info_log),
                            "GL_EXT_draw_buffers is not enabled");
        ringl_context_free_temporary(context, result, sizeof(*result));
        return;
    }

    object->compile_status = RINGL_TRUE;
    object->declaration_count = result->declaration_count;
    object->statement_count = result->statement_count;
    object->attribute_count = result->attribute_count;
    object->sampler_uniform_count = result->sampler_uniform_count;
    memcpy(object->sampler_uniform_names, result->sampler_uniform_names,
           sizeof(object->sampler_uniform_names));
    object->float_uniform_count = result->float_uniform_count;
    memcpy(object->float_uniform_names, result->float_uniform_names,
           sizeof(object->float_uniform_names));
    object->int_uniform_count = result->int_uniform_count;
    memcpy(object->int_uniform_names, result->int_uniform_names,
           sizeof(object->int_uniform_names));
    object->bool_uniform_count = result->bool_uniform_count;
    memcpy(object->bool_uniform_names, result->bool_uniform_names,
           sizeof(object->bool_uniform_names));
    object->bvec2_uniform_count = result->bvec2_uniform_count;
    memcpy(object->bvec2_uniform_names, result->bvec2_uniform_names,
           sizeof(object->bvec2_uniform_names));
    object->bvec3_uniform_count = result->bvec3_uniform_count;
    memcpy(object->bvec3_uniform_names, result->bvec3_uniform_names,
           sizeof(object->bvec3_uniform_names));
    object->bvec4_uniform_count = result->bvec4_uniform_count;
    memcpy(object->bvec4_uniform_names, result->bvec4_uniform_names,
           sizeof(object->bvec4_uniform_names));
    object->vec2_uniform_count = result->vec2_uniform_count;
    memcpy(object->vec2_uniform_names, result->vec2_uniform_names,
           sizeof(object->vec2_uniform_names));
    object->vec3_uniform_count = result->vec3_uniform_count;
    memcpy(object->vec3_uniform_names, result->vec3_uniform_names,
           sizeof(object->vec3_uniform_names));
    object->vec4_uniform_count = result->vec4_uniform_count;
    memcpy(object->vec4_uniform_names, result->vec4_uniform_names,
           sizeof(object->vec4_uniform_names));
    object->ivec2_uniform_count = result->ivec2_uniform_count;
    memcpy(object->ivec2_uniform_names, result->ivec2_uniform_names,
           sizeof(object->ivec2_uniform_names));
    object->ivec3_uniform_count = result->ivec3_uniform_count;
    memcpy(object->ivec3_uniform_names, result->ivec3_uniform_names,
           sizeof(object->ivec3_uniform_names));
    object->ivec4_uniform_count = result->ivec4_uniform_count;
    memcpy(object->ivec4_uniform_names, result->ivec4_uniform_names,
           sizeof(object->ivec4_uniform_names));
    object->mat2_uniform_count = result->mat2_uniform_count;
    memcpy(object->mat2_uniform_names, result->mat2_uniform_names,
           sizeof(object->mat2_uniform_names));
    object->mat3_uniform_count = result->mat3_uniform_count;
    memcpy(object->mat3_uniform_names, result->mat3_uniform_names,
           sizeof(object->mat3_uniform_names));
    object->mat4_uniform_count = result->mat4_uniform_count;
    memcpy(object->mat4_uniform_names, result->mat4_uniform_names,
           sizeof(object->mat4_uniform_names));
    object->uses_standard_derivatives = result->uses_standard_derivatives;
    object->uses_shader_texture_lod = result->uses_shader_texture_lod;
    object->uses_webgl_frag_depth = result->uses_webgl_frag_depth;
    object->uses_webgl_draw_buffers = result->uses_webgl_draw_buffers;
    ringl_context_free_temporary(context, result, sizeof(*result));
}

uint32_t ringl_get_shader_compile_status(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return RINGL_FALSE;
    object = ringl_shader_object_for_api(context, shader);
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
    object = ringl_shader_object_for_api(context, shader);
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
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->shader_type;
}

int ringl_get_shader_parameteriv_bounded(uint32_t shader, uint32_t pname,
                                         int32_t* value, size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    int32_t result;

    if (context == NULL)
        return -1;
    if (value == NULL || value_count < 1u) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (pname != RINGL_COMPILE_STATUS && pname != RINGL_SHADER_TYPE &&
        pname != RINGL_INFO_LOG_LENGTH &&
        pname != RINGL_SHADER_SOURCE_LENGTH) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (pname == RINGL_COMPILE_STATUS) {
        result = (int32_t)object->compile_status;
    } else if (pname == RINGL_SHADER_TYPE) {
        result = (int32_t)object->shader_type;
    } else if (pname == RINGL_INFO_LOG_LENGTH) {
        size_t length = strlen(object->info_log);

        if (length >= (size_t)INT32_MAX) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        result = (int32_t)(length + 1u);
    } else {
        if (object->source_length >= (uint64_t)INT32_MAX) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        /* GLES source length includes the terminating NUL. */
        result = (int32_t)(object->source_length + 1u);
    }
    *value = result;
    return 0;
}

int ringl_get_shader_precision_format(
    uint32_t shader_type, uint32_t precision_type,
    RinGLShaderPrecisionFormatV1* format)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderPrecisionFormatV1 result;

    if (context == NULL)
        return -1;
    if (format == NULL || format->struct_size < sizeof(*format) ||
        format->api_version != RINGL_API_VERSION || format->reserved0 != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!ringl_shader_type_valid(shader_type) ||
        !ringl_shader_precision_type_valid(precision_type)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }

    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.api_version = RINGL_API_VERSION;
    if (precision_type == RINGL_LOW_FLOAT ||
        precision_type == RINGL_MEDIUM_FLOAT ||
        precision_type == RINGL_HIGH_FLOAT) {
        /* RSH1's only floating execution domain is binary32. */
        result.range_min = -126;
        result.range_max = 127;
        result.precision = 23;
    } else {
        /* RSH1 integer values are signed two's-complement i32. GLES reports
         * the integer exponent range as [31, 30] with zero fractional bits. */
        result.range_min = 31;
        result.range_max = 30;
        result.precision = 0;
    }
    *format = result;
    return 0;
}

uint64_t ringl_get_shader_source_length(uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    if (context == NULL)
        return 0u;
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return object->source_length;
}

uint64_t ringl_copy_shader_source(uint32_t shader, char* buffer,
                                  uint64_t buffer_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLShaderObject* object;
    uint64_t copy_length;

    if (context == NULL)
        return 0u;
    object = ringl_shader_object_for_api(context, shader);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    if (buffer == NULL || buffer_size == 0u)
        return object->source_length;

    copy_length = object->source_length;
    if (copy_length >= buffer_size)
        copy_length = buffer_size - 1u;
    if (copy_length != 0u)
        memcpy(buffer, object->source, (size_t)copy_length);
    buffer[copy_length] = '\0';
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
        if (context->shaders[index].source != NULL) {
            uint64_t source_size = context->shaders[index].source_length + 1u;
            ringl_shader_release_storage(context,
                                         (void**)&context->shaders[index].source,
                                         source_size);
        }
        ringl_shader_release_storage(context,
                                     (void**)&context->shaders[index].rsh1,
                                     context->shaders[index].rsh1_size);
        context->shaders[index].rsh1_size = 0u;
        if (context->shaders[index].ringpu_module != 0u)
            ringl_backend_destroy_object(context,
                                         context->shaders[index].ringpu_module);
        memset(&context->shaders[index], 0, sizeof(context->shaders[index]));
    }
}
