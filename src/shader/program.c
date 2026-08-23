/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"
#include "glsl_parser.h"
#include "glsl_lower.h"

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
    for (index = 0u; index < program->vec4_uniform_count; ++index) {
        if (strcmp(program->vec4_uniforms[index].name, name) == 0)
            return 1;
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

static int ringl_program_lower_uniform_shader(
    RinGLContext* context, const RinGLShaderObject* shader,
    const RinGLGlslVec4UniformValue* uniforms, uint32_t uniform_count,
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
    if (ringl_glsl_lower_rsh1_with_vec4_uniforms(
            shader->shader_type, shader->source,
            (size_t)shader->source_length, uniforms, uniform_count,
            &lowered) != 0 || !lowered.ok || lowered.byte_size == 0u) {
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

/* Compile linked vec4 values into separate RSH1 modules. RinGPU executes the
 * generated CONST_F32 instructions directly; this is intentionally a
 * program-owned executable, not source replacement or a CPU draw shortcut. */
static int ringl_program_rebuild_uniform_artifacts(
    RinGLContext* context, RinGLProgramObject* program,
    const RinGLShaderObject* vertex, const RinGLShaderObject* fragment)
{
    RinGLGlslVec4UniformValue uniforms[RINGL_MAX_VEC4_UNIFORMS];
    uint8_t* vertex_rsh1 = NULL;
    uint8_t* fragment_rsh1 = NULL;
    uint32_t vertex_rsh1_size = 0u;
    uint32_t fragment_rsh1_size = 0u;
    uint64_t vertex_module = 0u;
    uint64_t fragment_module = 0u;
    uint32_t index;

    if (context == NULL || program == NULL || vertex == NULL ||
        fragment == NULL || program->vec4_uniform_count == 0u ||
        program->vec4_uniform_count > RINGL_MAX_VEC4_UNIFORMS)
        return 0;
    memset(uniforms, 0, sizeof(uniforms));
    for (index = 0u; index < program->vec4_uniform_count; ++index) {
        uniforms[index].name = program->vec4_uniforms[index].name;
        memcpy(uniforms[index].values, program->vec4_uniforms[index].values,
               sizeof(uniforms[index].values));
    }
    if (!ringl_program_lower_uniform_shader(
            context, vertex, uniforms, program->vec4_uniform_count,
            &vertex_rsh1, &vertex_rsh1_size, &vertex_module) ||
        !ringl_program_lower_uniform_shader(
            context, fragment, uniforms, program->vec4_uniform_count,
            &fragment_rsh1, &fragment_rsh1_size, &fragment_module)) {
        if (vertex_module != 0u)
            ringl_backend_destroy_object(context, vertex_module);
        if (fragment_module != 0u)
            ringl_backend_destroy_object(context, fragment_module);
        free(vertex_rsh1);
        free(fragment_rsh1);
        return 0;
    }

    ringl_invalidate_graphics_artifacts(context);
    ringl_program_discard_uniform_artifacts(context, program);
    program->vertex_uniform_rsh1 = vertex_rsh1;
    program->fragment_uniform_rsh1 = fragment_rsh1;
    program->vertex_uniform_rsh1_size = vertex_rsh1_size;
    program->fragment_uniform_rsh1_size = fragment_rsh1_size;
    program->vertex_uniform_module = vertex_module;
    program->fragment_uniform_module = fragment_module;
    return 1;
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
    object->vec4_uniform_count = 0u;
    object->varying_count = 0u;
    memset(object->attributes, 0, sizeof(object->attributes));
    memset(object->sampler_uniforms, 0, sizeof(object->sampler_uniforms));
    memset(object->vec4_uniforms, 0, sizeof(object->vec4_uniforms));
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
    if (!ringl_program_collect_vec4_uniforms(object, vertex, fragment)) {
        ringl_program_set_log(object,
                              "invalid or too many active vec4 uniforms");
        return;
    }
    if (!ringl_program_collect_varyings(object, vertex, fragment)) {
        ringl_program_set_log(object, "vertex/fragment varying interface mismatch");
        return;
    }
    if (object->vec4_uniform_count == 0u && context->has_ringpu_ops &&
        context->ringpu_ops.create_shader_module != NULL) {
        if (!ringl_program_prepare_gpu_shader(context, object->vertex_shader, vertex)) {
            ringl_program_set_log(object, "vertex shader failed RinGPU validation");
            return;
        }
        if (!ringl_program_prepare_gpu_shader(context, object->fragment_shader, fragment)) {
            ringl_program_set_log(object, "fragment shader failed RinGPU validation");
            return;
        }
    }
    if (object->vec4_uniform_count != 0u &&
        !ringl_program_rebuild_uniform_artifacts(context, object, vertex,
                                                 fragment)) {
        ringl_program_set_log(object,
                              "vec4 uniform shader is outside the RinGPU lowering profile");
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
                                      object->vec4_uniform_count;
    }
    *info = result;
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
    if (index >= object->sampler_uniform_count + object->vec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (index < object->sampler_uniform_count) {
        ringl_active_info_set(&result, RINGL_SAMPLER_2D,
                              object->sampler_uniforms[index].name);
    } else {
        ringl_active_info_set(&result, RINGL_FLOAT_VEC4,
                              object->vec4_uniforms[
                                  index - object->sampler_uniform_count].name);
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
    for (i = 0u; i < object->vec4_uniform_count; ++i) {
        if (strcmp(object->vec4_uniforms[i].name, name) == 0)
            return (int32_t)(object->sampler_uniform_count + i);
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

int ringl_get_uniform_1i(uint32_t program, int32_t location,
                          int32_t* value_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLProgramObject* object;

    if (context == NULL || value_out == NULL)
        return -1;
    object = ringl_program_object(context, program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location >= object->sampler_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    *value_out = object->sampler_uniforms[location].texture_unit;
    return 0;
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
    if (context->current_program == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    object = ringl_program_object(context, context->current_program);
    if (object == NULL || !object->link_status || location < 0 ||
        (uint32_t)location < object->sampler_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->vec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count;
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
    if (!ringl_program_rebuild_uniform_artifacts(context, object, vertex,
                                                 fragment)) {
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
        (uint32_t)location < object->sampler_uniform_count ||
        (uint32_t)location >= object->sampler_uniform_count +
                                  object->vec4_uniform_count) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    uniform_index = (uint32_t)location - object->sampler_uniform_count;
    memcpy(values_out, object->vec4_uniforms[uniform_index].values,
           sizeof(object->vec4_uniforms[uniform_index].values));
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
