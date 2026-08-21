/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include "ringl_internal.h"

#include <assert.h>

int main(void)
{
    RinGLContext* first = NULL;
    RinGLContext* second = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };

    assert(ringl_context_create(&desc, &first) == 0);
    assert(ringl_context_create(&desc, &second) == 0);
    assert(first != NULL);
    assert(second != NULL);
    assert(first != second);
    assert(ringl_context_dirty_bits(first) == RINGL_DIRTY_ALL);

    assert(ringl_make_current(first) == 0);
    assert(ringl_get_current_context() == first);

    ringl_context_record_error(first, RINGL_INVALID_ENUM);
    ringl_context_record_error(first, RINGL_OUT_OF_MEMORY);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_clear_dirty(first, RINGL_DIRTY_PIPELINE);
    assert((ringl_context_dirty_bits(first) & RINGL_DIRTY_PIPELINE) == 0u);
    ringl_context_mark_dirty(first, RINGL_DIRTY_PIPELINE);
    assert((ringl_context_dirty_bits(first) & RINGL_DIRTY_PIPELINE) != 0u);

    assert(ringl_make_current(second) == 0);
    assert(ringl_get_current_context() == second);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_destroy(second);
    assert(ringl_get_current_context() == NULL);

    ringl_context_destroy(first);
    return 0;
}
