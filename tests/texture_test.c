/* SPDX-License-Identifier: MIT */
#include <assert.h>

#include <ringl/ringl.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t textures[2] = {0u, 0u};
    uint32_t recycled = 0u;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);

    ringl_gen_textures(2, textures);
    assert(textures[0] != 0u && textures[1] != 0u);
    assert(textures[0] != textures[1]);
    assert(!ringl_is_texture(textures[0]));

    ringl_bind_texture(RINGL_TEXTURE_2D, textures[0]);
    assert(ringl_is_texture(textures[0]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[0]);

    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0 + 1u);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    ringl_bind_texture(RINGL_TEXTURE_2D, textures[1]);
    assert(ringl_is_texture(textures[1]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[1]);

    ringl_active_texture(RINGL_TEXTURE0);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[0]);

    ringl_delete_textures(1, &textures[0]);
    assert(!ringl_is_texture(textures[0]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[1]);

    ringl_active_texture(RINGL_TEXTURE0 + RINGL_MAX_TEXTURE_UNITS);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0 + 1u);

    ringl_bind_texture(0x1234u, textures[1]);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_gen_textures(1, &recycled);
    assert(recycled != 0u);
    assert(recycled != textures[0]);
    ringl_bind_texture(RINGL_TEXTURE_2D, recycled);
    assert(ringl_is_texture(recycled));

    ringl_delete_textures(1, &textures[1]);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == recycled);
    ringl_delete_textures(1, &recycled);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);

    ringl_context_destroy(context);
    return 0;
}
