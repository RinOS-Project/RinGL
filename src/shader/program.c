/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_parser.h"

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
    if (context == NULL || program == 0u)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL)
        return;
    if (context->current_program == program)
        context->current_program = 0u;
    memset(object, 0, sizeof(*object));
    ringl_object_release(context, program, RINGL_OBJECT_PROGRAM);
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
    if (target == NULL || source == NULL) {
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
    target->link_status = RINGL_FALSE;
    target->sampler_uniform_count = 0u;
    target->varying_count = 0u;
    memset(target->sampler_uniforms, 0, sizeof(target->sampler_uniforms));
    memset(target->varyings, 0, sizeof(target->varyings));
    ringl_program_set_log(target, "");
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
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

void ringl_link_program(uint32_t program)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    if (context == NULL)
        return;
    object = ringl_program_object(context, program);
    if (object == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    object->link_status = RINGL_FALSE;
    object->sampler_uniform_count = 0u;
    object->varying_count = 0u;
    memset(object->sampler_uniforms, 0, sizeof(object->sampler_uniforms));
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
    if (!ringl_program_collect_sampler_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object, "too many active sampler uniforms");
        return;
    }
    if (!ringl_program_collect_varyings(object, vertex, fragment)) {
        ringl_program_set_log(object, "vertex/fragment varying interface mismatch");
        return;
    }
    if (context->has_ringpu_ops && context->ringpu_ops.create_shader_module != NULL) {
        if (!ringl_program_prepare_gpu_shader(context, object->vertex_shader, vertex)) {
            ringl_program_set_log(object, "vertex shader failed RinGPU validation");
            return;
        }
        if (!ringl_program_prepare_gpu_shader(context, object->fragment_shader, fragment)) {
            ringl_program_set_log(object, "fragment shader failed RinGPU validation");
            return;
        }
    }
    object->link_status = RINGL_TRUE;
    ringl_program_set_log(object, "");
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
    return -1;
}

void ringl_uniform_1i(int32_t location, int32_t value)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;
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
    if (location < 0 || (uint32_t)location >= object->sampler_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (object->sampler_uniforms[location].texture_unit != value) {
        object->sampler_uniforms[location].texture_unit = value;
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
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
        memset(&context->programs[index], 0, sizeof(context->programs[index]));
    }
    context->current_program = 0u;
}
