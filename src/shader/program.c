/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_parser.h"
#include "glsl_lower.h"
#include "varying_lower.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static RinGLProgramObject* ringl_program_object(RinGLContext* context,
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

static RinGLShaderObject* ringl_program_shader(RinGLContext* context,
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

static void ringl_program_set_log(RinGLProgramObject* program,
                                  const char* message)
{
    size_t length;
    if (program == NULL)
        return;
    program->info_log[0] = '\0';
    if (message == NULL)
        return;
    length = strlen(message);
    if (length >= sizeof(program->info_log))
        length = sizeof(program->info_log) - 1u;
    memcpy(program->info_log, message, length);
    program->info_log[length] = '\0';
}

static void ringl_program_discard_uniform_artifacts(
    RinGLContext* context, RinGLProgramObject* program)
{
    if (program == NULL)
        return;
    if (program->vertex_uniform_module != 0u)
        ringl_backend_destroy_object(context, program->vertex_uniform_module);
    if (program->fragment_uniform_module != 0u)
        ringl_backend_destroy_object(context, program->fragment_uniform_module);
    free(program->vertex_uniform_rsh1);
    free(program->fragment_uniform_rsh1);
    program->vertex_uniform_rsh1 = NULL;
    program->fragment_uniform_rsh1 = NULL;
    program->vertex_uniform_module = 0u;
    program->fragment_uniform_module = 0u;
    program->vertex_uniform_rsh1_size = 0u;
    program->fragment_uniform_rsh1_size = 0u;
}

static int ringl_program_add_sampler_uniform(RinGLProgramObject* program,
                                             const char* name)
{
    uint32_t i;
    for (i = 0u; i < program->sampler_uniform_count; ++i) {
        if (strcmp(program->sampler_uniforms[i].name, name) == 0)
            return 1;
    }
    if (program->sampler_uniform_count >= RINGL_MAX_SAMPLER_UNIFORMS)
        return 0;
    i = program->sampler_uniform_count++;
    ringl_copy_c_string(program->sampler_uniforms[i].name,
                        sizeof(program->sampler_uniforms[i].name), name);
    program->sampler_uniforms[i].texture_unit = 0;
    return 1;
}

static int ringl_program_collect_sampler_uniforms(RinGLProgramObject* program,
                                                  const RinGLShaderObject* vertex,
                                                  const RinGLShaderObject* fragment)
{
    uint32_t i;
    program->sampler_uniform_count = 0u;
    memset(program->sampler_uniforms, 0, sizeof(program->sampler_uniforms));
    for (i = 0u; i < vertex->sampler_uniform_count; ++i) {
        if (!ringl_program_add_sampler_uniform(program,
                                               vertex->sampler_uniform_names[i]))
            return 0;
    }
    for (i = 0u; i < fragment->sampler_uniform_count; ++i) {
        if (!ringl_program_add_sampler_uniform(program,
                                               fragment->sampler_uniform_names[i]))
            return 0;
    }
    return 1;
}

static int ringl_program_add_float_uniform(RinGLProgramObject* program,
                                           const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->sampler_uniform_count; ++index) {
        if (strcmp(program->sampler_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->float_uniform_count; ++index) {
        if (strcmp(program->float_uniforms[index].name, name) == 0)
            return 1;
    }
    for (index = 0u; index < program->vec2_uniform_count; ++index) {
        if (strcmp(program->vec2_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->vec3_uniform_count; ++index) {
        if (strcmp(program->vec3_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->vec4_uniform_count; ++index) {
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->mat4_uniform_count; ++index) {
        if (strcmp(program->mat4_uniforms[index].name, name) == 0)
            return 0;
    }
    if (program->float_uniform_count >= RINGL_MAX_FLOAT_UNIFORMS)
        return 0;
    index = program->float_uniform_count++;
    ringl_copy_c_string(program->float_uniforms[index].name,
                        sizeof(program->float_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_float_uniforms(RinGLProgramObject* program,
                                                const RinGLShaderObject* vertex,
                                                const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->float_uniform_count = 0u;
    memset(program->float_uniforms, 0, sizeof(program->float_uniforms));
    for (index = 0u; index < vertex->float_uniform_count; ++index) {
        if (!ringl_program_add_float_uniform(program,
                                              vertex->float_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->float_uniform_count; ++index) {
        if (!ringl_program_add_float_uniform(program,
                                              fragment->float_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_add_vec2_uniform(RinGLProgramObject* program,
                                          const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL ||
        program->vec2_uniform_count >= RINGL_MAX_VEC2_UNIFORMS)
        return 0;
    for (index = 0u; index < program->sampler_uniform_count; ++index)
        if (strcmp(program->sampler_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->float_uniform_count; ++index)
        if (strcmp(program->float_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec2_uniform_count; ++index)
        if (strcmp(program->vec2_uniforms[index].name, name) == 0)
            return 1;
    for (index = 0u; index < program->vec3_uniform_count; ++index)
        if (strcmp(program->vec3_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec4_uniform_count; ++index)
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->mat4_uniform_count; ++index)
        if (strcmp(program->mat4_uniforms[index].name, name) == 0)
            return 0;
    index = program->vec2_uniform_count++;
    ringl_copy_c_string(program->vec2_uniforms[index].name,
                        sizeof(program->vec2_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_vec2_uniforms(RinGLProgramObject* program,
                                               const RinGLShaderObject* vertex,
                                               const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->vec2_uniform_count = 0u;
    memset(program->vec2_uniforms, 0, sizeof(program->vec2_uniforms));
    for (index = 0u; index < vertex->vec2_uniform_count; ++index)
        if (!ringl_program_add_vec2_uniform(program,
                                             vertex->vec2_uniform_names[index]))
            return 0;
    for (index = 0u; index < fragment->vec2_uniform_count; ++index)
        if (!ringl_program_add_vec2_uniform(program,
                                             fragment->vec2_uniform_names[index]))
            return 0;
    return 1;
}

static int ringl_program_add_vec3_uniform(RinGLProgramObject* program,
                                          const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL ||
        program->vec3_uniform_count >= RINGL_MAX_VEC3_UNIFORMS)
        return 0;
    for (index = 0u; index < program->sampler_uniform_count; ++index)
        if (strcmp(program->sampler_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->float_uniform_count; ++index)
        if (strcmp(program->float_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec2_uniform_count; ++index)
        if (strcmp(program->vec2_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec3_uniform_count; ++index)
        if (strcmp(program->vec3_uniforms[index].name, name) == 0)
            return 1;
    for (index = 0u; index < program->vec4_uniform_count; ++index)
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->mat4_uniform_count; ++index)
        if (strcmp(program->mat4_uniforms[index].name, name) == 0)
            return 0;
    index = program->vec3_uniform_count++;
    ringl_copy_c_string(program->vec3_uniforms[index].name,
                        sizeof(program->vec3_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_vec3_uniforms(RinGLProgramObject* program,
                                               const RinGLShaderObject* vertex,
                                               const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->vec3_uniform_count = 0u;
    memset(program->vec3_uniforms, 0, sizeof(program->vec3_uniforms));
    for (index = 0u; index < vertex->vec3_uniform_count; ++index)
        if (!ringl_program_add_vec3_uniform(program,
                                             vertex->vec3_uniform_names[index]))
            return 0;
    for (index = 0u; index < fragment->vec3_uniform_count; ++index)
        if (!ringl_program_add_vec3_uniform(program,
                                             fragment->vec3_uniform_names[index]))
            return 0;
    return 1;
}

static int ringl_program_add_vec4_uniform(RinGLProgramObject* program,
                                          const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->sampler_uniform_count; ++index) {
        if (strcmp(program->sampler_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->float_uniform_count; ++index) {
        if (strcmp(program->float_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->vec2_uniform_count; ++index) {
        if (strcmp(program->vec2_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->vec3_uniform_count; ++index) {
        if (strcmp(program->vec3_uniforms[index].name, name) == 0)
            return 0;
    }
    for (index = 0u; index < program->vec4_uniform_count; ++index) {
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 1;
    }
    for (index = 0u; index < program->mat4_uniform_count; ++index) {
        if (strcmp(program->mat4_uniforms[index].name, name) == 0)
            return 0;
    }
    if (program->vec4_uniform_count >= RINGL_MAX_VEC4_UNIFORMS)
        return 0;
    index = program->vec4_uniform_count++;
    ringl_copy_c_string(program->vec4_uniforms[index].name,
                        sizeof(program->vec4_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_vec4_uniforms(RinGLProgramObject* program,
                                               const RinGLShaderObject* vertex,
                                               const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->vec4_uniform_count = 0u;
    memset(program->vec4_uniforms, 0, sizeof(program->vec4_uniforms));
    for (index = 0u; index < vertex->vec4_uniform_count; ++index) {
        if (!ringl_program_add_vec4_uniform(program,
                                             vertex->vec4_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->vec4_uniform_count; ++index) {
        if (!ringl_program_add_vec4_uniform(program,
                                             fragment->vec4_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_add_mat4_uniform(RinGLProgramObject* program,
                                          const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL ||
        program->mat4_uniform_count >= RINGL_MAX_MAT4_UNIFORMS)
        return 0;
    for (index = 0u; index < program->sampler_uniform_count; ++index)
        if (strcmp(program->sampler_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->float_uniform_count; ++index)
        if (strcmp(program->float_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec2_uniform_count; ++index)
        if (strcmp(program->vec2_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec3_uniform_count; ++index)
        if (strcmp(program->vec3_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->vec4_uniform_count; ++index)
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 0;
    for (index = 0u; index < program->mat4_uniform_count; ++index)
        if (strcmp(program->mat4_uniforms[index].name, name) == 0)
            return 1;
    index = program->mat4_uniform_count++;
    ringl_copy_c_string(program->mat4_uniforms[index].name,
                        sizeof(program->mat4_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_mat4_uniforms(RinGLProgramObject* program,
                                               const RinGLShaderObject* vertex,
                                               const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->mat4_uniform_count = 0u;
    memset(program->mat4_uniforms, 0, sizeof(program->mat4_uniforms));
    for (index = 0u; index < vertex->mat4_uniform_count; ++index)
        if (!ringl_program_add_mat4_uniform(program,
                                             vertex->mat4_uniform_names[index]))
            return 0;
    for (index = 0u; index < fragment->mat4_uniform_count; ++index)
        if (!ringl_program_add_mat4_uniform(program,
                                             fragment->mat4_uniform_names[index]))
            return 0;
    return 1;
}

static int ringl_uniform_name_in_set(const void* entries, size_t entry_size,
                                    uint32_t count, const char* name)
{
    uint32_t index;
    const char* bytes = entries;

    for (index = 0u; index < count; ++index) {
        if (strcmp(bytes + (size_t)index * entry_size, name) == 0)
            return 1;
    }
    return 0;
}

static int ringl_program_uniform_name_used(const RinGLProgramObject* program,
                                           const char* name)
{
    if (program == NULL || name == NULL)
        return 0;
    return ringl_uniform_name_in_set(program->sampler_uniforms,
                                     sizeof(program->sampler_uniforms[0]),
                                     program->sampler_uniform_count, name) ||
           ringl_uniform_name_in_set(program->float_uniforms,
                                     sizeof(program->float_uniforms[0]),
                                     program->float_uniform_count, name) ||
           ringl_uniform_name_in_set(program->vec2_uniforms,
                                     sizeof(program->vec2_uniforms[0]),
                                     program->vec2_uniform_count, name) ||
           ringl_uniform_name_in_set(program->vec3_uniforms,
                                     sizeof(program->vec3_uniforms[0]),
                                     program->vec3_uniform_count, name) ||
           ringl_uniform_name_in_set(program->vec4_uniforms,
                                     sizeof(program->vec4_uniforms[0]),
                                     program->vec4_uniform_count, name) ||
           ringl_uniform_name_in_set(program->mat4_uniforms,
                                     sizeof(program->mat4_uniforms[0]),
                                     program->mat4_uniform_count, name) ||
           ringl_uniform_name_in_set(program->int_uniforms,
                                     sizeof(program->int_uniforms[0]),
                                     program->int_uniform_count, name) ||
           ringl_uniform_name_in_set(program->ivec2_uniforms,
                                     sizeof(program->ivec2_uniforms[0]),
                                     program->ivec2_uniform_count, name) ||
           ringl_uniform_name_in_set(program->ivec3_uniforms,
                                     sizeof(program->ivec3_uniforms[0]),
                                     program->ivec3_uniform_count, name) ||
           ringl_uniform_name_in_set(program->ivec4_uniforms,
                                     sizeof(program->ivec4_uniforms[0]),
                                     program->ivec4_uniform_count, name);
}

static int ringl_program_add_int_uniform(RinGLProgramObject* program,
                                         const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->int_uniform_count; ++index) {
        if (strcmp(program->int_uniforms[index].name, name) == 0)
            return 1;
    }
    if (program->int_uniform_count >= RINGL_MAX_INT_UNIFORMS ||
        ringl_program_uniform_name_used(program, name))
        return 0;
    index = program->int_uniform_count++;
    ringl_copy_c_string(program->int_uniforms[index].name,
                        sizeof(program->int_uniforms[index].name), name);
    return 1;
}

static int ringl_program_add_ivec2_uniform(RinGLProgramObject* program,
                                           const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->ivec2_uniform_count; ++index) {
        if (strcmp(program->ivec2_uniforms[index].name, name) == 0)
            return 1;
    }
    if (program->ivec2_uniform_count >= RINGL_MAX_IVEC2_UNIFORMS ||
        ringl_program_uniform_name_used(program, name))
        return 0;
    index = program->ivec2_uniform_count++;
    ringl_copy_c_string(program->ivec2_uniforms[index].name,
                        sizeof(program->ivec2_uniforms[index].name), name);
    return 1;
}

static int ringl_program_add_ivec3_uniform(RinGLProgramObject* program,
                                           const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->ivec3_uniform_count; ++index) {
        if (strcmp(program->ivec3_uniforms[index].name, name) == 0)
            return 1;
    }
    if (program->ivec3_uniform_count >= RINGL_MAX_IVEC3_UNIFORMS ||
        ringl_program_uniform_name_used(program, name))
        return 0;
    index = program->ivec3_uniform_count++;
    ringl_copy_c_string(program->ivec3_uniforms[index].name,
                        sizeof(program->ivec3_uniforms[index].name), name);
    return 1;
}

static int ringl_program_add_ivec4_uniform(RinGLProgramObject* program,
                                           const char* name)
{
    uint32_t index;

    if (program == NULL || name == NULL)
        return 0;
    for (index = 0u; index < program->ivec4_uniform_count; ++index) {
        if (strcmp(program->ivec4_uniforms[index].name, name) == 0)
            return 1;
    }
    if (program->ivec4_uniform_count >= RINGL_MAX_IVEC4_UNIFORMS ||
        ringl_program_uniform_name_used(program, name))
        return 0;
    index = program->ivec4_uniform_count++;
    ringl_copy_c_string(program->ivec4_uniforms[index].name,
                        sizeof(program->ivec4_uniforms[index].name), name);
    return 1;
}

static int ringl_program_collect_int_uniforms(RinGLProgramObject* program,
                                              const RinGLShaderObject* vertex,
                                              const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->int_uniform_count = 0u;
    memset(program->int_uniforms, 0, sizeof(program->int_uniforms));
    for (index = 0u; index < vertex->int_uniform_count; ++index) {
        if (!ringl_program_add_int_uniform(program,
                                           vertex->int_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->int_uniform_count; ++index) {
        if (!ringl_program_add_int_uniform(program,
                                           fragment->int_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_collect_ivec2_uniforms(RinGLProgramObject* program,
                                                const RinGLShaderObject* vertex,
                                                const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->ivec2_uniform_count = 0u;
    memset(program->ivec2_uniforms, 0, sizeof(program->ivec2_uniforms));
    for (index = 0u; index < vertex->ivec2_uniform_count; ++index) {
        if (!ringl_program_add_ivec2_uniform(program,
                                             vertex->ivec2_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->ivec2_uniform_count; ++index) {
        if (!ringl_program_add_ivec2_uniform(program,
                                             fragment->ivec2_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_collect_ivec3_uniforms(RinGLProgramObject* program,
                                                const RinGLShaderObject* vertex,
                                                const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->ivec3_uniform_count = 0u;
    memset(program->ivec3_uniforms, 0, sizeof(program->ivec3_uniforms));
    for (index = 0u; index < vertex->ivec3_uniform_count; ++index) {
        if (!ringl_program_add_ivec3_uniform(program,
                                             vertex->ivec3_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->ivec3_uniform_count; ++index) {
        if (!ringl_program_add_ivec3_uniform(program,
                                             fragment->ivec3_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_collect_ivec4_uniforms(RinGLProgramObject* program,
                                                const RinGLShaderObject* vertex,
                                                const RinGLShaderObject* fragment)
{
    uint32_t index;

    if (program == NULL || vertex == NULL || fragment == NULL)
        return 0;
    program->ivec4_uniform_count = 0u;
    memset(program->ivec4_uniforms, 0, sizeof(program->ivec4_uniforms));
    for (index = 0u; index < vertex->ivec4_uniform_count; ++index) {
        if (!ringl_program_add_ivec4_uniform(program,
                                             vertex->ivec4_uniform_names[index]))
            return 0;
    }
    for (index = 0u; index < fragment->ivec4_uniform_count; ++index) {
        if (!ringl_program_add_ivec4_uniform(program,
                                             fragment->ivec4_uniform_names[index]))
            return 0;
    }
    return 1;
}

static int ringl_program_collect_attributes(RinGLProgramObject* program,
                                            const RinGLShaderObject* vertex)
{
    RinGLGlslParseResult result;
    uint32_t assigned_locations = 0u;
    uint32_t scalar_width = 0u;
    uint32_t index;

    program->attribute_count = 0u;
    memset(program->attributes, 0, sizeof(program->attributes));
    if (ringl_glsl_parse(RINGL_VERTEX_SHADER, vertex->source,
                         (size_t)vertex->source_length, &result) != 0 ||
        !result.ok || result.attribute_count > RINGL_MAX_VERTEX_ATTRIBS)
        return 0;

    for (index = 0u; index < result.attribute_count; ++index) {
        ringl_copy_c_string(program->attributes[index].name,
                            sizeof(program->attributes[index].name),
                            result.attribute_names[index]);
        program->attributes[index].width = result.attribute_widths[index];
        if (program->attributes[index].width == 0u ||
            program->attributes[index].width >
                RINGL_MAX_VERTEX_INPUT_COMPONENTS ||
            scalar_width > RINGL_MAX_VERTEX_INPUT_COMPONENTS -
                program->attributes[index].width) {
            return 0;
        }
        scalar_width += program->attributes[index].width;
        program->attributes[index].location = UINT32_MAX;
    }

    /* A requested location may name an attribute which is inactive in this
     * executable. Such a request is retained but cannot collide until both
     * names become active on a later link. */
    for (index = 0u; index < result.attribute_count; ++index) {
        uint32_t binding_index;
        for (binding_index = 0u;
             binding_index < RINGL_MAX_VERTEX_ATTRIBS;
             ++binding_index) {
            const RinGLProgramAttributeBinding* binding =
                &program->attribute_bindings[binding_index];
            uint32_t location_bit;

            if (!binding->active ||
                strcmp(binding->name, program->attributes[index].name) != 0) {
                continue;
            }
            location_bit = UINT32_C(1) << binding->location;
            if ((assigned_locations & location_bit) != 0u)
                return 0;
            program->attributes[index].location = binding->location;
            assigned_locations |= location_bit;
            break;
        }
    }

    /* WebGL permits the implementation to choose unbound locations. Choose
     * the first remaining generic index deterministically, rather than
     * accidentally colliding with a later explicit binding. */
    for (index = 0u; index < result.attribute_count; ++index) {
        uint32_t location;

        if (program->attributes[index].location != UINT32_MAX)
            continue;
        for (location = 0u; location < RINGL_MAX_VERTEX_ATTRIBS; ++location) {
            uint32_t location_bit = UINT32_C(1) << location;
            if ((assigned_locations & location_bit) == 0u) {
                program->attributes[index].location = location;
                assigned_locations |= location_bit;
                break;
            }
        }
        if (location == RINGL_MAX_VERTEX_ATTRIBS)
            return 0;
    }
    program->attribute_count = result.attribute_count;
    return 1;
}

static int ringl_program_collect_varyings(RinGLProgramObject* program,
                                          const RinGLShaderObject* vertex,
                                          const RinGLShaderObject* fragment)
{
    RinGLGlslParseResult vertex_result;
    RinGLGlslParseResult fragment_result;
    uint32_t fragment_location = 0u;
    uint32_t fi;

    program->varying_count = 0u;
    memset(program->varyings, 0, sizeof(program->varyings));
    if (ringl_glsl_parse(RINGL_VERTEX_SHADER, vertex->source,
                         (size_t)vertex->source_length, &vertex_result) != 0 ||
        ringl_glsl_parse(RINGL_FRAGMENT_SHADER, fragment->source,
                         (size_t)fragment->source_length, &fragment_result) != 0)
        return 0;
    if (fragment_result.varying_count > RINGL_MAX_VARYINGS)
        return 0;

    for (fi = 0u; fi < fragment_result.varying_count; ++fi) {
        uint32_t vi;
        uint32_t vertex_location = 4u;
        int found = 0;
        for (vi = 0u; vi < vertex_result.varying_count; ++vi) {
            if (strcmp(vertex_result.varying_names[vi],
                       fragment_result.varying_names[fi]) == 0) {
                if (vertex_result.varying_widths[vi] !=
                    fragment_result.varying_widths[fi])
                    return 0;
                found = 1;
                break;
            }
            vertex_location += vertex_result.varying_widths[vi];
        }
        if (!found || vertex_location + fragment_result.varying_widths[fi] > 32u ||
            fragment_location + fragment_result.varying_widths[fi] > 32u)
            return 0;
        ringl_copy_c_string(program->varyings[fi].name,
                            sizeof(program->varyings[fi].name),
                            fragment_result.varying_names[fi]);
        program->varyings[fi].width = fragment_result.varying_widths[fi];
        program->varyings[fi].vertex_output_location = vertex_location;
        program->varyings[fi].fragment_input_location = fragment_location;
        fragment_location += fragment_result.varying_widths[fi];
        program->varying_count++;
    }
    return 1;
}

uint32_t ringl_create_program(void)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot;
    uint32_t program;
    if (context == NULL)
        return 0u;
    program = ringl_object_allocate(context, RINGL_OBJECT_PROGRAM);
    if (program == 0u) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return 0u;
    }
    slot = ringl_object_lookup(context, program, RINGL_OBJECT_PROGRAM);
    if (slot == NULL) {
        ringl_object_release(context, program, RINGL_OBJECT_PROGRAM);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return 0u;
    }
    ringl_object_promote(slot);
    return program;
}

void ringl_delete_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t vertex_shader;
    uint32_t fragment_shader;
    uint32_t linked_vertex_shader;
    uint32_t linked_fragment_shader;
    if (context == NULL || program == 0u)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL)
        return;
    vertex_shader = object->vertex_shader;
    fragment_shader = object->fragment_shader;
    linked_vertex_shader = object->linked_vertex_shader;
    linked_fragment_shader = object->linked_fragment_shader;
    if (object->vertex_uniform_rsh1 != NULL ||
        object->fragment_uniform_rsh1 != NULL ||
        object->vertex_uniform_module != 0u ||
        object->fragment_uniform_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_program_discard_uniform_artifacts(context, object);
    }
    if (context->current_program == program)
        context->current_program = 0u;
    memset(object, 0, sizeof(*object));
    ringl_object_release(context, program, RINGL_OBJECT_PROGRAM);
    ringl_shader_release_if_delete_pending(context, vertex_shader);
    ringl_shader_release_if_delete_pending(context, fragment_shader);
    ringl_shader_release_if_delete_pending(context, linked_vertex_shader);
    ringl_shader_release_if_delete_pending(context, linked_fragment_shader);
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

int ringl_is_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;
    if (context == NULL || program == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, program, RINGL_OBJECT_PROGRAM);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

void ringl_attach_shader(uint32_t program, uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* target;
    RinGLShaderObject* source;
    if (context == NULL)
        return;
    target = ringl_program_object(context, program);
    source = ringl_program_shader(context, shader);
    if (target == NULL || source == NULL ||
        ringl_shader_is_delete_pending(context, shader)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (source->shader_type == RINGL_VERTEX_SHADER) {
        if (target->vertex_shader != 0u && target->vertex_shader != shader) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        target->vertex_shader = shader;
    } else if (source->shader_type == RINGL_FRAGMENT_SHADER) {
        if (target->fragment_shader != 0u && target->fragment_shader != shader) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        target->fragment_shader = shader;
    } else {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
}

void ringl_detach_shader(uint32_t program, uint32_t shader)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* target;
    RinGLShaderObject* source;

    if (context == NULL)
        return;
    target = ringl_program_object(context, program);
    source = ringl_program_shader(context, shader);
    if (target == NULL || source == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (target->vertex_shader == shader)
        target->vertex_shader = 0u;
    else if (target->fragment_shader == shader)
        target->fragment_shader = 0u;
    else {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_shader_release_if_delete_pending(context, shader);
}

static int ringl_program_prepare_gpu_shader(RinGLContext* context,
                                            uint32_t shader,
                                            RinGLShaderObject* object)
{
    if (context->ringpu_ops.create_shader_module == NULL)
        return 1;
    if (object->rsh1_size == 0u && ringl_lower_shader_rsh1(shader) != 0)
        return 0;
    if (object->ringpu_module == 0u && ringl_realize_shader_module(shader) != 0)
        return 0;
    return object->ringpu_module != 0u;
}

static int ringl_shader_has_numeric_uniforms(const RinGLShaderObject* shader)
{
    return shader != NULL &&
        (shader->float_uniform_count != 0u || shader->int_uniform_count != 0u ||
         shader->vec2_uniform_count != 0u || shader->ivec2_uniform_count != 0u ||
         shader->vec3_uniform_count != 0u || shader->vec4_uniform_count != 0u ||
         shader->ivec3_uniform_count != 0u || shader->ivec4_uniform_count != 0u ||
         shader->mat4_uniform_count != 0u);
}

static int ringl_shader_has_uniform_name(const RinGLShaderObject* shader,
                                         uint32_t type, const char* name)
{
    uint32_t index;
    const char (*names)[RINGL_UNIFORM_NAME_MAX] = NULL;
    uint32_t count = 0u;

    if (shader == NULL || name == NULL)
        return 0;
    switch (type) {
    case RINGL_FLOAT:
        names = shader->float_uniform_names;
        count = shader->float_uniform_count;
        break;
    case RINGL_INT:
        names = shader->int_uniform_names;
        count = shader->int_uniform_count;
        break;
    case RINGL_FLOAT_VEC2:
        names = shader->vec2_uniform_names;
        count = shader->vec2_uniform_count;
        break;
    case RINGL_INT_VEC2:
        names = shader->ivec2_uniform_names;
        count = shader->ivec2_uniform_count;
        break;
    case RINGL_FLOAT_VEC3:
        names = shader->vec3_uniform_names;
        count = shader->vec3_uniform_count;
        break;
    case RINGL_INT_VEC3:
        names = shader->ivec3_uniform_names;
        count = shader->ivec3_uniform_count;
        break;
    case RINGL_FLOAT_VEC4:
        names = shader->vec4_uniform_names;
        count = shader->vec4_uniform_count;
        break;
    case RINGL_INT_VEC4:
        names = shader->ivec4_uniform_names;
        count = shader->ivec4_uniform_count;
        break;
    case RINGL_FLOAT_MAT4:
        names = shader->mat4_uniform_names;
        count = shader->mat4_uniform_count;
        break;
    default:
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        if (strcmp(names[index], name) == 0)
            return 1;
    }
    return 0;
}

static int ringl_program_lower_uniform_shader(
    RinGLContext* context, const RinGLShaderObject* shader,
    const RinGLGlslUniformValue* uniforms, uint32_t uniform_count,
    uint8_t** rsh1_out, uint32_t* rsh1_size_out, uint64_t* module_out)
{
    RinGLGlslLowerResult lowered;
    uint8_t* copy;
    uint64_t module = 0u;

    if (context == NULL || shader == NULL || rsh1_out == NULL ||
        rsh1_size_out == NULL || module_out == NULL || shader->source == NULL)
        return 0;
    *rsh1_out = NULL;
    *rsh1_size_out = 0u;
    *module_out = 0u;
    if (((shader->uses_standard_derivatives == 0u &&
          shader->uses_webgl_draw_buffers == 0u &&
          strstr(shader->source, "varying") != NULL)
             ? ringl_glsl_lower_varying_rsh1_with_uniforms(
                   shader->shader_type, shader->source,
                   (size_t)shader->source_length, uniforms, uniform_count,
                   &lowered)
             : ringl_glsl_lower_rsh1_with_uniforms(
                   shader->shader_type, shader->source,
                   (size_t)shader->source_length, uniforms, uniform_count,
                   &lowered)) != 0 ||
        !lowered.ok || lowered.byte_size == 0u) {
        return 0;
    }
    copy = malloc(lowered.byte_size);
    if (copy == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return 0;
    }
    memcpy(copy, lowered.bytes, lowered.byte_size);
    if (context->has_ringpu_ops &&
        context->ringpu_ops.create_shader_module != NULL &&
        (ringl_backend_create_shader_module(context, copy, lowered.byte_size,
                                            &module) != 0 ||
         module == 0u)) {
        free(copy);
        return 0;
    }
    *rsh1_out = copy;
    *rsh1_size_out = lowered.byte_size;
    *module_out = module;
    return 1;
}

/* Compile linked scalar/vector values into separate RSH1 modules. RinGPU executes the
 * generated CONST_F32 instructions directly; this is intentionally a
 * program-owned executable, not source replacement or a CPU draw shortcut. */
static int ringl_program_rebuild_uniform_artifacts(
    RinGLContext* context, RinGLProgramObject* program,
    const RinGLShaderObject* vertex, const RinGLShaderObject* fragment,
    int rebuild_vertex, int rebuild_fragment)
{
    RinGLGlslUniformValue uniforms[RINGL_GLSL_MAX_UNIFORMS];
    uint8_t* vertex_rsh1 = NULL;
    uint8_t* fragment_rsh1 = NULL;
    uint32_t vertex_rsh1_size = 0u;
    uint32_t fragment_rsh1_size = 0u;
    uint64_t vertex_module = 0u;
    uint64_t fragment_module = 0u;
    int vertex_has_numeric_uniforms;
    int fragment_has_numeric_uniforms;
    uint32_t index;
    uint32_t uniform_count = 0u;

    if (context == NULL || program == NULL || vertex == NULL ||
        fragment == NULL ||
        (program->float_uniform_count == 0u && program->int_uniform_count == 0u &&
         program->vec2_uniform_count == 0u && program->ivec2_uniform_count == 0u &&
         program->vec3_uniform_count == 0u && program->ivec3_uniform_count == 0u &&
         program->vec4_uniform_count == 0u && program->ivec4_uniform_count == 0u &&
         program->mat4_uniform_count == 0u) ||
        program->float_uniform_count > RINGL_MAX_FLOAT_UNIFORMS ||
        program->int_uniform_count > RINGL_MAX_INT_UNIFORMS ||
        program->vec2_uniform_count > RINGL_MAX_VEC2_UNIFORMS ||
        program->ivec2_uniform_count > RINGL_MAX_IVEC2_UNIFORMS ||
        program->vec3_uniform_count > RINGL_MAX_VEC3_UNIFORMS ||
        program->ivec3_uniform_count > RINGL_MAX_IVEC3_UNIFORMS ||
        program->vec4_uniform_count > RINGL_MAX_VEC4_UNIFORMS ||
        program->ivec4_uniform_count > RINGL_MAX_IVEC4_UNIFORMS ||
        program->mat4_uniform_count > RINGL_MAX_MAT4_UNIFORMS)
        return 0;
    vertex_has_numeric_uniforms = ringl_shader_has_numeric_uniforms(vertex);
    fragment_has_numeric_uniforms = ringl_shader_has_numeric_uniforms(fragment);
    rebuild_vertex = rebuild_vertex && vertex_has_numeric_uniforms;
    rebuild_fragment = rebuild_fragment && fragment_has_numeric_uniforms;
    if (!rebuild_vertex && !rebuild_fragment)
        return 0;
    memset(uniforms, 0, sizeof(uniforms));
    for (index = 0u; index < program->float_uniform_count; ++index) {
        uniforms[uniform_count].name = program->float_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_FLOAT;
        uniforms[uniform_count++].values[0] = program->float_uniforms[index].value;
    }
    for (index = 0u; index < program->int_uniform_count; ++index) {
        uniforms[uniform_count].name = program->int_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_INT;
        uniforms[uniform_count++].i32_values[0] = program->int_uniforms[index].value;
    }
    for (index = 0u; index < program->vec2_uniform_count; ++index) {
        uniforms[uniform_count].name = program->vec2_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_FLOAT_VEC2;
        memcpy(uniforms[uniform_count++].values,
               program->vec2_uniforms[index].values,
               sizeof(program->vec2_uniforms[index].values));
    }
    for (index = 0u; index < program->ivec2_uniform_count; ++index) {
        uniforms[uniform_count].name = program->ivec2_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_INT_VEC2;
        memcpy(uniforms[uniform_count++].i32_values,
               program->ivec2_uniforms[index].values,
               sizeof(program->ivec2_uniforms[index].values));
    }
    for (index = 0u; index < program->vec3_uniform_count; ++index) {
        uniforms[uniform_count].name = program->vec3_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_FLOAT_VEC3;
        memcpy(uniforms[uniform_count++].values,
               program->vec3_uniforms[index].values,
               sizeof(program->vec3_uniforms[index].values));
    }
    for (index = 0u; index < program->ivec3_uniform_count; ++index) {
        uniforms[uniform_count].name = program->ivec3_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_INT_VEC3;
        memcpy(uniforms[uniform_count++].i32_values,
               program->ivec3_uniforms[index].values,
               sizeof(program->ivec3_uniforms[index].values));
    }
    for (index = 0u; index < program->vec4_uniform_count; ++index) {
        uniforms[uniform_count].name = program->vec4_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_FLOAT_VEC4;
        memcpy(uniforms[uniform_count++].values,
               program->vec4_uniforms[index].values,
               sizeof(program->vec4_uniforms[index].values));
    }
    for (index = 0u; index < program->ivec4_uniform_count; ++index) {
        uniforms[uniform_count].name = program->ivec4_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_INT_VEC4;
        memcpy(uniforms[uniform_count++].i32_values,
               program->ivec4_uniforms[index].values,
               sizeof(program->ivec4_uniforms[index].values));
    }
    for (index = 0u; index < program->mat4_uniform_count; ++index) {
        uniforms[uniform_count].name = program->mat4_uniforms[index].name;
        uniforms[uniform_count].type = RINGL_FLOAT_MAT4;
        memcpy(uniforms[uniform_count++].values,
               program->mat4_uniforms[index].values,
               sizeof(program->mat4_uniforms[index].values));
    }
    if ((rebuild_vertex &&
         !ringl_program_lower_uniform_shader(
             context, vertex, uniforms,
             uniform_count,
             &vertex_rsh1, &vertex_rsh1_size, &vertex_module)) ||
        (rebuild_fragment &&
         !ringl_program_lower_uniform_shader(
             context, fragment, uniforms,
             uniform_count,
             &fragment_rsh1, &fragment_rsh1_size, &fragment_module))) {
        if (vertex_module != 0u)
            ringl_backend_destroy_object(context, vertex_module);
        if (fragment_module != 0u)
            ringl_backend_destroy_object(context, fragment_module);
        free(vertex_rsh1);
        free(fragment_rsh1);
        return 0;
    }

    ringl_invalidate_graphics_artifacts(context);
    if (rebuild_vertex) {
        if (program->vertex_uniform_module != 0u)
            ringl_backend_destroy_object(context, program->vertex_uniform_module);
        free(program->vertex_uniform_rsh1);
        program->vertex_uniform_rsh1 = vertex_rsh1;
        program->vertex_uniform_rsh1_size = vertex_rsh1_size;
        program->vertex_uniform_module = vertex_module;
    }
    if (rebuild_fragment) {
        if (program->fragment_uniform_module != 0u)
            ringl_backend_destroy_object(context, program->fragment_uniform_module);
        free(program->fragment_uniform_rsh1);
        program->fragment_uniform_rsh1 = fragment_rsh1;
        program->fragment_uniform_rsh1_size = fragment_rsh1_size;
        program->fragment_uniform_module = fragment_module;
    }
    return 1;
}

/* A uniform update must replace every stage that reads that name, but it must
 * not invalidate an independent program-owned module. In particular, a
 * vertex transform update must keep a fragment texture/tint executable (and
 * its sampler layout) alive until that fragment's own uniform changes. */
static int ringl_program_rebuild_uniform_for_name(
    RinGLContext* context, RinGLProgramObject* program,
    const RinGLShaderObject* vertex, const RinGLShaderObject* fragment,
    uint32_t type, const char* name)
{
    int rebuild_vertex;
    int rebuild_fragment;

    rebuild_vertex = ringl_shader_has_uniform_name(vertex, type, name);
    rebuild_fragment = ringl_shader_has_uniform_name(fragment, type, name);
    if (!rebuild_vertex && !rebuild_fragment)
        return 0;
    return ringl_program_rebuild_uniform_artifacts(
        context, program, vertex, fragment, rebuild_vertex, rebuild_fragment);
}

void ringl_link_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    uint32_t old_linked_vertex;
    uint32_t old_linked_fragment;
    if (context == NULL)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    /* The old linked executable is no longer retained by this bounded linker;
     * retire its program-owned uniform modules before rebuilding reflection. */
    if (object->vertex_uniform_rsh1 != NULL ||
        object->fragment_uniform_rsh1 != NULL ||
        object->vertex_uniform_module != 0u ||
        object->fragment_uniform_module != 0u) {
        ringl_invalidate_graphics_artifacts(context);
        ringl_program_discard_uniform_artifacts(context, object);
    }
    object->link_status = RINGL_FALSE;
    object->validate_status = RINGL_FALSE;
    object->attribute_count = 0u;
    object->sampler_uniform_count = 0u;
    object->float_uniform_count = 0u;
    object->int_uniform_count = 0u;
    object->vec2_uniform_count = 0u;
    object->vec3_uniform_count = 0u;
    object->vec4_uniform_count = 0u;
    object->ivec2_uniform_count = 0u;
    object->ivec3_uniform_count = 0u;
    object->ivec4_uniform_count = 0u;
    object->mat4_uniform_count = 0u;
    object->varying_count = 0u;
    memset(object->attributes, 0, sizeof(object->attributes));
    memset(object->sampler_uniforms, 0, sizeof(object->sampler_uniforms));
    memset(object->float_uniforms, 0, sizeof(object->float_uniforms));
    memset(object->int_uniforms, 0, sizeof(object->int_uniforms));
    memset(object->vec2_uniforms, 0, sizeof(object->vec2_uniforms));
    memset(object->vec3_uniforms, 0, sizeof(object->vec3_uniforms));
    memset(object->vec4_uniforms, 0, sizeof(object->vec4_uniforms));
    memset(object->ivec2_uniforms, 0, sizeof(object->ivec2_uniforms));
    memset(object->ivec3_uniforms, 0, sizeof(object->ivec3_uniforms));
    memset(object->ivec4_uniforms, 0, sizeof(object->ivec4_uniforms));
    memset(object->mat4_uniforms, 0, sizeof(object->mat4_uniforms));
    memset(object->varyings, 0, sizeof(object->varyings));
    if (object->vertex_shader == 0u || object->fragment_shader == 0u) {
        ringl_program_set_log(object, "vertex and fragment shaders are required");
        return;
    }
    vertex = ringl_program_shader(context, object->vertex_shader);
    fragment = ringl_program_shader(context, object->fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_program_set_log(object, "attached shader was deleted");
        return;
    }
    if (!vertex->compile_status || !fragment->compile_status) {
        ringl_program_set_log(object, "all attached shaders must compile successfully");
        return;
    }
    if (!ringl_program_collect_attributes(object, vertex)) {
        ringl_program_set_log(object, "invalid active attribute interface");
        return;
    }
    if (!ringl_program_collect_sampler_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object, "too many active sampler uniforms");
        return;
    }
    if (!ringl_program_collect_float_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object,
                              "invalid or too many active float uniforms");
        return;
    }
    if (!ringl_program_collect_vec2_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object, "invalid or too many active vec2 uniforms");
        return;
    }
    if (!ringl_program_collect_vec3_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object, "invalid or too many active vec3 uniforms");
        return;
    }
    if (!ringl_program_collect_vec4_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object,
                              "invalid or too many active vec4 uniforms");
        return;
    }
    if (!ringl_program_collect_mat4_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object,
                              "invalid or too many active mat4 uniforms");
        return;
    }
    if (!ringl_program_collect_int_uniforms(object, vertex, fragment) ||
        !ringl_program_collect_ivec2_uniforms(object, vertex, fragment) ||
        !ringl_program_collect_ivec3_uniforms(object, vertex, fragment) ||
        !ringl_program_collect_ivec4_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object,
                              "integer uniform interface mismatch or capacity exceeded");
        return;
    }
    if (!ringl_program_collect_varyings(object, vertex, fragment)) {
        ringl_program_set_log(object, "vertex/fragment varying interface mismatch");
        return;
    }
    if (context->has_ringpu_ops &&
        context->ringpu_ops.create_shader_module != NULL) {
        /* A program-owned module is required only for the stage that reads a
         * mutable numeric uniform. Keep the opposite stage on its normal
         * shader object so a sampler/varying lowering profile is not
         * incorrectly re-parsed by the scalar uniform lowerer. */
        if (!ringl_shader_has_numeric_uniforms(vertex) &&
            !ringl_program_prepare_gpu_shader(context, object->vertex_shader, vertex)) {
            ringl_program_set_log(object, "vertex shader failed RinGPU validation");
            return;
        }
        if (!ringl_shader_has_numeric_uniforms(fragment) &&
            !ringl_program_prepare_gpu_shader(context, object->fragment_shader, fragment)) {
            ringl_program_set_log(object, "fragment shader failed RinGPU validation");
            return;
        }
        /* The linked fragment may instead use a program-owned numeric-uniform
         * module. Its resource layout is still sourced from the validated
         * shader lowering, because draw-time texture binding maps declared
         * sampler locations to the RSH1 resource pairs. Do not leave that
         * metadata empty merely because the executable module is rebuilt per
         * program. */
        if (ringl_shader_has_numeric_uniforms(fragment) &&
            fragment->sampler_uniform_count != 0u && fragment->rsh1_size == 0u &&
            ringl_lower_shader_rsh1(object->fragment_shader) != 0) {
            ringl_program_set_log(object,
                                  "fragment sampler metadata failed RinGPU validation");
            return;
        }
    }
    if ((object->float_uniform_count != 0u || object->int_uniform_count != 0u ||
         object->vec2_uniform_count != 0u || object->ivec2_uniform_count != 0u ||
         object->vec3_uniform_count != 0u || object->ivec3_uniform_count != 0u ||
         object->vec4_uniform_count != 0u || object->ivec4_uniform_count != 0u ||
         object->mat4_uniform_count != 0u) &&
        !ringl_program_rebuild_uniform_artifacts(context, object, vertex,
                                                 fragment,
                                                 ringl_shader_has_numeric_uniforms(vertex),
                                                 ringl_shader_has_numeric_uniforms(fragment))) {
        ringl_program_set_log(object,
                              "uniform shader is outside the RinGPU lowering profile");
        return;
    }
    old_linked_vertex = object->linked_vertex_shader;
    old_linked_fragment = object->linked_fragment_shader;
    object->linked_vertex_shader = object->vertex_shader;
    object->linked_fragment_shader = object->fragment_shader;
    object->link_status = RINGL_TRUE;
    ringl_program_set_log(object, "");
    ringl_shader_release_if_delete_pending(context, old_linked_vertex);
    ringl_shader_release_if_delete_pending(context, old_linked_fragment);
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

uint32_t ringl_get_program_link_status(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    if (context == NULL)
        return RINGL_FALSE;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return RINGL_FALSE;
    }
    return object->link_status;
}

void ringl_validate_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    /* A successfully linked RinGL executable has already passed the only
     * backend validation that this bounded implementation can require. */
    object->validate_status = object->link_status ? RINGL_TRUE : RINGL_FALSE;
}

int ringl_get_program_info(uint32_t program, RinGLProgramInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLProgramInfoV1 result;

    if (context == NULL || info == NULL)
        return -1;
    if (info->struct_size < sizeof(*info) || info->api_version != RINGL_API_VERSION)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }

    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.api_version = RINGL_API_VERSION;
    result.link_status = object->link_status;
    result.validate_status = object->validate_status;
    result.attached_shader_count =
        (object->vertex_shader != 0u ? 1u : 0u) +
        (object->fragment_shader != 0u ? 1u : 0u);
    if (object->link_status) {
        result.active_attribute_count = object->attribute_count;
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
    }
    *info = result;
    return 0;
}

int ringl_get_attached_shaders(uint32_t program, uint32_t* shaders,
                               uint32_t capacity,
                               uint32_t* shader_count_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t result[2];
    uint32_t count = 0u;

    if (context == NULL || shader_count_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (object->vertex_shader != 0u)
        result[count++] = object->vertex_shader;
    if (object->fragment_shader != 0u)
        result[count++] = object->fragment_shader;
    if ((shaders == NULL && capacity != 0u) ||
        (shaders != NULL && capacity < count)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (shaders != NULL && count != 0u)
        memcpy(shaders, result, count * sizeof(result[0]));
    *shader_count_out = count;
    return 0;
}

static int ringl_active_info_header_valid(const RinGLActiveInfoV1* info)
{
    return info != NULL && info->struct_size >= sizeof(*info) &&
           info->api_version == RINGL_API_VERSION;
}

static uint32_t ringl_attribute_gl_type(uint32_t width)
{
    switch (width) {
    case 1u:
        return RINGL_FLOAT;
    case 2u:
        return RINGL_FLOAT_VEC2;
    case 3u:
        return RINGL_FLOAT_VEC3;
    case 4u:
        return RINGL_FLOAT_VEC4;
    default:
        return 0u;
    }
}

static void ringl_active_info_set(RinGLActiveInfoV1* result,
                                  uint32_t type, const char* name)
{
    size_t name_length = strlen(name);

    memset(result, 0, sizeof(*result));
    result->struct_size = sizeof(*result);
    result->api_version = RINGL_API_VERSION;
    result->type = type;
    result->size = 1u;
    result->name_length = (uint32_t)name_length;
    memcpy(result->name, name, name_length + 1u);
}

int ringl_get_active_attrib(uint32_t program, uint32_t index,
                            RinGLActiveInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLActiveInfoV1 result;
    uint32_t type;

    if (context == NULL || !ringl_active_info_header_valid(info))
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (index >= object->attribute_count) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    type = ringl_attribute_gl_type(object->attributes[index].width);
    if (type == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    ringl_active_info_set(&result, type, object->attributes[index].name);
    *info = result;
    return 0;
}

int ringl_get_active_uniform(uint32_t program, uint32_t index,
                             RinGLActiveInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLActiveInfoV1 result;

    if (context == NULL || !ringl_active_info_header_valid(info))
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (index >= object->sampler_uniform_count + object->float_uniform_count +
                     object->vec2_uniform_count + object->vec3_uniform_count +
                     object->vec4_uniform_count + object->mat4_uniform_count +
                     object->int_uniform_count + object->ivec2_uniform_count +
                     object->ivec3_uniform_count + object->ivec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (index < object->sampler_uniform_count) {
        ringl_active_info_set(&result, RINGL_SAMPLER_2D,
                              object->sampler_uniforms[index].name);
    } else if (index < object->sampler_uniform_count +
                           object->float_uniform_count) {
        ringl_active_info_set(&result, RINGL_FLOAT,
                              object->float_uniforms[
                                  index - object->sampler_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                           object->float_uniform_count +
                           object->vec2_uniform_count) {
        ringl_active_info_set(&result, RINGL_FLOAT_VEC2,
                              object->vec2_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                           object->float_uniform_count +
                           object->vec2_uniform_count +
                           object->vec3_uniform_count) {
        ringl_active_info_set(&result, RINGL_FLOAT_VEC3,
                              object->vec3_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                       object->float_uniform_count +
                       object->vec2_uniform_count +
                       object->vec3_uniform_count +
                       object->vec4_uniform_count) {
        ringl_active_info_set(&result, RINGL_FLOAT_VEC4,
                              object->vec4_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                       object->float_uniform_count +
                       object->vec2_uniform_count +
                       object->vec3_uniform_count +
                       object->vec4_uniform_count +
                       object->mat4_uniform_count) {
        ringl_active_info_set(&result, RINGL_FLOAT_MAT4,
                              object->mat4_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count -
                                      object->vec4_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                       object->float_uniform_count +
                       object->vec2_uniform_count +
                       object->vec3_uniform_count +
                       object->vec4_uniform_count +
                       object->mat4_uniform_count +
                       object->int_uniform_count) {
        ringl_active_info_set(&result, RINGL_INT,
                              object->int_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count -
                                      object->vec4_uniform_count -
                                      object->mat4_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                       object->float_uniform_count +
                       object->vec2_uniform_count +
                       object->vec3_uniform_count +
                       object->vec4_uniform_count +
                       object->mat4_uniform_count +
                       object->int_uniform_count +
                       object->ivec2_uniform_count) {
        ringl_active_info_set(&result, RINGL_INT_VEC2,
                              object->ivec2_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count -
                                      object->vec4_uniform_count -
                                      object->mat4_uniform_count -
                                      object->int_uniform_count].name);
    } else if (index < object->sampler_uniform_count +
                       object->float_uniform_count +
                       object->vec2_uniform_count +
                       object->vec3_uniform_count +
                       object->vec4_uniform_count +
                       object->mat4_uniform_count +
                       object->int_uniform_count +
                       object->ivec2_uniform_count +
                       object->ivec3_uniform_count) {
        ringl_active_info_set(&result, RINGL_INT_VEC3,
                              object->ivec3_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count -
                                      object->vec4_uniform_count -
                                      object->mat4_uniform_count -
                                      object->int_uniform_count -
                                      object->ivec2_uniform_count].name);
    } else {
        ringl_active_info_set(&result, RINGL_INT_VEC4,
                              object->ivec4_uniforms[
                                  index - object->sampler_uniform_count -
                                      object->float_uniform_count -
                                      object->vec2_uniform_count -
                                      object->vec3_uniform_count -
                                      object->vec4_uniform_count -
                                      object->mat4_uniform_count -
                                      object->int_uniform_count -
                                      object->ivec2_uniform_count -
                                      object->ivec3_uniform_count].name);
    }
    *info = result;
    return 0;
}

uint64_t ringl_get_program_info_log(uint32_t program, char* buffer,
                                    uint64_t buffer_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    size_t length;
    size_t copy_length;
    if (context == NULL)
        return 0u;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    length = strlen(object->info_log);
    if (buffer == NULL || buffer_size == 0u)
        return (uint64_t)length;
    copy_length = length;
    if ((uint64_t)copy_length >= buffer_size)
        copy_length = (size_t)(buffer_size - 1u);
    memcpy(buffer, object->info_log, copy_length);
    buffer[copy_length] = '\0';
    return (uint64_t)length;
}

static int ringl_program_binding_name_valid(const char* name)
{
    size_t length = 0u;

    if (name == NULL || name[0] == '\0')
        return 0;
    while (length < RINGL_UNIFORM_NAME_MAX && name[length] != '\0')
        ++length;
    return length != RINGL_UNIFORM_NAME_MAX;
}

void ringl_bind_attrib_location(uint32_t program, uint32_t index,
                                const char* name)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t binding_index;

    if (context == NULL)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (index >= RINGL_MAX_VERTEX_ATTRIBS ||
        !ringl_program_binding_name_valid(name)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (name[0] == 'g' && name[1] == 'l' && name[2] == '_') {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    for (binding_index = 0u;
         binding_index < RINGL_MAX_VERTEX_ATTRIBS;
         ++binding_index) {
        RinGLProgramAttributeBinding* binding =
            &object->attribute_bindings[binding_index];
        if (binding->active && strcmp(binding->name, name) == 0) {
            binding->location = index;
            return;
        }
    }
    for (binding_index = 0u;
         binding_index < RINGL_MAX_VERTEX_ATTRIBS;
         ++binding_index) {
        RinGLProgramAttributeBinding* binding =
            &object->attribute_bindings[binding_index];
        if (!binding->active) {
            ringl_copy_c_string(binding->name, sizeof(binding->name), name);
            binding->location = index;
            binding->active = RINGL_TRUE;
            return;
        }
    }
    ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
}

void ringl_use_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    if (context == NULL)
        return;
    if (program == 0u) {
        context->current_program = 0u;
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
        return;
    }
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (context->current_program != program) {
        context->current_program = program;
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

uint32_t ringl_get_current_program(void)
{
    RinGLContext* context = ringl_get_current_context();
    return context == NULL ? 0u : context->current_program;
}

int32_t ringl_get_attrib_location(uint32_t program, const char* name)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t index;

    if (context == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || name == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    for (index = 0u; index < object->attribute_count; ++index) {
        if (strcmp(object->attributes[index].name, name) == 0)
            return (int32_t)object->attributes[index].location;
    }
    return -1;
}

int32_t ringl_get_uniform_location(uint32_t program, const char* name)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t i;
    if (context == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || name == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    for (i = 0u; i < object->sampler_uniform_count; ++i) {
        if (strcmp(object->sampler_uniforms[i].name, name) == 0)
            return (int32_t)i;
    }
    for (i = 0u; i < object->float_uniform_count; ++i) {
        if (strcmp(object->float_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count + i);
    }
    for (i = 0u; i < object->vec2_uniform_count; ++i) {
        if (strcmp(object->vec2_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count + i);
    }
    for (i = 0u; i < object->vec3_uniform_count; ++i) {
        if (strcmp(object->vec3_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count + i);
    }
    for (i = 0u; i < object->vec4_uniform_count; ++i) {
        if (strcmp(object->vec4_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count + i);
    }
    for (i = 0u; i < object->mat4_uniform_count; ++i) {
        if (strcmp(object->mat4_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count +
                             object->vec4_uniform_count + i);
    }
    for (i = 0u; i < object->int_uniform_count; ++i) {
        if (strcmp(object->int_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count +
                             object->vec4_uniform_count +
                             object->mat4_uniform_count + i);
    }
    for (i = 0u; i < object->ivec2_uniform_count; ++i) {
        if (strcmp(object->ivec2_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count +
                             object->vec4_uniform_count +
                             object->mat4_uniform_count +
                             object->int_uniform_count + i);
    }
    for (i = 0u; i < object->ivec3_uniform_count; ++i) {
        if (strcmp(object->ivec3_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count +
                             object->vec4_uniform_count +
                             object->mat4_uniform_count +
                             object->int_uniform_count +
                             object->ivec2_uniform_count + i);
    }
    for (i = 0u; i < object->ivec4_uniform_count; ++i) {
        if (strcmp(object->ivec4_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count +
                             object->vec3_uniform_count +
                             object->vec4_uniform_count +
                             object->mat4_uniform_count +
                             object->int_uniform_count +
                             object->ivec2_uniform_count +
                             object->ivec3_uniform_count + i);
    }
    return -1;
}

static uint32_t ringl_int_uniform_first_location(const RinGLProgramObject* object)
{
    return object->sampler_uniform_count + object->float_uniform_count +
           object->vec2_uniform_count + object->vec3_uniform_count +
           object->vec4_uniform_count + object->mat4_uniform_count;
}

static uint32_t ringl_ivec2_uniform_first_location(const RinGLProgramObject* object)
{
    return ringl_int_uniform_first_location(object) + object->int_uniform_count;
}

static uint32_t ringl_ivec3_uniform_first_location(const RinGLProgramObject* object)
{
    return ringl_ivec2_uniform_first_location(object) + object->ivec2_uniform_count;
}

static uint32_t ringl_ivec4_uniform_first_location(const RinGLProgramObject* object)
{
    return ringl_ivec3_uniform_first_location(object) + object->ivec3_uniform_count;
}

void ringl_uniform_1i(int32_t location, int32_t value)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLProgramIntUniform* uniform;
    int32_t previous_value;
    uint32_t first_location;
    if (context == NULL || location == -1)
        return;
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (location < 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint32_t)location < object->sampler_uniform_count) {
        if (object->sampler_uniforms[location].texture_unit == value)
            return;
        object->sampler_uniforms[location].texture_unit = value;
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
        return;
    }
    first_location = ringl_int_uniform_first_location(object);
    if ((uint32_t)location < first_location ||
        (uint32_t)location >= first_location + object->int_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform = &object->int_uniforms[(uint32_t)location - first_location];
    if (uniform->value == value)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    previous_value = uniform->value;
    uniform->value = value;
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment, RINGL_INT, uniform->name)) {
        uniform->value = previous_value;
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

int ringl_get_uniform_1i(uint32_t program, int32_t location,
                          int32_t* value_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL || value_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || !object->link_status || location < 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if ((uint32_t)location < object->sampler_uniform_count) {
        *value_out = object->sampler_uniforms[location].texture_unit;
        return 0;
    }
    {
        uint32_t first_location = ringl_int_uniform_first_location(object);
        if ((uint32_t)location < first_location ||
            (uint32_t)location >= first_location + object->int_uniform_count) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        *value_out = object->int_uniforms[(uint32_t)location - first_location].value;
    }
    return 0;
}

void ringl_uniform_1f(int32_t location, float value)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLProgramFloatUniform* uniform;
    float previous_value;
    uint32_t uniform_index;

    if (context == NULL || location == -1)
        return;
    if (!isfinite(value)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < object->sampler_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->float_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count;
    uniform = &object->float_uniforms[uniform_index];
    if (uniform->value == value)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    previous_value = uniform->value;
    uniform->value = value;
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment, RINGL_FLOAT, uniform->name)) {
        uniform->value = previous_value;
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

int ringl_get_uniform_1f(uint32_t program, int32_t location,
                          float* value_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t uniform_index;

    if (context == NULL || value_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < object->sampler_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->float_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count;
    *value_out = object->float_uniforms[uniform_index].value;
    return 0;
}

typedef float* (*RinGLVectorUniformValuesAt)(RinGLProgramObject* object,
                                             uint32_t uniform_index);
typedef const char* (*RinGLVectorUniformNameAt)(RinGLProgramObject* object,
                                                uint32_t uniform_index);

static float* ringl_vec2_uniform_values_at(RinGLProgramObject* object,
                                            uint32_t uniform_index)
{
    return object->vec2_uniforms[uniform_index].values;
}

static float* ringl_vec3_uniform_values_at(RinGLProgramObject* object,
                                            uint32_t uniform_index)
{
    return object->vec3_uniforms[uniform_index].values;
}

static const char* ringl_vec2_uniform_name_at(RinGLProgramObject* object,
                                               uint32_t uniform_index)
{
    return object->vec2_uniforms[uniform_index].name;
}

static const char* ringl_vec3_uniform_name_at(RinGLProgramObject* object,
                                               uint32_t uniform_index)
{
    return object->vec3_uniforms[uniform_index].name;
}

static void ringl_set_vector_uniform(int32_t location, const float* values,
                                     uint32_t width, uint32_t first_location,
                                     uint32_t uniform_count,
                                     RinGLVectorUniformValuesAt values_at,
                                     RinGLVectorUniformNameAt name_at)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    float previous_values[3];
    float* storage;
    uint32_t uniform_index;
    uint32_t index;

    if (context == NULL || location == -1)
        return;
    if (values == NULL || values_at == NULL || name_at == NULL ||
        width < 2u || width > 3u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    for (index = 0u; index < width; ++index) {
        if (!isfinite(values[index])) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - first_location;
    storage = values_at(object, uniform_index);
    if (memcmp(storage, values, width * sizeof(*values)) == 0)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memcpy(previous_values, storage, width * sizeof(*storage));
    memcpy(storage, values, width * sizeof(*storage));
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment,
            width == 2u ? RINGL_FLOAT_VEC2 : RINGL_FLOAT_VEC3,
            name_at(object, uniform_index))) {
        memcpy(storage, previous_values, width * sizeof(*storage));
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

void ringl_uniform_2f(int32_t location, float x, float y)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    float values[2] = { x, y };

    if (context == NULL || location == -1)
        return;
    object = ringl_program_object(context, context->current_program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_set_vector_uniform(location, values, 2u,
                             object->sampler_uniform_count +
                             object->float_uniform_count,
                             object->vec2_uniform_count,
                             ringl_vec2_uniform_values_at,
                             ringl_vec2_uniform_name_at);
}

void ringl_uniform_3f(int32_t location, float x, float y, float z)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    float values[3] = { x, y, z };

    if (context == NULL || location == -1)
        return;
    object = ringl_program_object(context, context->current_program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_set_vector_uniform(location, values, 3u,
                             object->sampler_uniform_count +
                             object->float_uniform_count +
                             object->vec2_uniform_count,
                             object->vec3_uniform_count,
                             ringl_vec3_uniform_values_at,
                             ringl_vec3_uniform_name_at);
}

int ringl_get_uniform_2f(uint32_t program, int32_t location,
                          float values_out[2])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t first_location;
    uint32_t uniform_index;

    if (context == NULL || values_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    first_location = object == NULL ? 0u : object->sampler_uniform_count +
                                           object->float_uniform_count;
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + object->vec2_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - first_location;
    memcpy(values_out, object->vec2_uniforms[uniform_index].values,
           sizeof(object->vec2_uniforms[uniform_index].values));
    return 0;
}

int ringl_get_uniform_3f(uint32_t program, int32_t location,
                          float values_out[3])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t first_location;
    uint32_t uniform_index;

    if (context == NULL || values_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    first_location = object == NULL ? 0u : object->sampler_uniform_count +
                                           object->float_uniform_count +
                                           object->vec2_uniform_count;
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + object->vec3_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - first_location;
    memcpy(values_out, object->vec3_uniforms[uniform_index].values,
           sizeof(object->vec3_uniforms[uniform_index].values));
    return 0;
}

typedef int32_t* (*RinGLIntegerVectorUniformValuesAt)(
    RinGLProgramObject* object, uint32_t uniform_index);
typedef const char* (*RinGLIntegerVectorUniformNameAt)(
    RinGLProgramObject* object, uint32_t uniform_index);

static int32_t* ringl_ivec2_uniform_values_at(RinGLProgramObject* object,
                                               uint32_t uniform_index)
{
    return object->ivec2_uniforms[uniform_index].values;
}

static int32_t* ringl_ivec3_uniform_values_at(RinGLProgramObject* object,
                                               uint32_t uniform_index)
{
    return object->ivec3_uniforms[uniform_index].values;
}

static int32_t* ringl_ivec4_uniform_values_at(RinGLProgramObject* object,
                                               uint32_t uniform_index)
{
    return object->ivec4_uniforms[uniform_index].values;
}

static const char* ringl_ivec2_uniform_name_at(RinGLProgramObject* object,
                                                uint32_t uniform_index)
{
    return object->ivec2_uniforms[uniform_index].name;
}

static const char* ringl_ivec3_uniform_name_at(RinGLProgramObject* object,
                                                uint32_t uniform_index)
{
    return object->ivec3_uniforms[uniform_index].name;
}

static const char* ringl_ivec4_uniform_name_at(RinGLProgramObject* object,
                                                uint32_t uniform_index)
{
    return object->ivec4_uniforms[uniform_index].name;
}

static void ringl_set_integer_vector_uniform(
    int32_t location, const int32_t* values, uint32_t width,
    uint32_t first_location, uint32_t uniform_count, uint32_t type,
    RinGLIntegerVectorUniformValuesAt values_at,
    RinGLIntegerVectorUniformNameAt name_at)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    int32_t previous_values[4];
    int32_t* storage;
    uint32_t uniform_index;

    if (context == NULL || location == -1)
        return;
    if (values == NULL || values_at == NULL || name_at == NULL ||
        width < 2u || width > 4u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - first_location;
    storage = values_at(object, uniform_index);
    if (memcmp(storage, values, width * sizeof(*values)) == 0)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memcpy(previous_values, storage, width * sizeof(*storage));
    memcpy(storage, values, width * sizeof(*storage));
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment, type,
            name_at(object, uniform_index))) {
        memcpy(storage, previous_values, width * sizeof(*storage));
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

static int ringl_get_integer_vector_uniform(
    uint32_t program, int32_t location, int32_t* values_out, uint32_t width,
    uint32_t first_location, uint32_t uniform_count,
    RinGLIntegerVectorUniformValuesAt values_at)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t uniform_index;

    if (context == NULL || values_out == NULL || values_at == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - first_location;
    memcpy(values_out, values_at(object, uniform_index),
           width * sizeof(*values_out));
    return 0;
}

void ringl_uniform_2i(int32_t location, int32_t x, int32_t y)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    const int32_t values[2] = { x, y };

    if (context == NULL || location == -1)
        return;
    object = ringl_program_object(context, context->current_program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_set_integer_vector_uniform(location, values, 2u,
                                     ringl_ivec2_uniform_first_location(object),
                                     object->ivec2_uniform_count,
                                     RINGL_INT_VEC2,
                                     ringl_ivec2_uniform_values_at,
                                     ringl_ivec2_uniform_name_at);
}

void ringl_uniform_3i(int32_t location, int32_t x, int32_t y, int32_t z)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    const int32_t values[3] = { x, y, z };

    if (context == NULL || location == -1)
        return;
    object = ringl_program_object(context, context->current_program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_set_integer_vector_uniform(location, values, 3u,
                                     ringl_ivec3_uniform_first_location(object),
                                     object->ivec3_uniform_count,
                                     RINGL_INT_VEC3,
                                     ringl_ivec3_uniform_values_at,
                                     ringl_ivec3_uniform_name_at);
}

void ringl_uniform_4i(int32_t location, int32_t x, int32_t y, int32_t z,
                      int32_t w)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    const int32_t values[4] = { x, y, z, w };

    if (context == NULL || location == -1)
        return;
    object = ringl_program_object(context, context->current_program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_set_integer_vector_uniform(location, values, 4u,
                                     ringl_ivec4_uniform_first_location(object),
                                     object->ivec4_uniform_count,
                                     RINGL_INT_VEC4,
                                     ringl_ivec4_uniform_values_at,
                                     ringl_ivec4_uniform_name_at);
}

int ringl_get_uniform_2i(uint32_t program, int32_t location,
                          int32_t values_out[2])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL)
        return -1;
    object = ringl_program_object(context, program);
    return ringl_get_integer_vector_uniform(
        program, location, values_out, 2u,
        object == NULL ? 0u : ringl_ivec2_uniform_first_location(object),
        object == NULL ? 0u : object->ivec2_uniform_count,
        ringl_ivec2_uniform_values_at);
}

int ringl_get_uniform_3i(uint32_t program, int32_t location,
                          int32_t values_out[3])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL)
        return -1;
    object = ringl_program_object(context, program);
    return ringl_get_integer_vector_uniform(
        program, location, values_out, 3u,
        object == NULL ? 0u : ringl_ivec3_uniform_first_location(object),
        object == NULL ? 0u : object->ivec3_uniform_count,
        ringl_ivec3_uniform_values_at);
}

int ringl_get_uniform_4i(uint32_t program, int32_t location,
                          int32_t values_out[4])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL)
        return -1;
    object = ringl_program_object(context, program);
    return ringl_get_integer_vector_uniform(
        program, location, values_out, 4u,
        object == NULL ? 0u : ringl_ivec4_uniform_first_location(object),
        object == NULL ? 0u : object->ivec4_uniform_count,
        ringl_ivec4_uniform_values_at);
}

void ringl_uniform_4f(int32_t location, float x, float y, float z, float w)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLProgramVec4Uniform* uniform;
    float next_values[4] = { x, y, z, w };
    float previous_values[4];
    uint32_t uniform_index;

    if (context == NULL || location == -1)
        return;
    /* Keep the linked executable representable by the RSH1 lowering path.
     * Accepting NaN or infinity here would persist an invalid literal and
     * could make a later uniform-artifact rebuild fail after state changed. */
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(w)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < object->sampler_uniform_count +
                                  object->float_uniform_count +
                                  object->vec2_uniform_count +
                                  object->vec3_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->float_uniform_count +
                                  object->vec2_uniform_count +
                                  object->vec3_uniform_count +
                                  object->vec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count -
                    object->float_uniform_count - object->vec2_uniform_count -
                    object->vec3_uniform_count;
    uniform = &object->vec4_uniforms[uniform_index];
    if (memcmp(uniform->values, next_values, sizeof(next_values)) == 0)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memcpy(previous_values, uniform->values, sizeof(previous_values));
    memcpy(uniform->values, next_values, sizeof(next_values));
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment, RINGL_FLOAT_VEC4,
            uniform->name)) {
        memcpy(uniform->values, previous_values, sizeof(previous_values));
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

int ringl_get_uniform_4f(uint32_t program, int32_t location,
                          float values_out[4])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t uniform_index;

    if (context == NULL || values_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < object->sampler_uniform_count +
                                  object->float_uniform_count +
                                  object->vec2_uniform_count +
                                  object->vec3_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->float_uniform_count +
                                  object->vec2_uniform_count +
                                  object->vec3_uniform_count +
                                  object->vec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count -
                    object->float_uniform_count - object->vec2_uniform_count -
                    object->vec3_uniform_count;
    memcpy(values_out, object->vec4_uniforms[uniform_index].values,
           sizeof(object->vec4_uniforms[uniform_index].values));
    return 0;
}

void ringl_uniform_matrix4fv(int32_t location, uint32_t transpose,
                             const float values[16])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLProgramMat4Uniform* uniform;
    float previous_values[16];
    uint32_t first_location;
    uint32_t uniform_index;
    uint32_t index;

    if (context == NULL || location == -1)
        return;
    if (transpose != 0u || values == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    for (index = 0u; index < 16u; ++index) {
        if (!isfinite(values[index])) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    first_location = object == NULL ? 0u : object->sampler_uniform_count +
                                           object->float_uniform_count +
                                           object->vec2_uniform_count +
                                           object->vec3_uniform_count +
                                           object->vec4_uniform_count;
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + object->mat4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - first_location;
    uniform = &object->mat4_uniforms[uniform_index];
    if (memcmp(uniform->values, values, sizeof(uniform->values)) == 0)
        return;
    vertex = ringl_program_shader(context, object->linked_vertex_shader);
    fragment = ringl_program_shader(context, object->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memcpy(previous_values, uniform->values, sizeof(previous_values));
    memcpy(uniform->values, values, sizeof(uniform->values));
    if (!ringl_program_rebuild_uniform_for_name(
            context, object, vertex, fragment, RINGL_FLOAT_MAT4,
            uniform->name)) {
        memcpy(uniform->values, previous_values, sizeof(previous_values));
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

int ringl_get_uniform_matrix4f(uint32_t program, int32_t location,
                                float values_out[16])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    uint32_t first_location;
    uint32_t uniform_index;

    if (context == NULL || values_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    first_location = object == NULL ? 0u : object->sampler_uniform_count +
                                           object->float_uniform_count +
                                           object->vec2_uniform_count +
                                           object->vec3_uniform_count +
                                           object->vec4_uniform_count;
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < first_location ||
        (uint32_t)location >= first_location + object->mat4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - first_location;
    memcpy(values_out, object->mat4_uniforms[uniform_index].values,
           sizeof(object->mat4_uniforms[uniform_index].values));
    return 0;
}

void ringl_program_objects_destroy_all(RinGLContext* context)
{
    uint32_t index;
    if (context == NULL)
        return;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_PROGRAM ||
            context->objects[index].state == RINGL_OBJECT_FREE)
            continue;
        ringl_program_discard_uniform_artifacts(context,
                                                &context->programs[index]);
        memset(&context->programs[index], 0, sizeof(context->programs[index]));
    }
    context->current_program = 0u;
}
