/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <string.h>

static int framebuffer_valid(const RinGLDefaultFramebufferV1* framebuffer)
{
    if (framebuffer == NULL)
        return 0;
    if (framebuffer->struct_size < sizeof(*framebuffer) ||
        framebuffer->api_version != RINGL_API_VERSION ||
        framebuffer->reserved0 != 0u || framebuffer->flags != 0u) {
        return 0;
    }
    if (framebuffer->color_target == 0u || framebuffer->color_format == 0u ||
        framebuffer->width == 0u || framebuffer->height == 0u) {
        return 0;
    }
    return 1;
}

static int framebuffer_state_valid(uint32_t state)
{
    return state == RINGL_RIN_GPU_IMAGE_UNDEFINED ||
           state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET ||
           state == RINGL_RIN_GPU_IMAGE_PRESENT;
}

int ringl_set_default_framebuffer(const RinGLDefaultFramebufferV1* framebuffer)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t dirty = RINGL_DIRTY_FRAMEBUFFER;

    if (context == NULL)
        return -1;
    if (framebuffer == NULL) {
        memset(&context->default_framebuffer, 0,
               sizeof(context->default_framebuffer));
        context->has_default_framebuffer = 0u;
        context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_FRAMEBUFFER |
                                 RINGL_DIRTY_PIPELINE);
        return 0;
    }
    if (!framebuffer_valid(framebuffer)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    if (!context->has_default_framebuffer ||
        context->default_framebuffer.color_format != framebuffer->color_format) {
        dirty |= RINGL_DIRTY_PIPELINE;
    }
    context->default_framebuffer = *framebuffer;
    context->has_default_framebuffer = 1u;
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_PRESENT;
    ringl_context_mark_dirty(context, dirty);
    return 0;
}

int ringl_set_default_framebuffer_state(uint32_t state)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return -1;
    if (!context->has_default_framebuffer) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (!framebuffer_state_valid(state)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    context->default_framebuffer_state = state;
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    return 0;
}

int ringl_get_default_framebuffer(RinGLDefaultFramebufferV1* framebuffer)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || framebuffer == NULL)
        return -1;
    if (!context->has_default_framebuffer) {
        memset(framebuffer, 0, sizeof(*framebuffer));
        return 1;
    }
    *framebuffer = context->default_framebuffer;
    return 0;
}
