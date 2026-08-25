/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t images[RINGL_MAX_COLOR_ATTACHMENTS];
    uint32_t image_count;
    uint32_t uploads;
    uint32_t transitions;
    uint32_t mrt_passes;
    uint32_t submissions;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;

    assert(size_bytes != 0u && buffer_out != NULL);
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    return buffer != 0u && offset == 0u && data != NULL && size_bytes != 0u
        ? 0 : -1;
}

static int fake_create_image_2d(void* session,
                                const RinGLRinGpuImage2DV1* desc,
                                uint64_t* image_out)
{
    FakeBackend* backend = session;

    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 2u && desc->height == 2u);
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM);
    assert(desc->usage == (RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
                           RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED |
                           RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                           RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE));
    assert(backend->image_count < RINGL_MAX_COLOR_ATTACHMENTS);
    *image_out = ++backend->next_handle;
    backend->images[backend->image_count++] = *image_out;
    return 0;
}

static int fake_upload_image_2d(void* session, uint64_t image,
                                const RinGLRinGpuImageUpload2DV1* upload,
                                const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;

    assert(image != 0u && upload != NULL && data != NULL);
    assert(upload->width == 2u && upload->height == 2u);
    assert(upload->source_row_pitch_bytes == 8u && size_bytes == 16u);
    ++backend->uploads;
    return 0;
}

static int fake_create_command_list(void* session, uint32_t capabilities,
                                    uint64_t* command_list_out)
{
    FakeBackend* backend = session;

    assert(capabilities == RINGL_RIN_GPU_QUEUE_GRAPHICS);
    assert(command_list_out != NULL);
    *command_list_out = ++backend->next_handle;
    return 0;
}

static int fake_reset_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && image != 0u);
    assert(old_state == RINGL_RIN_GPU_IMAGE_COPY_DESTINATION);
    assert(new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
    ++backend->transitions;
    return 0;
}

