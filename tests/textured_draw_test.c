/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t texture_image;
    uint64_t texture_sampler;
    uint64_t pipeline;
    char commands[32];
    uint32_t command_count;
    uint32_t shader_creates;
    uint32_t bind_group_creates;
} FakeBackend;

static void record(FakeBackend* backend, char command)
{
    assert(backend->command_count < sizeof(backend->commands));
    backend->commands[backend->command_count++] = command;
}

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
    assert(size_bytes > 0u);
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    assert(buffer != 0u && offset == 0u && data != NULL && size_bytes > 0u);
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    (void)session;
    assert(object != 0u);
    return 0;
}

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = session;
    assert(rsh1 != NULL && size_bytes >= 64u);
    ++backend->shader_creates;
    *shader_module_out = ++backend->next_handle;
    return 0;
}

static int fake_create_pipeline_native(
    void* session, const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count, const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count, uint64_t* pipeline_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && pipeline_out != NULL);
    assert(desc->vertex_shader != 0u && desc->fragment_shader != 0u);
    assert(desc->vertex_stride == 16u);
    assert(desc->position_output_location == 0u);
    assert(desc->blend_enabled == 0u);
    assert(desc->color_write_mask == RINGL_RIN_GPU_COLOR_WRITE_ALL);
    assert(desc->cull_mode == RINGL_RIN_GPU_CULL_NONE);
    assert(desc->front_face == RINGL_RIN_GPU_FRONT_FACE_CCW);
    assert(attribute_count == 4u && attributes != NULL);
    assert(attributes[0].location == 0u && attributes[0].offset == 0u);
    assert(attributes[1].location == 1u && attributes[1].offset == 4u);
    assert(attributes[2].location == 2u && attributes[2].offset == 8u);
    assert(attributes[3].location == 3u && attributes[3].offset == 12u);
    assert(varying_count == 4u && varyings != NULL);
    assert(varyings[0].vertex_output_location == 4u);
    assert(varyings[0].fragment_input_location == 0u);
    assert(varyings[0].type == 1u && varyings[0].interpolation == 1u);
    assert(varyings[1].vertex_output_location == 5u);
    assert(varyings[1].fragment_input_location == 1u);
    assert(varyings[1].type == 1u && varyings[1].interpolation == 1u);
    assert(varyings[2].vertex_output_location == 6u);
    assert(varyings[2].fragment_input_location == 2u);
    assert(varyings[2].type == 1u && varyings[2].interpolation == 1u);
    assert(varyings[3].vertex_output_location == 7u);
    assert(varyings[3].fragment_input_location == 3u);
    assert(varyings[3].type == 1u && varyings[3].interpolation == 1u);
    backend->pipeline = ++backend->next_handle;
    *pipeline_out = backend->pipeline;
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
    assert(command_list != 0u);
    return 0;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    if (image == backend->texture_image) {
        assert(old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED);
        assert(new_state == RINGL_RIN_GPU_IMAGE_SHADER_READ);
        record(backend, 't');
    } else {
        assert(image == 700u);
        assert(old_state == RINGL_RIN_GPU_IMAGE_PRESENT);
        assert(new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
        record(backend, 'T');
    }
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && pass != NULL && pass->color_target == 700u);
    assert(pass->load_op == RINGL_RIN_GPU_RENDER_LOAD);
    record(backend, 'B');
    return 0;
}

static int fake_set_raster_state(void* session, uint64_t command_list,
                                 const RinGLRinGpuRasterStateV1* state)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && state != NULL);
    assert(state->viewport_x == -9.0f && state->viewport_y == -4.0f);
    assert(state->viewport_width == 100.0f && state->viewport_height == 80.0f);
    assert(state->min_depth == 0.0f && state->max_depth == 1.0f);
    assert(state->scissor_enabled == 1u);
    assert(state->scissor_x == 0 && state->scissor_y == 3);
    assert(state->scissor_width == 15u && state->scissor_height == 30u);
    record(backend, 'R');
    return 0;
}

static int fake_create_image(void* session,
                             const RinGLRinGpuSampledImage2DV1* desc,
                             uint64_t* image_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && desc->width == 2u && desc->height == 2u);
    backend->texture_image = ++backend->next_handle;
    *image_out = backend->texture_image;
    return 0;
}

