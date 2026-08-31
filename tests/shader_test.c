/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>
#include "../src/ringl_internal.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t vertex;
    uint32_t fragment;
    int32_t query_value = -1;
    RinGLShaderPrecisionFormatV1 precision = {
        .struct_size = sizeof(precision),
        .api_version = RINGL_API_VERSION,
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    assert(ringl_create_shader(0xdeadbeefu) == 0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    assert(vertex != 0u);
    assert(fragment != 0u);
    assert(ringl_is_shader(vertex));
    assert(ringl_get_shader_type(vertex) == RINGL_VERTEX_SHADER);
    assert(ringl_get_shader_parameteriv_bounded(
               vertex, RINGL_SHADER_TYPE, &query_value, 1u) == 0);
    assert(query_value == (int32_t)RINGL_VERTEX_SHADER);
    query_value = 123;
    assert(ringl_get_shader_parameteriv_bounded(
               vertex, RINGL_SHADER_TYPE, &query_value, 0u) == -1);
    assert(query_value == 123);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);

    assert(ringl_get_shader_precision_format(RINGL_VERTEX_SHADER,
                                             RINGL_HIGH_FLOAT,
                                             &precision) == 0);
    assert(precision.range_min == -126 && precision.range_max == 127 &&
           precision.precision == 23);
    assert(ringl_get_shader_precision_format(RINGL_FRAGMENT_SHADER,
                                             RINGL_MEDIUM_INT,
                                             &precision) == 0);
    assert(precision.range_min == 31 && precision.range_max == 30 &&
           precision.precision == 0);
    precision.range_min = 777;
    assert(ringl_get_shader_precision_format(0xdeadbeefu, RINGL_HIGH_FLOAT,
                                             &precision) == -1);
    assert(precision.range_min == 777);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    precision.reserved0 = 1u;
    assert(ringl_get_shader_precision_format(RINGL_VERTEX_SHADER,
                                             RINGL_HIGH_FLOAT,
                                             &precision) == -1);
    assert(precision.range_min == 777);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    precision.reserved0 = 0u;

    ringl_shader_source(vertex, "void main(){}", -1);
    assert(ringl_get_shader_source_length(vertex) == 13u);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_shader_source(vertex, "abcXYZ", 3);
    assert(ringl_get_shader_source_length(vertex) == 3u);
    assert(ringl_get_shader_parameteriv_bounded(
               vertex, RINGL_SHADER_SOURCE_LENGTH, &query_value, 1u) == 0);
    assert(query_value == 4);
    {
        char complete[8] = { 0 };
        char truncated[3] = { 0 };
        char untouched[4] = { 'x', 'y', 'z', '\0' };

        assert(ringl_copy_shader_source(vertex, complete, sizeof(complete)) ==
               3u);
        assert(strcmp(complete, "abc") == 0);
        assert(ringl_copy_shader_source(vertex, truncated,
                                        sizeof(truncated)) == 3u);
        assert(strcmp(truncated, "ab") == 0);
        assert(ringl_copy_shader_source(vertex, NULL, 0u) == 3u);
        assert(ringl_copy_shader_source(0u, untouched, sizeof(untouched)) ==
               0u);
        assert(strcmp(untouched, "xyz") == 0);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
    }

    ringl_shader_source(vertex, NULL, -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_shader_source_length(vertex) == 3u);

    /* Source snapshots are browser-owned shader input and participate in the
     * same per-context budget.  A full reservation must reject replacement
     * before malloc while preserving the previous source. */
    const uint64_t available_shadow_budget =
        RINGL_MAX_CPU_SHADOW_BYTES - context->cpu_shadow_bytes;
    assert(ringl_context_reserve_shadow_bytes(
               context, available_shadow_budget) != 0);
    ringl_shader_source(vertex, "replacement", -1);
    assert(ringl_get_error() == RINGL_OUT_OF_MEMORY);
    assert(ringl_get_shader_source_length(vertex) == 3u);
    ringl_context_release_shadow_bytes(context, available_shadow_budget);
    ringl_shader_source(vertex, "replacement", -1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_shader_source_length(vertex) == 11u);

    /* Parser reflection and its diagnostic buffer are temporary workspace,
     * not an uncharged stack escape. A full reservation rejects compilation
     * before publication and the same shader compiles after the reservation
     * is released. */
    ringl_shader_source(vertex,
                        "attribute vec2 position; void main() { gl_Position = vec4(position, 0.0, 1.0); }",
                        -1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        const uint64_t available_shadow_budget =
            RINGL_MAX_CPU_SHADOW_BYTES - context->cpu_shadow_bytes;
        assert(ringl_context_reserve_shadow_bytes(
                   context, available_shadow_budget) != 0);
        ringl_compile_shader(vertex);
        assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
        assert(ringl_get_error() == RINGL_OUT_OF_MEMORY);
        assert(context->cpu_shadow_bytes == RINGL_MAX_CPU_SHADOW_BYTES);
        ringl_context_release_shadow_bytes(context, available_shadow_budget);
    }
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_parameteriv_bounded(
               vertex, RINGL_COMPILE_STATUS, &query_value, 1u) == 0);
    assert(query_value == (int32_t)RINGL_TRUE);
    assert(ringl_get_shader_parameteriv_bounded(
               vertex, RINGL_INFO_LOG_LENGTH, &query_value, 1u) == 0);
    assert(query_value == 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_delete_shader(vertex);
    assert(!ringl_is_shader(vertex));
    ringl_delete_shader(fragment);

    ringl_context_destroy(context);
    return 0;
}
