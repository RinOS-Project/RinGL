/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include "ringl_internal.h"

#include <assert.h>
#include <string.h>

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
    uint32_t second_buffer = 0u;
    uint32_t slot_index;
    uint32_t second_slot_index;
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    float current_value[4] = {0.0f, 0.0f, 0.0f, 0.0f};

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

    /* first_instance + instance_count - 1 may legally equal UINT32_MAX.
     * The divisor keeps the resolved element inside this small test buffer;
     * only the boundary arithmetic is under test. */
    ringl_vertex_attrib_divisor(0u, UINT32_MAX);
    assert(ringl_validate_vertex_fetch_instanced(
               context, 0u, 1u, UINT32_MAX, 1u, &layout) == 0);
    ringl_vertex_attrib_divisor(0u, 0u);

    ringl_disable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 0u);
    ringl_vertex_attrib_pointer(1u, 1, RINGL_FLOAT, RINGL_FALSE, 8, 4u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_enable_vertex_attrib_array(1u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.attribute_count == 2u);
    assert(layout.attributes[0].location == 0u);
    assert(layout.attributes[1].location == 1u);

    /* Enabled arrays may independently select their captured buffer and
     * effective stride. The V1 compatibility fields are deliberately zero
     * when this layout needs the multi-binding backend contract. */
    ringl_gen_buffers(1, &second_buffer);
    assert(second_buffer != 0u);
    second_slot_index = ringl_object_slot_index(second_buffer);
    assert(second_slot_index < RINGL_OBJECT_SLOT_COUNT);
    context->buffers[second_slot_index].size_bytes = 12u;
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, second_buffer);
    ringl_vertex_attrib_pointer(1u, 1, RINGL_FLOAT, RINGL_FALSE, 4, 0u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.buffer == 0u && layout.stride == 0u);
    assert(layout.binding_count == 2u);
    assert(layout.bindings[0].buffer == buffer &&
           layout.bindings[0].stride == 8u);
    assert(layout.bindings[1].buffer == second_buffer &&
           layout.bindings[1].stride == 4u);
    assert(layout.attributes[0].binding == 0u);
    assert(layout.attributes[1].binding == 1u);
    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) == 0);
    assert(ringl_validate_vertex_fetch(context, 0u, 4u, &layout) != 0);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);

    ringl_vertex_attrib_pointer(2u, 3, RINGL_FLOAT, RINGL_FALSE, 12, 0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_vertex_attrib_pointer(2u, 1, 0x1405u, RINGL_FALSE, 8, 0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    /* WebGL 1 vertex input types are forwarded as scalar RinGPU inputs.
     * Byte strides are intentionally not forced to Float32 alignment. */
    ringl_disable_vertex_attrib_array(0u);
    ringl_disable_vertex_attrib_array(1u);
    context->buffers[slot_index].size_bytes = 12u;
    ringl_vertex_attrib_pointer(0u, 2, RINGL_BYTE, RINGL_TRUE, 3, 0u);
    ringl_enable_vertex_attrib_array(0u);
    assert(ringl_get_vertex_attrib(0u, &info) == 0);
    assert(info.type == RINGL_BYTE && info.normalized == RINGL_TRUE &&
           info.stride == 3u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.stride == 3u && layout.attribute_count == 2u);
    assert(layout.attributes[0].format == RINGL_NATIVE_VERTEX_SNORM8 &&
           layout.attributes[0].offset == 0u &&
           layout.attributes[1].format == RINGL_NATIVE_VERTEX_SNORM8 &&
           layout.attributes[1].offset == 1u);
    assert(ringl_validate_vertex_fetch(context, 0u, 4u, &layout) == 0);

    ringl_vertex_attrib_pointer(0u, 1, RINGL_UNSIGNED_BYTE, RINGL_FALSE,
                                3, 0u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.attributes[0].format == RINGL_NATIVE_VERTEX_UINT8);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_SHORT, RINGL_TRUE, 6, 2u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.stride == 6u &&
           layout.attributes[0].format == RINGL_NATIVE_VERTEX_SNORM16 &&
           layout.attributes[0].offset == 2u);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_UNSIGNED_SHORT, RINGL_FALSE,
                                6, 2u);
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.attributes[0].format == RINGL_NATIVE_VERTEX_UINT16);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_SHORT, RINGL_FALSE, 2, 1u);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

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

    /* A linked program must consume disabled arrays as WebGL generic values,
     * not as missing buffer pointers. The default vector is (0, 0, 0, 1),
     * and the scalar RSH1/RinGPU interface carries its exact float bits. */
    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(vertex,
                        "attribute vec4 position; void main() { gl_Position = position; }",
                        -1);
    ringl_shader_source(fragment,
                        "void main() { gl_FragColor = vec4(1.0); }", -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    ringl_use_program(program);
    ringl_disable_vertex_attrib_array(0u);
    ringl_vertex_attrib3f(0u, -1.5f, 2.25f, 3.5f);
    assert(ringl_get_vertex_attrib_current(0u, current_value) == 0);
    assert(current_value[0] == -1.5f && current_value[1] == 2.25f &&
           current_value[2] == 3.5f && current_value[3] == 1.0f);
    assert(ringl_get_vertex_attrib_current(RINGL_MAX_VERTEX_ATTRIBS,
                                           current_value) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(current_value[0] == -1.5f && current_value[1] == 2.25f &&
           current_value[2] == 3.5f && current_value[3] == 1.0f);
    {
        int32_t integer_value = -77;
        float float_values[4] = {-77.0f, -77.0f, -77.0f, -77.0f};

        assert(ringl_get_vertex_attribiv_bounded(
                   0u, RINGL_VERTEX_ATTRIB_ARRAY_ENABLED, &integer_value,
                   1u) == 0);
        assert(integer_value == 0);
        assert(ringl_get_vertex_attribiv_bounded(
                   0u, RINGL_VERTEX_ATTRIB_ARRAY_SIZE, &integer_value,
                   1u) == 0);
        assert(integer_value == 2);
        assert(ringl_get_vertex_attribfv_bounded(
                   0u, RINGL_CURRENT_VERTEX_ATTRIB, float_values, 4u) == 0);
        assert(float_values[0] == -1.5f && float_values[1] == 2.25f &&
               float_values[2] == 3.5f && float_values[3] == 1.0f);
        float_values[0] = -77.0f;
        assert(ringl_get_vertex_attribfv_bounded(
                   0u, RINGL_CURRENT_VERTEX_ATTRIB, float_values, 3u) == -1);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(float_values[0] == -77.0f);
        assert(ringl_get_vertex_attribiv_bounded(
                   0u, RINGL_VERTEX_ATTRIB_ARRAY_POINTER, &integer_value,
                   1u) == -1);
        assert(ringl_get_error() == RINGL_INVALID_ENUM);
    }
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.buffer == 0u && layout.stride == 0u);
    assert(layout.attribute_count == 4u &&
           layout.has_constant_attributes == RINGL_TRUE);
    for (uint32_t component = 0u; component < 4u; ++component) {
        float value = 0.0f;
        assert(layout.attributes[component].format ==
               RINGL_NATIVE_VERTEX_FLOAT32);
        assert(layout.attributes[component].flags ==
               RINGL_RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32);
        memcpy(&value, &layout.attributes[component].offset, sizeof(value));
        if (component == 0u) assert(value == -1.5f);
        if (component == 1u) assert(value == 2.25f);
        if (component == 2u) assert(value == 3.5f);
        if (component == 3u) assert(value == 1.0f);
    }
    assert(ringl_validate_vertex_fetch(context, 0u, 3u, &layout) == 0);

    ringl_delete_buffers(1, &buffer);
    /* Deleting an unrelated buffer cannot invalidate a disabled generic
     * attribute: its current value is independent of vertex storage. */
    assert(ringl_resolve_vertex_layout(context, &layout) == 0);
    assert(layout.buffer == 0u && layout.has_constant_attributes == RINGL_TRUE);

    ringl_delete_buffers(1, &second_buffer);
    ringl_context_destroy(context);
    return 0;
}
