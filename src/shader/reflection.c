/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "rsh1_abi.h"

#include <string.h>

#include <ringl/reflection.h>

static RinGLProgramObject* program_object(RinGLContext* context,
                                          uint32_t program)
{
    uint32_t index;

    if (ringl_object_lookup(context, program, RINGL_OBJECT_PROGRAM) == NULL)
        return NULL;
    index = ringl_object_slot_index(program);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->programs[index];
}

static RinGLShaderObject* shader_object(RinGLContext* context,
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

static int shader_header(const uint8_t* rsh1, uint32_t rsh1_size,
                         uint32_t expected_stage,
                         RinGLRsh1HeaderV1* header)
{
    if (rsh1 == NULL || rsh1_size < sizeof(*header)) {
        return 0;
    }
    memcpy(header, rsh1, sizeof(*header));
    return header->magic == RINGL_RSH1_MAGIC &&
           header->version == RINGL_RSH1_VERSION &&
           header->header_size == sizeof(*header) &&
           header->stage == expected_stage &&
           header->total_size == rsh1_size;
}

int ringl_get_program_reflection(uint32_t program,
                                 RinGLProgramReflectionV1* reflection)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLRsh1HeaderV1 vertex_header;
    RinGLRsh1HeaderV1 fragment_header;
    RinGLProgramReflectionV1 result;
    const uint8_t* vertex_rsh1;
    const uint8_t* fragment_rsh1;
    uint32_t vertex_rsh1_size;
    uint32_t fragment_rsh1_size;

    if (context == NULL || reflection == NULL)
        return -1;
    if (reflection->struct_size < sizeof(*reflection) ||
        reflection->api_version != RINGL_API_VERSION) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }

    object = program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    vertex = shader_object(context, object->linked_vertex_shader);
    fragment = shader_object(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL)
        return -1;

    if (object->float_uniform_count == 0u && object->int_uniform_count == 0u &&
        object->vec2_uniform_count == 0u && object->ivec2_uniform_count == 0u &&
        object->vec3_uniform_count == 0u && object->ivec3_uniform_count == 0u &&
        object->vec4_uniform_count == 0u && object->ivec4_uniform_count == 0u &&
        object->mat4_uniform_count == 0u &&
        vertex->rsh1_size == 0u &&
        ringl_lower_shader_rsh1(object->linked_vertex_shader) != 0) {
        return -1;
    }
    if (object->float_uniform_count == 0u && object->int_uniform_count == 0u &&
        object->vec2_uniform_count == 0u && object->ivec2_uniform_count == 0u &&
        object->vec3_uniform_count == 0u && object->ivec3_uniform_count == 0u &&
        object->vec4_uniform_count == 0u && object->ivec4_uniform_count == 0u &&
        object->mat4_uniform_count == 0u &&
        fragment->rsh1_size == 0u &&
        ringl_lower_shader_rsh1(object->linked_fragment_shader) != 0) {
        return -1;
    }

    vertex_rsh1 = object->vertex_uniform_rsh1 != NULL
        ? object->vertex_uniform_rsh1 : vertex->rsh1;
    fragment_rsh1 = object->fragment_uniform_rsh1 != NULL
        ? object->fragment_uniform_rsh1 : fragment->rsh1;
    vertex_rsh1_size = object->vertex_uniform_rsh1 != NULL
        ? object->vertex_uniform_rsh1_size : vertex->rsh1_size;
    fragment_rsh1_size = object->fragment_uniform_rsh1 != NULL
        ? object->fragment_uniform_rsh1_size : fragment->rsh1_size;

    if (!shader_header(vertex_rsh1, vertex_rsh1_size,
                       RINGL_RSH1_STAGE_VERTEX, &vertex_header) ||
        !shader_header(fragment_rsh1, fragment_rsh1_size,
                       RINGL_RSH1_STAGE_FRAGMENT,
                       &fragment_header)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.api_version = RINGL_API_VERSION;
    result.vertex_input_count = vertex_header.input_count;
    result.vertex_output_count = vertex_header.output_count;
    result.fragment_input_count = fragment_header.input_count;
    result.fragment_output_count = fragment_header.output_count;
    result.active_uniform_count = object->sampler_uniform_count +
                                  object->float_uniform_count +
                                  object->int_uniform_count +
                                  object->vec2_uniform_count +
                                  object->vec3_uniform_count +
                                  object->vec4_uniform_count +
                                  object->ivec2_uniform_count +
                                  object->ivec3_uniform_count +
                                  object->ivec4_uniform_count +
                                  object->mat4_uniform_count;
    result.vertex_shader_module = object->vertex_uniform_rsh1 != NULL
        ? object->vertex_uniform_module : vertex->ringpu_module;
    result.fragment_shader_module = object->fragment_uniform_rsh1 != NULL
        ? object->fragment_uniform_module : fragment->ringpu_module;

    *reflection = result;
    return 0;
}
