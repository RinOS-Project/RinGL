/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include "ringl_internal.h"

#include <assert.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    RinGLVertexAttribInfoV1 info = {
        .struct_size = sizeof(info),
        .api_version = RINGL_API_VERSION,
    };
    RinGLResolvedVertexLayout layout;
    uint32_t buffer = 0u;
    uint32_t slot_index;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    ringl_gen_buffers(1, &buffer);
    assert(buffer != 0u);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);
    assert(ringl_is_buffer(buffer));

    slot_index = ringl_object_slot_index(buffer);
    assert(slot_index < RINGL_OBJECT_SLOT_COUNT);
    context->buffers[slot_index].size_bytes = 16u;

    ringl_vertex_attrib_pointer(0u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 0u);
    ringl_vertex_attrib_pointer(1u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 4u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_enable_vertex_attrib_array(1u);

    assert(ringl_get_vertex_attrib(1u, &info) == 0);
    assert(info.enabled == RINGL_TRUE);
    assert(info.size == 1u);
    assert(info.type == RINGL_FLOAT);
    assert(info.stride == 8u);
    assert(info.buffer == buffer);
    assert(info.offset == 4u);

    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.buffer == buffer);
    assert(layout.stride == 8u);
    assert(layout.attribute_count == 2u);
    assert(layout.attributes[0].location == 0u);
    assert(layout.attributes[0].format == RINGL_NATIVE_VERTEX_FLOAT32);
    assert(layout.attributes[0].offset == 0u);
    assert(layout.attributes[1].location == 1u);
    assert(layout.attributes[1].offset == 4u);

    assert(ringl_validate_vertex_fetch(context, 0u, 2u, &layout) == 0);
    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) != 0);
    assert(ringl_validate_vertex_fetch(context, UINT32_MAX, 2u, &layout) != 0);

    ringl_vertex_attrib_pointer(2u, 2, RINGL_FLOAT, RINGL_FALSE, 8, 0u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_vertex_attrib_pointer(2u, 1, 0x1405u, RINGL_FALSE, 8, 0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_disable_vertex_attrib_array(1u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.attribute_count == 1u);

    ringl_delete_buffers(1, &buffer);
    assert(ringl_resolve_vertex_layout(context, &layout) != 0);

    ringl_context_destroy(context);
    return 0;
}
