/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include <assert.h>

static RinGLVertexAttribInfoV1 vertex_attrib_info(void)
{
    RinGLVertexAttribInfoV1 info = {
        .struct_size = sizeof(info),
        .api_version = RINGL_API_VERSION,
    };

    assert(ringl_get_vertex_attrib(0u, &info) == 0);
    return info;
}

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t default_buffer = 0u;
    uint32_t array_buffer = 0u;
    uint32_t array = 0u;
    RinGLVertexAttribInfoV1 info;
    float current[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    /* The default VAO is valid state but has no object name. */
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_FALSE && info.size == 4u &&
           info.type == RINGL_FLOAT && info.buffer == 0u);
    assert(ringl_get_vertex_attrib_divisor(0u) == 0u);
    ringl_vertex_attrib4f(0u, 1.0f, 2.0f, 3.0f, 4.0f);

    ringl_gen_buffers(1, &default_buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, default_buffer);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 8, 0u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_divisor(0u, 3u);
    ringl_bind_buffer(RINGL_ELEMENT_ARRAY_BUFFER, default_buffer);

    ringl_gen_vertex_arrays(1, &array);
    assert(array != 0u && ringl_is_vertex_array(array) == 0);
    ringl_bind_vertex_array(array);
    assert(ringl_get_bound_vertex_array() == array &&
           ringl_is_vertex_array(array) != 0);
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_FALSE && info.size == 4u &&
           info.type == RINGL_FLOAT && info.buffer == 0u);
    assert(ringl_get_vertex_attrib_divisor(0u) == 0u);
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == 0u);
    assert(ringl_get_vertex_attrib_current(0u, current) == 0);
    assert(current[0] == 1.0f && current[1] == 2.0f &&
           current[2] == 3.0f && current[3] == 4.0f);

    ringl_gen_buffers(1, &array_buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, array_buffer);
    ringl_vertex_attrib_pointer(0u, 3, RINGL_FLOAT, RINGL_FALSE, 12, 4u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_divisor(0u, 2u);
    ringl_bind_buffer(RINGL_ELEMENT_ARRAY_BUFFER, array_buffer);

    ringl_bind_vertex_array(0u);
    assert(ringl_get_bound_vertex_array() == 0u);
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_TRUE && info.size == 2u &&
           info.buffer == default_buffer && info.offset == 0u);
    assert(ringl_get_vertex_attrib_divisor(0u) == 3u);
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == default_buffer);
    assert(ringl_get_vertex_attrib_current(0u, current) == 0);
    assert(current[0] == 1.0f && current[1] == 2.0f &&
           current[2] == 3.0f && current[3] == 4.0f);

    ringl_bind_vertex_array(array);
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_TRUE && info.size == 3u &&
           info.buffer == array_buffer && info.offset == 4u);
    assert(ringl_get_vertex_attrib_divisor(0u) == 2u);
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == array_buffer);

    /* Buffer deletion must invalidate active and inactive VAO references,
     * preventing a recycled object name from becoming an implicit source. */
    ringl_delete_buffers(1, &array_buffer);
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_FALSE && info.buffer == 0u);
    assert(ringl_get_vertex_attrib_divisor(0u) == 2u);
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == 0u);
    ringl_bind_vertex_array(0u);
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == default_buffer);
    ringl_bind_vertex_array(array);
    info = vertex_attrib_info();
    assert(info.enabled == RINGL_FALSE && info.buffer == 0u);

    ringl_delete_vertex_arrays(1, &array);
    assert(ringl_get_bound_vertex_array() == 0u && ringl_is_vertex_array(array) == 0);
    ringl_bind_vertex_array(array);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    ringl_delete_buffers(1, &default_buffer);
    ringl_context_destroy(context);
    return 0;
}