static int fake_upload_image(void* session, uint64_t image,
                             const RinGLRinGpuImageUpload2DV1* upload,
                             const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;
    assert(image == backend->texture_image && upload != NULL && data != NULL);
    assert(upload->width == 2u && upload->height == 2u);
    assert(size_bytes == 16u);
    return 0;
}

static int fake_create_sampler(void* session,
                               const RinGLRinGpuSamplerV1* desc,
                               uint64_t* sampler_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL);
    backend->texture_sampler = ++backend->next_handle;
    *sampler_out = backend->texture_sampler;
    return 0;
}

static int fake_create_bind_group(
    void* session, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV1* bindings,
    uint32_t binding_count, uint64_t* bind_group_out)
{
    FakeBackend* backend = session;
    assert(pipeline == backend->pipeline && bindings != NULL);
    assert(binding_count == 2u);
    assert(bindings[0].binding == 0u);
    assert(bindings[0].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
    assert(bindings[0].access == RINGL_RIN_GPU_RESOURCE_READ);
    assert(bindings[0].resource == backend->texture_image);
    assert(bindings[1].binding == 1u);
    assert(bindings[1].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
    assert(bindings[1].access == 0u);
    assert(bindings[1].resource == backend->texture_sampler);
    ++backend->bind_group_creates;
    *bind_group_out = ++backend->next_handle;
    return 0;
}

static int fake_bind_resources(void* session, uint64_t command_list,
                               uint64_t bind_group)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && bind_group != 0u);
    record(backend, 'G');
    return 0;
}

static int fake_draw(void* session, uint64_t command_list,
                     const RinGLRinGpuDrawVerticesV1* draw)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && draw != NULL);
    assert(draw->pipeline == backend->pipeline);
    assert(draw->color_target == 700u && draw->vertex_count == 3u);
    record(backend, 'D');
    return 0;
}

static int fake_end(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    record(backend, 'E');
    return 0;
}

static int fake_close(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    record(backend, 'C');
    return 0;
}

static int fake_submit(void* session, uint64_t queue, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(queue == 900u && command_list != 0u);
    record(backend, 'S');
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
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass = fake_begin_render_pass,
        .draw_vertices = fake_draw,
        .end_render_pass = fake_end,
        .close_command_list = fake_close,
        .queue_submit = fake_submit,
        .create_sampled_image_2d = fake_create_image,
        .upload_image_2d = fake_upload_image,
        .create_sampler = fake_create_sampler,
        .create_graphics_pipeline_native = fake_create_pipeline_native,
        .set_raster_state = fake_set_raster_state,
        .create_graphics_bind_group = fake_create_bind_group,
        .bind_graphics_resources = fake_bind_resources,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = 900u,
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
        .color_target = 700u,
        .color_format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM,
        .width = 320u,
        .height = 200u,
    };
    RinGLContext* context = NULL;
    uint32_t buffer;
    uint32_t texture;
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    int32_t sampler_location;
    const float vertices[] = {
        -0.75f, -0.75f, 0.0f, 0.0f,
         0.75f, -0.75f, 1.0f, 0.0f,
         0.0f,   0.75f, 0.5f, 1.0f,
    };
    const uint8_t pixels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u,
    };
    char commands[33];

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);

    ringl_gen_buffers(1, &buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(vertices), vertices,
                      RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 16, 0u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(1u, 2, RINGL_FLOAT, RINGL_FALSE, 16, 8u);
    ringl_enable_vertex_attrib_array(1u);

    ringl_gen_textures(1, &texture);
    ringl_active_texture(RINGL_TEXTURE0);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(
        vertex,
        "attribute vec2 position; attribute vec2 texCoord; varying vec2 uv; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texCoord; }",
        -1);
    ringl_shader_source(
        fragment,
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv); }",
        -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    ringl_use_program(program);
    sampler_location = ringl_get_uniform_location(program, "colorTexture");
    assert(sampler_location == 0);
    ringl_uniform_1i(sampler_location, 0);

    ringl_viewport(-9, -4, 100, 80);
    ringl_scissor(-5, 3, 20, 30);
    ringl_enable(RINGL_SCISSOR_TEST);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    assert(backend.shader_creates == 2u);
    assert(backend.bind_group_creates == 1u);
    memcpy(commands, backend.commands, backend.command_count);
    commands[backend.command_count] = '\0';
    assert(strcmp(commands, "tTBRGDECS") == 0);

    ringl_context_destroy(context);
    return 0;
}
