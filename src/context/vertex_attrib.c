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
    if (type != RINGL_FLOAT) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if ((size != 1 && size != 2) || normalized != RINGL_FALSE ||
        stride < 0 || stride > 2048) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->array_buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((offset & UINT64_C(3)) != 0u || (stride != 0 && (stride & 3) != 0)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    attrib->size = (uint32_t)size;
    attrib->type = type;
    attrib->normalized = RINGL_FALSE;
    attrib->stride = (uint32_t)stride;
    attrib->buffer = context->array_buffer;
    attrib->offset = offset;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
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
