/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct __attribute__((packed)) Rsh1Header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t stage;
    uint32_t flags;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t input_count;
    uint32_t output_count;
    uint32_t resource_count;
    uint32_t workgroup_x;
    uint32_t workgroup_y;
    uint32_t workgroup_z;
    uint32_t entry_instruction;
    uint32_t reserved0;
    uint32_t reserved1;
} Rsh1Header;

typedef struct __attribute__((packed)) Rsh1Instruction {
    uint16_t opcode;
    uint16_t flags;
    uint16_t destination;
    uint16_t source0;
    uint16_t source1;
    uint16_t resource;
    uint32_t immediate;
} Rsh1Instruction;

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t texture_images[2];
    uint64_t texture_samplers[2];
    uint32_t texture_image_count;
    uint32_t texture_sampler_count;
    uint64_t pipeline;
    char commands[32];
    uint32_t command_count;
    uint32_t shader_creates;
    uint32_t vertex_color_material_fragment_modules;
    float expected_vertex_color_tint[4];
    float expected_vertex_color_opacity;
    uint32_t validate_vertex_color_tint;
    uint32_t pipeline_creates;
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
    Rsh1Header header;

    assert(rsh1 != NULL && size_bytes >= 64u);
    memcpy(&header, rsh1, sizeof(header));
    if (header.stage == 2u &&
        ((header.resource_count == 2u && header.instruction_count == 32u) ||
         (header.resource_count == 4u && header.instruction_count == 42u))) {
        const Rsh1Instruction* instructions =
            (const Rsh1Instruction*)((const uint8_t*)rsh1 + sizeof(header));
        uint32_t tint_base = header.instruction_count == 32u ? 14u : 24u;
        uint32_t color_multiply_base =
            header.instruction_count == 32u ? 10u : 20u;
        uint16_t color_input_base =
            header.resource_count == 2u ? 2u : 4u;
        static const uint16_t swizzled_color_components[4] = {2u, 1u, 0u, 3u};
        uint32_t index;

        assert(size_bytes == sizeof(header) +
                                 header.instruction_count * sizeof(*instructions));
        for (index = 0u; index < 4u; ++index) {
            assert(instructions[color_multiply_base + index].source1 ==
                   (uint16_t)(color_input_base +
                              swizzled_color_components[index]));
        }
        if (backend->validate_vertex_color_tint) {
            for (index = 0u; index < 4u; ++index) {
                uint32_t expected_bits;

                memcpy(&expected_bits,
                       &backend->expected_vertex_color_tint[
                           swizzled_color_components[index]],
                       sizeof(expected_bits));
                assert(instructions[tint_base + index].opcode == 16u &&
                       instructions[tint_base + index].immediate == expected_bits);
            }
            {
                uint32_t expected_bits;

                memcpy(&expected_bits, &backend->expected_vertex_color_opacity,
                       sizeof(expected_bits));
                assert(instructions[tint_base + 8u].opcode == 16u &&
                       instructions[tint_base + 8u].immediate == expected_bits);
            }
        }
        ++backend->vertex_color_material_fragment_modules;
    }
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
    uint32_t index;

    assert(desc != NULL && pipeline_out != NULL);
    assert(desc->vertex_shader != 0u && desc->fragment_shader != 0u);
    assert(desc->position_output_location == 0u);
    assert(desc->blend_enabled == 0u);
    assert(desc->color_write_mask == RINGL_RIN_GPU_COLOR_WRITE_ALL);
    assert(desc->cull_mode == RINGL_RIN_GPU_CULL_NONE);
    assert(desc->front_face == RINGL_RIN_GPU_FRONT_FACE_CCW);

    if (backend->pipeline_creates == 0u) {
        assert(desc->vertex_stride == 24u);
        assert(attribute_count == 6u && attributes != NULL);
        for (index = 0u; index < 6u; ++index) {
            assert(attributes[index].location == index);
            assert(attributes[index].offset == index * 4u);
        }
        assert(varying_count == 4u && varyings != NULL);
        for (index = 0u; index < 4u; ++index) {
            assert(varyings[index].vertex_output_location == 4u + index);
            assert(varyings[index].fragment_input_location == index);
            assert(varyings[index].type == 1u &&
                   varyings[index].interpolation == 1u);
        }
    } else if (backend->pipeline_creates == 1u) {
        assert(desc->vertex_stride == 32u);
        assert(attribute_count == 8u && attributes != NULL);
        for (index = 0u; index < 8u; ++index) {
            assert(attributes[index].location == index);
            assert(attributes[index].offset == index * 4u);
        }
        assert(varying_count == 6u && varyings != NULL);
        for (index = 0u; index < 6u; ++index) {
            assert(varyings[index].vertex_output_location == 4u + index);
            assert(varyings[index].fragment_input_location == index);
            assert(varyings[index].type == 1u &&
                   varyings[index].interpolation == 1u);
        }
    } else if (backend->pipeline_creates == 2u) {
        assert(desc->vertex_stride == 40u);
        assert(attribute_count == 10u && attributes != NULL);
        for (index = 0u; index < 10u; ++index) {
            assert(attributes[index].location == index);
            assert(attributes[index].offset == index * 4u);
        }
        assert(varying_count == 8u && varyings != NULL);
        for (index = 0u; index < 8u; ++index) {
            assert(varyings[index].vertex_output_location == 4u + index);
            assert(varyings[index].fragment_input_location == index);
            assert(varyings[index].type == 1u &&
                   varyings[index].interpolation == 1u);
        }
    } else {
        assert(0);
    }
    ++backend->pipeline_creates;
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
    if (image == backend->texture_images[0] ||
        image == backend->texture_images[1]) {
        assert(old_state == RINGL_RIN_GPU_IMAGE_COPY_DESTINATION);
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
    assert(state->min_depth == 0.25f && state->max_depth == 0.75f);
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
    assert(backend->texture_image_count < 2u);
    *image_out = ++backend->next_handle;
    backend->texture_images[backend->texture_image_count++] = *image_out;
    return 0;
}