static int fake_begin_render_pass_mrt(void* session, uint64_t command_list,
                                      const RinGLRinGpuRenderPassMrtV1* pass)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && pass != NULL);
    assert(pass->active_color_mask == UINT32_C(0xf));
    assert(pass->color_load_op == RINGL_RIN_GPU_RENDER_CLEAR);
    assert(pass->color_store_op == RINGL_RIN_GPU_RENDER_STORE);
    assert(pass->color_write_mask == UINT32_C(0xf));
    assert(pass->clear_red == 0.25f && pass->clear_green == 0.5f);
    assert(pass->clear_blue == 0.75f && pass->clear_alpha == 1.0f);
    for (uint32_t index = 0u; index < RINGL_MAX_COLOR_ATTACHMENTS; ++index) {
        assert(pass->color_targets[index] == backend->images[index]);
        assert(pass->color_mip_levels[index] == 0u);
        assert(pass->color_array_layers[index] == 0u);
    }
    ++backend->mrt_passes;
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    (void)session;
    (void)command_list;
    (void)pass;
    /* command_ops_ready() retains this legacy capability requirement. A
     * selected four-target FBO must nevertheless take fake_begin_render_pass_mrt. */
    assert(!"four-target clear selected the legacy render-pass callback");
    return -1;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_close_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_queue_submit(void* session, uint64_t queue,
                             uint64_t command_list)
{
    FakeBackend* backend = session;

    assert(queue == UINT64_C(7) && command_list != 0u);
    ++backend->submissions;
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    (void)session;
    return object != 0u ? 0 : -1;
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
        .create_image_2d = fake_create_image_2d,
        .upload_image_2d = fake_upload_image_2d,
        .begin_render_pass_mrt_v1 = fake_begin_render_pass_mrt,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = UINT64_C(7),
        .queue_capabilities = RINGL_RIN_GPU_QUEUE_GRAPHICS,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    uint32_t textures[RINGL_MAX_COLOR_ATTACHMENTS] = {0};
    uint32_t framebuffer = 0u;
    uint32_t back = RINGL_BACK;
    uint32_t none = RINGL_NONE;
    uint32_t buffers[RINGL_MAX_COLOR_ATTACHMENTS] = {
        RINGL_COLOR_ATTACHMENT0,
        RINGL_COLOR_ATTACHMENT1,
        RINGL_COLOR_ATTACHMENT2,
        RINGL_COLOR_ATTACHMENT3,
    };
    uint32_t sparse_buffers[2] = {
        RINGL_NONE,
        RINGL_COLOR_ATTACHMENT1,
    };
    RinGLDefaultFramebufferV1 default_framebuffer = {
        .struct_size = sizeof(default_framebuffer),
        .api_version = RINGL_API_VERSION,
        .color_target = UINT64_C(99),
        .color_format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM,
        .width = 2u,
        .height = 2u,
    };
    int32_t query = 123;
    uint8_t pixels[16] = {0};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_get_integerv_bounded(RINGL_MAX_DRAW_BUFFERS_WEBGL, &query,
                                      1u) == -1);
    assert(query == 123);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_enable_webgl_draw_buffers() == 0);
    assert(ringl_get_integerv_bounded(RINGL_MAX_DRAW_BUFFERS_WEBGL, &query,
                                      1u) == 0);
    assert(query == (int32_t)RINGL_MAX_COLOR_ATTACHMENTS);
    assert(ringl_get_integerv_bounded(RINGL_MAX_COLOR_ATTACHMENTS_WEBGL,
                                      &query, 1u) == 0);
    assert(query == (int32_t)RINGL_MAX_COLOR_ATTACHMENTS);
    ringl_gen_textures(RINGL_MAX_COLOR_ATTACHMENTS, textures);
    for (uint32_t index = 0u; index < RINGL_MAX_COLOR_ATTACHMENTS; ++index) {
        assert(textures[index] != 0u);
        ringl_bind_texture(RINGL_TEXTURE_2D, textures[index]);
        ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
        assert(ringl_get_error() == RINGL_NO_ERROR);
    }
    ringl_gen_framebuffers(1, &framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffer);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER0_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_COLOR_ATTACHMENT0);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER1_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_NONE);
    for (uint32_t index = 0u; index < RINGL_MAX_COLOR_ATTACHMENTS; ++index) {
        ringl_framebuffer_texture_2d(
            RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0 + index,
            RINGL_TEXTURE_2D, textures[index], 0);
        assert(ringl_get_error() == RINGL_NO_ERROR);
    }
    ringl_draw_buffers(RINGL_MAX_COLOR_ATTACHMENTS, buffers);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    for (uint32_t index = 0u; index < RINGL_MAX_COLOR_ATTACHMENTS; ++index) {
        assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER0_WEBGL + index,
                                          &query, 1u) == 0);
        assert(query == (int32_t)(RINGL_COLOR_ATTACHMENT0 + index));
    }
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);

    ringl_clear_color(0.25f, 0.5f, 0.75f, 1.0f);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.image_count == RINGL_MAX_COLOR_ATTACHMENTS);
    assert(backend.uploads == RINGL_MAX_COLOR_ATTACHMENTS);
    assert(backend.transitions == RINGL_MAX_COLOR_ATTACHMENTS);
    assert(backend.mrt_passes == 1u && backend.submissions == 1u);

    ringl_draw_buffers(2, sparse_buffers);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER0_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_NONE);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER1_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_COLOR_ATTACHMENT1);
    ringl_draw_buffers(0, NULL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER1_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_NONE);

    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);
    assert(ringl_set_default_framebuffer(&default_framebuffer) == 0);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER0_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_BACK);
    ringl_draw_buffers(1, &none);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_integerv_bounded(RINGL_DRAW_BUFFER0_WEBGL, &query, 1u) ==
           0);
    assert(query == (int32_t)RINGL_NONE);
    ringl_draw_buffers(1, &back);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_destroy(context);
    return 0;
}
