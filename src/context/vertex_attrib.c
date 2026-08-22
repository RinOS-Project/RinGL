/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>

static RinGLVertexAttribState* ringl_vertex_attrib(RinGLContext* context,
                                                   uint32_t index)
{
    if (context == NULL || index >= RINGL_MAX_VERTEX_ATTRIBS)
        return NULL;
    return &context->vertex_attribs[index];
}

static uint32_t ringl_vertex_component_bytes(uint32_t type)
{
    if (type == RINGL_BYTE || type == RINGL_UNSIGNED_BYTE)
        return 1u;
    if (type == RINGL_SHORT || type == RINGL_UNSIGNED_SHORT)
        return 2u;
    if (type == RINGL_FLOAT)
        return 4u;
    return 0u;
}

static void ringl_vertex_attrib_set_current(RinGLContext* context,
                                            uint32_t index, float x, float y,
                                            float z, float w)
{
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    attrib->current_value[0] = x;
    attrib->current_value[1] = y;
    attrib->current_value[2] = z;
    attrib->current_value[3] = w;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

void ringl_enable_vertex_attrib_array(uint32_t index)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (!attrib->enabled) {
        attrib->enabled = RINGL_TRUE;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

void ringl_disable_vertex_attrib_array(uint32_t index)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (attrib->enabled) {
        attrib->enabled = RINGL_FALSE;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

void ringl_vertex_attrib_pointer(uint32_t index,
                                 int32_t size,
                                 uint32_t type,
                                 uint32_t normalized,
                                 int32_t stride,
                                 uint64_t offset)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (ringl_vertex_component_bytes(type) == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if ((size != 1 && size != 2 && size != 3 && size != 4) ||
        (normalized != RINGL_FALSE && normalized != RINGL_TRUE) ||
        stride < 0 || stride > 2048) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->array_buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((offset % ringl_vertex_component_bytes(type)) != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    attrib->size = (uint32_t)size;
    attrib->type = type;
    attrib->normalized = normalized;
    attrib->stride = (uint32_t)stride;
    attrib->buffer = context->array_buffer;
    attrib->offset = offset;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

void ringl_vertex_attrib1f(uint32_t index, float x)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x,
                                    0.0f, 0.0f, 1.0f);
}

void ringl_vertex_attrib2f(uint32_t index, float x, float y)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    0.0f, 1.0f);
}

void ringl_vertex_attrib3f(uint32_t index, float x, float y, float z)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    z, 1.0f);
}

void ringl_vertex_attrib4f(uint32_t index, float x, float y, float z,
                           float w)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    z, w);
}

int ringl_get_vertex_attrib(uint32_t index, RinGLVertexAttribInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL || info == NULL)
        return -1;
    if (info->struct_size < sizeof(*info) || info->api_version != RINGL_API_VERSION)
        return -1;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }

    info->enabled = attrib->enabled;
    info->size = attrib->size;
    info->type = attrib->type;
    info->normalized = attrib->normalized;
    info->stride = attrib->stride;
    info->buffer = attrib->buffer;
    info->offset = attrib->offset;
    return 0;
}

void ringl_vertex_attrib_detach_buffer(RinGLContext* context, uint32_t buffer)
{
    uint32_t index;

    if (context == NULL || buffer == 0u)
        return;
    for (index = 0; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
        RinGLVertexAttribState* attrib = &context->vertex_attribs[index];
        if (attrib->buffer != buffer)
            continue;
        attrib->buffer = 0u;
        attrib->offset = 0u;
        attrib->stride = 0u;
        attrib->enabled = RINGL_FALSE;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}
