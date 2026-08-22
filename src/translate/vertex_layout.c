/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <string.h>

int ringl_resolve_vertex_layout(const RinGLContext* context,
                                RinGLResolvedVertexLayout* layout)
{
    uint32_t index;
    uint32_t common_buffer = 0u;
    uint32_t common_stride = 0u;
    uint32_t scalar_location = 0u;

    if (context == NULL || layout == NULL)
        return -1;
    memset(layout, 0, sizeof(*layout));

    for (index = 0; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
        const RinGLVertexAttribState* attrib = &context->vertex_attribs[index];
        uint32_t effective_stride;
        uint32_t component;

        if (!attrib->enabled)
            continue;
        if (attrib->buffer == 0u || (attrib->size != 1u && attrib->size != 2u) ||
            attrib->type != RINGL_FLOAT || attrib->normalized != RINGL_FALSE)
            return -1;
        if (ringl_object_lookup_const(context, attrib->buffer,
                                      RINGL_OBJECT_BUFFER) == NULL)
            return -1;
        if (attrib->offset > UINT32_MAX)
            return -1;
        if ((uint64_t)attrib->offset + (uint64_t)attrib->size * 4u > UINT32_MAX)
            return -1;

        effective_stride = attrib->stride == 0u ? attrib->size * 4u
                                                 : attrib->stride;
        if (effective_stride > 2048u || (effective_stride & 3u) != 0u ||
            effective_stride < attrib->size * 4u)
            return -1;

        if (common_buffer == 0u) {
            common_buffer = attrib->buffer;
            common_stride = effective_stride;
        } else if (common_buffer != attrib->buffer ||
                   common_stride != effective_stride) {
            return -1;
        }

        if (layout->attribute_count + attrib->size > RINGL_MAX_VERTEX_ATTRIBS)
            return -1;
        for (component = 0u; component < attrib->size; ++component) {
            RinGLResolvedVertexAttribute* resolved =
                &layout->attributes[layout->attribute_count++];
            resolved->location = scalar_location++;
            resolved->format = RINGL_NATIVE_VERTEX_FLOAT32;
            resolved->offset = (uint32_t)attrib->offset + component * 4u;
        }
    }

    layout->buffer = common_buffer;
    layout->stride = common_stride;
    return 0;
}

int ringl_validate_vertex_fetch(const RinGLContext* context,
                                uint32_t first_vertex,
                                uint32_t vertex_count,
                                RinGLResolvedVertexLayout* layout)
{
    uint32_t slot_index;
    uint32_t index;
    uint64_t last_vertex;
    const RinGLBufferObject* buffer;

    if (ringl_resolve_vertex_layout(context, layout) != 0)
        return -1;
    if (vertex_count == 0u || layout->attribute_count == 0u)
        return 0;
    if (layout->buffer == 0u || layout->stride == 0u)
        return -1;

    last_vertex = (uint64_t)first_vertex + (uint64_t)vertex_count - 1u;
    if (last_vertex > UINT32_MAX)
        return -1;

    slot_index = ringl_object_slot_index(layout->buffer);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    buffer = &context->buffers[slot_index];
    if (buffer->size_bytes == 0u)
        return -1;

    for (index = 0; index < layout->attribute_count; ++index) {
        const RinGLResolvedVertexAttribute* attrib = &layout->attributes[index];
        uint64_t stride_bytes;
        uint64_t end;

        if (last_vertex != 0u &&
            (uint64_t)layout->stride >
                (UINT64_MAX - (uint64_t)attrib->offset - 4u) / last_vertex) {
            return -1;
        }
        stride_bytes = last_vertex * (uint64_t)layout->stride;
        end = (uint64_t)attrib->offset + stride_bytes + 4u;
        if (end > buffer->size_bytes)
            return -1;
    }
    return 0;
}