static int fake_upload_image(void* session, uint64_t image,
                             const RinGLRinGpuImageUpload2DV1* upload,
                             const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;
    assert((image == backend->texture_images[0] ||
            image == backend->texture_images[1]) &&
           upload != NULL && data != NULL);
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
    assert(backend->texture_sampler_count < 2u);
    *sampler_out = ++backend->next_handle;
    backend->texture_samplers[backend->texture_sampler_count++] = *sampler_out;
    return 0;
}

static int fake_create_bind_group(
    void* session, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV1* bindings,
    uint32_t binding_count, uint64_t* bind_group_out)
{
    FakeBackend* backend = session;
    assert(pipeline == backend->pipeline && bindings != NULL);
    if (backend->pipeline_creates == 1u) {
        assert(binding_count == 4u);
        assert(bindings[0].binding == 0u);
        assert(bindings[0].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
        assert(bindings[0].access == RINGL_RIN_GPU_RESOURCE_READ);
        assert(bindings[0].resource == backend->texture_images[0]);
        assert(bindings[1].binding == 1u);
        assert(bindings[1].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
        assert(bindings[1].access == 0u);
        assert(bindings[1].resource == backend->texture_samplers[0]);
        assert(bindings[2].binding == 2u);
        assert(bindings[2].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
        assert(bindings[2].access == RINGL_RIN_GPU_RESOURCE_READ);
        assert(bindings[2].resource == backend->texture_images[1]);
        assert(bindings[3].binding == 3u);
        assert(bindings[3].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
        assert(bindings[3].access == 0u);
        assert(bindings[3].resource == backend->texture_samplers[1]);
    } else if (backend->pipeline_creates == 2u) {
        assert(binding_count == 2u);
        assert(bindings[0].binding == 0u);
        assert(bindings[0].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
        assert(bindings[0].access == RINGL_RIN_GPU_RESOURCE_READ);
        assert(bindings[0].resource == backend->texture_images[0]);
        assert(bindings[1].binding == 1u);
        assert(bindings[1].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
        assert(bindings[1].access == 0u);
        assert(bindings[1].resource == backend->texture_samplers[0]);
    } else if (backend->pipeline_creates == 3u) {
        assert(binding_count == 4u);
        assert(bindings[0].binding == 0u);
        assert(bindings[0].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
        assert(bindings[0].access == RINGL_RIN_GPU_RESOURCE_READ);
        assert(bindings[0].resource == backend->texture_images[0]);
        assert(bindings[1].binding == 1u);
        assert(bindings[1].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
        assert(bindings[1].access == 0u);
        assert(bindings[1].resource == backend->texture_samplers[0]);
        assert(bindings[2].binding == 2u);
        assert(bindings[2].kind == RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE);
        assert(bindings[2].access == RINGL_RIN_GPU_RESOURCE_READ);
        assert(bindings[2].resource == backend->texture_images[1]);
        assert(bindings[3].binding == 3u);
        assert(bindings[3].kind == RINGL_RIN_GPU_RESOURCE_SAMPLER);
        assert(bindings[3].access == 0u);
        assert(bindings[3].resource == backend->texture_samplers[1]);
    } else {
        assert(0);
    }
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
    uint32_t vertex_color_buffer;
    uint32_t two_texture_vertex_color_buffer;
    uint32_t textures[2];
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    uint32_t vertex_color_vertex;
    uint32_t vertex_color_fragment;
    uint32_t vertex_color_program;
    uint32_t two_texture_vertex_color_vertex;
    uint32_t two_texture_vertex_color_fragment;
    uint32_t two_texture_vertex_color_program;
    int32_t first_sampler_location;
    int32_t second_sampler_location;
    int32_t tint_location;
    int32_t transform_location;
    int32_t vertex_color_sampler_location;
    int32_t vertex_color_opacity_location;
    int32_t vertex_color_tint_location;
    int32_t vertex_color_transform_location;
    int32_t two_texture_vertex_color_first_sampler_location;
    int32_t two_texture_vertex_color_second_sampler_location;
    int32_t two_texture_vertex_color_opacity_location;
    int32_t two_texture_vertex_color_tint_location;
    int32_t two_texture_vertex_color_transform_location;
    const float vertices[] = {
        -0.75f, -0.75f, 0.0f, 0.0f, 0.25f, 0.75f,
         0.75f, -0.75f, 1.0f, 0.0f, 0.75f, 0.25f,
         0.0f,   0.75f, 0.5f, 1.0f, 0.50f, 0.50f,
    };
    const uint8_t pixels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u,
    };
    const float transform[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const float tint[4] = {0.5f, 1.0f, 0.25f, 1.0f};
    const float opacity = 0.625f;
    const float vertex_color_vertices[] = {
        -0.75f, -0.75f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.75f, -0.75f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
        0.0f,   0.75f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
    };
    const float two_texture_vertex_color_vertices[] = {
        -0.75f, -0.75f, 0.0f, 0.0f, 0.25f, 0.75f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.75f, -0.75f, 1.0f, 0.0f, 0.75f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f,
         0.0f,   0.75f, 0.5f, 1.0f, 0.50f, 0.50f, 0.0f, 0.0f, 1.0f, 1.0f,
    };
    char commands[33];

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);

    ringl_gen_buffers(1, &buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(vertices), vertices,
                      RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 24, 0u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(1u, 2, RINGL_FLOAT, RINGL_FALSE, 24, 8u);
    ringl_enable_vertex_attrib_array(1u);
    ringl_vertex_attrib_pointer(2u, 2, RINGL_FLOAT, RINGL_FALSE, 24, 16u);
    ringl_enable_vertex_attrib_array(2u);

    ringl_gen_textures(2, textures);
    ringl_active_texture(RINGL_TEXTURE0);
    ringl_bind_texture(RINGL_TEXTURE_2D, textures[0]);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    ringl_bind_texture(RINGL_TEXTURE_2D, textures[1]);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(
        vertex,
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; uniform mat4 transform; "
        "varying vec2 firstUv; varying vec2 secondUv; void main() { "
        "gl_Position = transform * vec4(position, 0.0, 1.0); "
        "firstUv = firstTexCoord; secondUv = secondTexCoord; }",
        -1);
    ringl_shader_source(
        fragment,
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform vec4 tint; varying vec2 firstUv; varying vec2 secondUv; "
        "void main() { gl_FragColor = (texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv)) * tint.stpq.bgra; }",
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
    first_sampler_location = ringl_get_uniform_location(program, "firstTexture");
    second_sampler_location = ringl_get_uniform_location(program, "secondTexture");
    tint_location = ringl_get_uniform_location(program, "tint");
    transform_location = ringl_get_uniform_location(program, "transform");
    assert(first_sampler_location == 0);
    assert(second_sampler_location == 1);
    assert(tint_location == 2);
    assert(transform_location == 3);
    ringl_uniform_1i(first_sampler_location, 0);
    ringl_uniform_1i(second_sampler_location, 1);
    ringl_uniform_matrix4fv(transform_location, RINGL_FALSE, transform);
    ringl_uniform_4f(tint_location, tint[0], tint[1], tint[2], tint[3]);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_viewport(-9, -4, 100, 80);
    ringl_depth_range(0.25f, 0.75f);
    ringl_scissor(-5, 3, 20, 30);
    ringl_enable(RINGL_SCISSOR_TEST);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    /* Link builds program-owned vertex and fragment modules. The matrix
     * setter replaces only the transformed two-UV vertex executable; the
     * tint setter replaces only the two-sampler fragment executable before
     * the textured draw builds its native RinGPU pipeline. */
    assert(backend.shader_creates == 4u);
    assert(backend.bind_group_creates == 1u);
    memcpy(commands, backend.commands, backend.command_count);
    commands[backend.command_count] = '\0';
    assert(strcmp(commands, "ttTBRGDECS") == 0);

    /* The common vertex-color texture route uses a distinct six-scalar
     * varying interface. It must preserve the matrix transform and bind the
     * real image/sampler pair instead of falling back to a pre-multiplied CPU
     * color. Its tint and scalar opacity remain RSH1 material operations. */
    ringl_gen_buffers(1, &vertex_color_buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, vertex_color_buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(vertex_color_vertices),
                      vertex_color_vertices, RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 32, 0u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(1u, 2, RINGL_FLOAT, RINGL_FALSE, 32, 8u);
    ringl_enable_vertex_attrib_array(1u);
    ringl_vertex_attrib_pointer(2u, 4, RINGL_FLOAT, RINGL_FALSE, 32, 16u);
    ringl_enable_vertex_attrib_array(2u);
    vertex_color_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    vertex_color_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    vertex_color_program = ringl_create_program();
    assert(vertex_color_vertex != 0u && vertex_color_fragment != 0u &&
           vertex_color_program != 0u);
    ringl_shader_source(
        vertex_color_vertex,
        "attribute vec2 position; attribute vec2 texCoord; attribute vec4 color; "
        "uniform mat4 transform; varying vec2 uv; varying vec4 vertexColor; "
        "void main() { gl_Position = transform * vec4(position, 0.0, 1.0); "
        "uv = texCoord; vertexColor = color; }",
        -1);
    ringl_shader_source(
        vertex_color_fragment,
        "uniform sampler2D colorTexture; uniform vec4 tint; uniform float opacity; "
        "varying vec2 uv; "
        "varying vec4 vertexColor; void main() { gl_FragColor = "
        "texture2D(colorTexture, uv) * vertexColor.stpq.bgra * tint.stpq.bgra * "
        "opacity; }",
        -1);
    ringl_compile_shader(vertex_color_vertex);
    ringl_compile_shader(vertex_color_fragment);
    assert(ringl_get_shader_compile_status(vertex_color_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(vertex_color_fragment) == RINGL_TRUE);
    ringl_attach_shader(vertex_color_program, vertex_color_vertex);
    ringl_attach_shader(vertex_color_program, vertex_color_fragment);
    ringl_link_program(vertex_color_program);
    assert(ringl_get_program_link_status(vertex_color_program) == RINGL_TRUE);
    ringl_use_program(vertex_color_program);
    vertex_color_sampler_location =
        ringl_get_uniform_location(vertex_color_program, "colorTexture");
    vertex_color_opacity_location =
        ringl_get_uniform_location(vertex_color_program, "opacity");
    vertex_color_tint_location =
        ringl_get_uniform_location(vertex_color_program, "tint");
    vertex_color_transform_location =
        ringl_get_uniform_location(vertex_color_program, "transform");
    assert(vertex_color_sampler_location == 0);
    assert(vertex_color_opacity_location == 1);
    assert(vertex_color_tint_location == 2);
    assert(vertex_color_transform_location == 3);
    ringl_uniform_1i(vertex_color_sampler_location, 0);
    ringl_uniform_matrix4fv(vertex_color_transform_location, RINGL_FALSE,
                            transform);
    memcpy(backend.expected_vertex_color_tint, tint,
           sizeof(backend.expected_vertex_color_tint));
    backend.expected_vertex_color_opacity = 0.0f;
    backend.validate_vertex_color_tint = 1u;
    ringl_uniform_4f(vertex_color_tint_location, tint[0], tint[1], tint[2],
                     tint[3]);
    backend.expected_vertex_color_opacity = opacity;
    ringl_uniform_1f(vertex_color_opacity_location, opacity);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.shader_creates == 9u);
    assert(backend.vertex_color_material_fragment_modules == 3u);
    assert(backend.pipeline_creates == 2u);
    assert(backend.bind_group_creates == 2u);

    /* Two independently bound textures can be added before the shared
     * interpolated vertex color, tint, and opacity material operations. This
     * verifies the twelve-output vertex / eight-input fragment interface and
     * both typed RinGPU image/sampler pairs end to end. */
    backend.validate_vertex_color_tint = 0u;
    ringl_gen_buffers(1, &two_texture_vertex_color_buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, two_texture_vertex_color_buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER,
                      sizeof(two_texture_vertex_color_vertices),
                      two_texture_vertex_color_vertices, RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 2, RINGL_FLOAT, RINGL_FALSE, 40, 0u);
    ringl_enable_vertex_attrib_array(0u);
    ringl_vertex_attrib_pointer(1u, 2, RINGL_FLOAT, RINGL_FALSE, 40, 8u);
    ringl_enable_vertex_attrib_array(1u);
    ringl_vertex_attrib_pointer(2u, 2, RINGL_FLOAT, RINGL_FALSE, 40, 16u);
    ringl_enable_vertex_attrib_array(2u);
    ringl_vertex_attrib_pointer(3u, 4, RINGL_FLOAT, RINGL_FALSE, 40, 24u);
    ringl_enable_vertex_attrib_array(3u);
    two_texture_vertex_color_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    two_texture_vertex_color_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    two_texture_vertex_color_program = ringl_create_program();
    assert(two_texture_vertex_color_vertex != 0u &&
           two_texture_vertex_color_fragment != 0u &&
           two_texture_vertex_color_program != 0u);
    ringl_shader_source(
        two_texture_vertex_color_vertex,
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; attribute vec4 color; uniform mat4 transform; "
        "varying vec2 firstUv; varying vec2 secondUv; varying vec4 vertexColor; "
        "void main() { gl_Position = transform * vec4(position, 0.0, 1.0); "
        "firstUv = firstTexCoord; secondUv = secondTexCoord; vertexColor = color; }",
        -1);
    ringl_shader_source(
        two_texture_vertex_color_fragment,
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform vec4 tint; uniform float opacity; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec4 vertexColor; void main() { "
        "gl_FragColor = (texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv)) * vertexColor.stpq.bgra * "
        "tint.stpq.bgra * opacity; }",
        -1);
    ringl_compile_shader(two_texture_vertex_color_vertex);
    ringl_compile_shader(two_texture_vertex_color_fragment);
    assert(ringl_get_shader_compile_status(two_texture_vertex_color_vertex) ==
           RINGL_TRUE);
    assert(ringl_get_shader_compile_status(two_texture_vertex_color_fragment) ==
           RINGL_TRUE);
    ringl_attach_shader(two_texture_vertex_color_program,
                         two_texture_vertex_color_vertex);
    ringl_attach_shader(two_texture_vertex_color_program,
                         two_texture_vertex_color_fragment);
    ringl_link_program(two_texture_vertex_color_program);
    assert(ringl_get_program_link_status(two_texture_vertex_color_program) ==
           RINGL_TRUE);
    ringl_use_program(two_texture_vertex_color_program);
    two_texture_vertex_color_first_sampler_location = ringl_get_uniform_location(
        two_texture_vertex_color_program, "firstTexture");
    two_texture_vertex_color_second_sampler_location = ringl_get_uniform_location(
        two_texture_vertex_color_program, "secondTexture");
    two_texture_vertex_color_opacity_location = ringl_get_uniform_location(
        two_texture_vertex_color_program, "opacity");
    two_texture_vertex_color_tint_location = ringl_get_uniform_location(
        two_texture_vertex_color_program, "tint");
    two_texture_vertex_color_transform_location = ringl_get_uniform_location(
        two_texture_vertex_color_program, "transform");
    assert(two_texture_vertex_color_first_sampler_location == 0);
    assert(two_texture_vertex_color_second_sampler_location == 1);
    assert(two_texture_vertex_color_opacity_location == 2);
    assert(two_texture_vertex_color_tint_location == 3);
    assert(two_texture_vertex_color_transform_location == 4);
    ringl_uniform_1i(two_texture_vertex_color_first_sampler_location, 0);
    ringl_uniform_1i(two_texture_vertex_color_second_sampler_location, 1);
    ringl_uniform_matrix4fv(two_texture_vertex_color_transform_location,
                            RINGL_FALSE, transform);
    memcpy(backend.expected_vertex_color_tint, tint,
           sizeof(backend.expected_vertex_color_tint));
    backend.expected_vertex_color_opacity = 0.0f;
    backend.validate_vertex_color_tint = 1u;
    ringl_uniform_4f(two_texture_vertex_color_tint_location, tint[0], tint[1],
                     tint[2], tint[3]);
    backend.expected_vertex_color_opacity = opacity;
    ringl_uniform_1f(two_texture_vertex_color_opacity_location, opacity);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.shader_creates == 14u);
    assert(backend.vertex_color_material_fragment_modules == 6u);
    assert(backend.pipeline_creates == 3u);
    assert(backend.bind_group_creates == 3u);

    ringl_context_destroy(context);
    return 0;
}
