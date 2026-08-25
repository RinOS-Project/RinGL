#include <assert.h>
#include <stdint.h>

#include <ringl/ringl.h>

int main(void)
{
    RinGLContext* context = NULL;
    uint32_t names[2] = {0u, 0u};
    uint32_t replacement = 0u;
    uint32_t stale;

    assert(ringl_context_create(NULL, &context) == 0);
    assert(context != NULL);
    assert(ringl_make_current(context) == 0);

    ringl_gen_buffers(2, names);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(names[0] != 0u);
    assert(names[1] != 0u);
    assert(names[0] != names[1]);
    assert(!ringl_is_buffer(names[0]));

    ringl_bind_buffer(RINGL_ARRAY_BUFFER, names[0]);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_is_buffer(names[0]));
    assert(ringl_get_bound_buffer(RINGL_ARRAY_BUFFER) == names[0]);

    ringl_bind_buffer(RINGL_ELEMENT_ARRAY_BUFFER, names[1]);
    assert(ringl_is_buffer(names[1]));
    assert(ringl_get_bound_buffer(RINGL_ELEMENT_ARRAY_BUFFER) == names[1]);

    stale = names[0];
    ringl_delete_buffers(1, &names[0]);
    assert(!ringl_is_buffer(stale));
    assert(ringl_get_bound_buffer(RINGL_ARRAY_BUFFER) == 0u);

    ringl_gen_buffers(1, &replacement);
    assert(replacement != 0u);
    assert(replacement != stale);

    ringl_bind_buffer(RINGL_ARRAY_BUFFER, stale);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(ringl_get_bound_buffer(RINGL_ARRAY_BUFFER) == 0u);

    ringl_bind_buffer(0xffffffffu, replacement);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_gen_buffers(-1, &replacement);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);

    ringl_delete_buffers(1, &replacement);
    ringl_delete_buffers(1, &names[1]);
    ringl_context_destroy(context);
    return 0;
}
