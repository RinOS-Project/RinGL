/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t created_images[2];
    uint32_t image_usages[2];
    uint32_t image_creates;
    uint32_t image_uploads;
    uint32_t transitions;
    uint32_t passes;
    uint32_t submissions;
    uint32_t shader_creates;
    uint32_t pipeline_creates;
    uint32_t draws;
    uint64_t last_pass_target;
    uint32_t last_transition_old_state;
    uint32_t last_transition_new_state;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    (void)session;
    if (size_bytes == 0u || buffer_out == NULL)
        return -1;
    *buffer_out = 1u;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    return buffer != 0u && offset == 0u && data != NULL && size_bytes != 0u
        ? 0 : -1;
}

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = session;

    assert(rsh1 != NULL && size_bytes >= 64u && shader_module_out != NULL);
    *shader_module_out = ++backend->next_handle;
    ++backend->shader_creates;
    return 0;
}

static int fake_create_graphics_pipeline(
    void* session, const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count, uint64_t* pipeline_out)
{
    FakeBackend* backend = session;

    assert(desc != NULL && attributes != NULL && pipeline_out != NULL);
    assert(desc->color_format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM);
    assert(attribute_count == 2u && attributes[0].location == 0u &&
           attributes[1].location == 1u);
    *pipeline_out = ++backend->next_handle;
    ++backend->pipeline_creates;
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    (void)session;
    return object != 0u ? 0 : -1;
}

static int fake_create_image_2d(void* session,
                                const RinGLRinGpuImage2DV1* desc,
                                uint64_t* image_out)
{
    FakeBackend* backend = session;

    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 2u && desc->height == 2u);
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM);
    assert(backend->image_creates < 2u);
    backend->image_usages[backend->image_creates] = desc->usage;
    *image_out = ++backend->next_handle;
    backend->created_images[backend->image_creates++] = *image_out;
    return 0;
}

static int fake_upload_image_2d(void* session, uint64_t image,
                                const RinGLRinGpuImageUpload2DV1* upload,
                                const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;

    assert(image == backend->created_images[0]);
    assert(upload != NULL && upload->width == 2u && upload->height == 2u);
    assert(upload->source_row_pitch_bytes == 8u);
    assert(data != NULL && size_bytes == 16u);
    ++backend->image_uploads;
    return 0;
}

static int fake_create_command_list(void* session, uint32_t capabilities,
                                    uint64_t* command_list_out)
{
    FakeBackend* backend = session;

    assert(capabilities == RINGL_RIN_GPU_QUEUE_GRAPHICS);
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

    assert(command_list != 0u);
    assert(image == backend->created_images[backend->transitions]);
    assert(old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED);
    assert(new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
    backend->last_transition_old_state = old_state;
    backend->last_transition_new_state = new_state;
    ++backend->transitions;
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && pass != NULL);
    assert(pass->color_target ==
           backend->created_images[backend->passes < 2u ? 0u : 1u]);
    assert(pass->load_op == (backend->passes == 1u
                                 ? RINGL_RIN_GPU_RENDER_LOAD
                                 : RINGL_RIN_GPU_RENDER_CLEAR));
    assert(pass->store_op == RINGL_RIN_GPU_RENDER_STORE);
    backend->last_pass_target = pass->color_target;
    ++backend->passes;
    return 0;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_draw_vertices(void* session, uint64_t command_list,
                              const RinGLRinGpuDrawVerticesV1* draw)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && draw != NULL);
    assert(draw->pipeline != 0u && draw->vertex_buffer != 0u);
    assert(draw->color_target == backend->created_images[0]);
    assert(draw->vertex_count == 3u && draw->first_vertex == 0u);
    ++backend->draws;
    return 0;
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

    assert(queue == 99u && command_list != 0u);
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
        .create_shader_module = fake_create_shader_module,
        .create_graphics_pipeline = fake_create_graphics_pipeline,
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass = fake_begin_render_pass,
        .draw_vertices = fake_draw_vertices,
        .end_render_pass = fake_end_render_pass,
        .close_command_list = fake_close_command_list,
        .queue_submit = fake_queue_submit,
        .create_image_2d = fake_create_image_2d,
        .upload_image_2d = fake_upload_image_2d,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = 99u,
        .queue_capabilities = RINGL_RIN_GPU_QUEUE_GRAPHICS,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    uint32_t texture = 0u;
    uint32_t texture_framebuffer = 0u;
    uint32_t renderbuffer = 0u;
    uint32_t renderbuffer_framebuffer = 0u;
    uint32_t vertex_buffer = 0u;
    uint32_t vertex = 0u;
    uint32_t fragment = 0u;
    uint32_t program = 0u;
    uint8_t pixels[16] = {0u};
    const float vertices[6] = {
        -0.5f, -0.5f,
         0.5f, -0.5f,
         0.0f,  0.5f,
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    ringl_gen_framebuffers(1, &texture_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, texture_framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, texture, 0);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.image_creates == 1u && backend.image_uploads == 1u);
    assert(backend.image_usages[0] ==
           (RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
            RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED |
            RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET));

    ringl_gen_buffers(1, &vertex_buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, vertex_buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(vertices), vertices,
                      RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 0, 0u);
    ringl_enable_vertex_attrib_array(0u);
    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(
        vertex,
        "attribute vec2 position; void main() { gl_Position = vec4(position, 0.0, 1.0); }",
        -1);
    ringl_shader_source(
        fragment,
        "void main() { gl_FragColor = vec4(1.0, 0.25, 0.0, 1.0); }", -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    ringl_use_program(program);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.shader_creates == 2u && backend.pipeline_creates == 1u);
    assert(backend.draws == 1u);

    ringl_gen_renderbuffers(1, &renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA8, 2, 2);
    ringl_gen_framebuffers(1, &renderbuffer_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, renderbuffer_framebuffer);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, renderbuffer);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.image_creates == 2u && backend.image_uploads == 1u);
    assert(backend.image_usages[1] == RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET);
    assert(backend.transitions == 2u && backend.passes == 3u);
    assert(backend.submissions == 3u);
    assert(backend.last_pass_target == backend.created_images[1]);
    assert(backend.last_transition_old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED);
    assert(backend.last_transition_new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);

    ringl_context_destroy(context);
    return 0;
}
