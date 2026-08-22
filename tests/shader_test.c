/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

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

    ringl_shader_source(vertex, "void main(){}", -1);
    assert(ringl_get_shader_source_length(vertex) == 13u);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_shader_source(vertex, "abcXYZ", 3);
    assert(ringl_get_shader_source_length(vertex) == 3u);
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

    ringl_delete_shader(vertex);
    assert(!ringl_is_shader(vertex));
    ringl_delete_shader(fragment);

    ringl_context_destroy(context);
    return 0;
}
