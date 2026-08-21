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

    if (context == NULL || layout == NULL)
        return -1;
    memset(layout, 0, sizeof(*layout));

    for (index = 0; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
        const RinGLVertexAttribState* attrib = &context->vertex_attribs[index];
        uint32_t effective_stride;
        RinGLResolvedVertexAttribute* resolved;

        if (!attrib->enabled)
            continue;
        if (attrib->buffer == 0u || attrib->size != 1u ||
            attrib->type != RINGL_FLOAT || attrib->normalized != RINGL_FALSE)
            return -1;
        if (ringl_object_lookup_const(context, attrib->buffer,
                                      RINGL_OBJECT_BUFFER) == NULL)
            return -1;
        if (attrib->offset > UINT32_MAX)
            return -1;

        effective_stride = attrib->stride == 0u ? 4u : attrib->stride;
        if (effective_stride > 2048u || (effective_stride & 3u) != 0u)
            return -1;

        if (common_buffer == 0u) {
            common_buffer = attrib->buffer;
            common_stride = effective_stride;
        } else if (common_buffer != attrib->buffer ||
                   common_stride != effective_stride) {
            return -1;
        }

        if (layout->attribute_count >= RINGL_MAX_VERTEX_ATTRIBS)
            return -1;
        resolved = &layout->attributes[layout->attribute_count++];
        resolved->location = index;
        resolved->format = RINGL_NATIVE_VERTEX_FLOAT32;
        resolved->offset = (uint32_t)attrib->offset;
    }

    layout->buffer = common_buffer;
    layout->stride = common_stride;
    return 0;
}
