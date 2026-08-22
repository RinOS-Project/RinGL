/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint32_t transitions;
    uint32_t passes;
    uint32_t submissions;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    (void)session;
    return size_bytes != 0u && buffer_out != NULL ? (*buffer_out = 1u, 0)
                                                : -1;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    return buffer == 1u && offset == 0u && data != NULL && size_bytes != 0u
        ? 0 : -1;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    (void)session;
    return object != 0u ? 0 : -1;
}

static int fake_create_command_list(void* session, uint32_t capabilities,
                                    uint64_t* command_list_out)
{
    (void)session;
    assert(capabilities == RINGL_RIN_GPU_QUEUE_GRAPHICS);
    assert(command_list_out != NULL);
    *command_list_out = 7u;
    return 0;
}

static int fake_reset_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list == 7u ? 0 : -1;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;

    assert(command_list == 7u);
    assert(image == 42u);
    assert(old_state == RINGL_RIN_GPU_IMAGE_PRESENT);
    assert(new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
    ++backend->transitions;
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;

    assert(command_list == 7u && pass != NULL);
    assert(pass->color_target == 42u);
    assert(pass->load_op == RINGL_RIN_GPU_RENDER_CLEAR);
    assert(pass->store_op == RINGL_RIN_GPU_RENDER_STORE);
    assert(pass->clear_red == 0.0f && pass->clear_green == 0.0f &&
           pass->clear_blue == 0.0f && pass->clear_alpha == 0.0f);
    assert(pass->color_write_mask == RINGL_RIN_GPU_COLOR_WRITE_ALL);
    assert(pass->clear_region.enabled == RINGL_FALSE);
    ++backend->passes;
    return 0;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    (void)session;
    return command_list == 7u ? 0 : -1;
}

static int fake_close_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list == 7u ? 0 : -1;
}

static int fake_queue_submit(void* session, uint64_t queue,
                             uint64_t command_list)
{
    FakeBackend* backend = session;

    assert(queue == 9u && command_list == 7u);
    ++backend->submissions;
    return 0;
}

int main(void)
{
    FakeBackend backend = {0};
    RinGLRinGpuOpsV1 ops = {
        .struct_size = sizeof(ops),
        .api_version = RINGL_API_VERSION,
        .create_buffer = fake_create_buffer,
        .upload_buffer = fake_upload_buffer,
        .destroy_object = fake_destroy_object,
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass = fake_begin_render_pass,
        .end_render_pass = fake_end_render_pass,
        .close_command_list = fake_close_command_list,
        .queue_submit = fake_queue_submit,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = 9u,
        .queue_capabilities = RINGL_RIN_GPU_QUEUE_GRAPHICS,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLDefaultFramebufferV1 framebuffer = {
        .struct_size = sizeof(framebuffer),
        .api_version = RINGL_API_VERSION,
        .color_target = 42u,
        .color_format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM,
        .width = 2u,
        .height = 2u,
    };
    RinGLContext* context = NULL;
    RinGLClearValuesV1 clear_values = {
        .struct_size = sizeof(clear_values),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t framebuffer_name = 0u;
    uint32_t error = RINGL_INVALID_ENUM;
    int32_t values[4] = {0};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    assert(ringl_set_default_framebuffer_state(RINGL_RIN_GPU_IMAGE_PRESENT) ==
           0);

    ringl_gen_framebuffers(1, &framebuffer_name);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffer_name);
    ringl_enable(RINGL_SCISSOR_TEST);
    ringl_scissor(1, 1, 1, 1);
    ringl_color_mask(RINGL_FALSE, RINGL_FALSE, RINGL_FALSE, RINGL_FALSE);
    ringl_depth_mask(RINGL_FALSE);
    ringl_stencil_mask(0u);
    ringl_clear_color(0.25f, 0.5f, 0.75f, 1.0f);
    ringl_clear_depth(0.125f);
    ringl_clear_stencil(0x7f);

    assert(ringl_clear_default_framebuffer_for_embedding(&error) == 0);
    assert(error == RINGL_NO_ERROR);
    assert(backend.transitions == 1u && backend.passes == 1u &&
           backend.submissions == 1u);
    assert(ringl_get_bound_framebuffer(RINGL_FRAMEBUFFER) == framebuffer_name);
    assert(ringl_is_enabled(RINGL_SCISSOR_TEST));
    ringl_get_integerv(RINGL_SCISSOR_BOX, values);
    assert(values[0] == 1 && values[1] == 1 && values[2] == 1 &&
           values[3] == 1);
    ringl_get_integerv(RINGL_COLOR_WRITEMASK, values);
    assert(values[0] == 0 && values[1] == 0 && values[2] == 0 &&
           values[3] == 0);
    assert(ringl_get_clear_values(&clear_values) == 0);
    assert(clear_values.red == 0.25f && clear_values.green == 0.5f &&
           clear_values.blue == 0.75f && clear_values.alpha == 1.0f &&
           clear_values.depth == 0.125f && clear_values.stencil == 0x7f);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_destroy(context);
    return 0;
}
