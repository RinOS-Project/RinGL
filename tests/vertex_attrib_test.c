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
    context->buffers[slot_index].size_bytes = 24u;

    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 0, 0u);
    ringl_enable_vertex_attrib_array(0u);

    assert(ringl_get_vertex_attrib(0u, &info) == 0);
    assert(info.enabled == RINGL_TRUE);
    assert(info.size == 2u);
    assert(info.type == RINGL_FLOAT);
    assert(info.stride == 0u);
    assert(info.buffer == buffer);
    assert(info.offset == 0u);

    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.buffer == buffer);
    assert(layout.stride == 8u);
    assert(layout.attribute_count == 2u);
    assert(layout.attributes[0].location == 0u);
    assert(layout.attributes[0].format == RINGL_NATIVE_VERTEX_FLOAT32);
    assert(layout.attributes[0].offset == 0u);
    assert(layout.attributes[1].location == 1u);
    assert(layout.attributes[1].format == RINGL_NATIVE_VERTEX_FLOAT32);
    assert(layout.attributes[1].offset == 4u);

    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) == 0);
    assert(ringl_validate_vertex_fetch(context, 0u, 4u, &layout) != 0);
    assert(ringl_validate_vertex_fetch(context, UINT32_MAX, 2u, &layout) != 0);

    ringl_disable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 0u);
    ringl_vertex_attrib_pointer(1u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 4u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_enable_vertex_attrib_array(1u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.attribute_count == 2u);
    assert(layout.attributes[0].location == 0u);
    assert(layout.attributes[1].location == 1u);

    ringl_vertex_attrib_pointer(2u, 3, RINGL_FLOAT, RINGL_FALSE, 12, 0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_vertex_attrib_pointer(2u, 1, 0x1405u, RINGL_FALSE, 8, 0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    context->buffers[slot_index].size_bytes = 60u;
    ringl_disable_vertex_attrib_array(0u);
    ringl_disable_vertex_attrib_array(1u);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 20, 0u);
    ringl_vertex_attrib_pointer(1u, 3, RINGL_FLOAT, RINGL_FALSE, 20, 8u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_enable_vertex_attrib_array(1u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.stride == 20u);
    assert(layout.attribute_count == 5u);
    assert(layout.attributes[0].location == 0u);
    assert(layout.attributes[1].location == 1u);
    assert(layout.attributes[2].location == 2u);
    assert(layout.attributes[4].location == 4u);
    assert(layout.attributes[2].offset == 8u);
    assert(layout.attributes[4].offset == 16u);
    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) == 0);

    context->buffers[slot_index].size_bytes = 72u;
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 24, 0u);
    ringl_vertex_attrib_pointer(1u, 4, RINGL_FLOAT, RINGL_FALSE, 24, 8u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.stride == 24u);
    assert(layout.attribute_count == 6u);
    assert(layout.attributes[5].location == 5u);
    assert(layout.attributes[5].offset == 20u);
    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) == 0);

    ringl_delete_buffers(1, &buffer);
    assert(ringl_resolve_vertex_layout(context, &layout) != 0);

    ringl_context_destroy(context);
    return 0;
}
