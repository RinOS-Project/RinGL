/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

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
    ringl_program_set_log(target, "");
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
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

    object->link_status = RINGL_TRUE;
    ringl_program_set_log(object, "");
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
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

uint64_t ringl_get_program_info_log(uint32_t program,
                                    char* buffer,
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
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
    }
}

uint32_t ringl_get_current_program(void)
{
    RinGLContext* context = ringl_get_current_context();
    return context == NULL ? 0u : context->current_program;
}
