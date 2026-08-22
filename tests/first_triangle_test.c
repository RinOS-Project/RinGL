/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    char commands[64];
    uint32_t command_count;
    uint32_t shader_creates;
    uint32_t pipeline_creates;
    uint32_t submissions;
    float clear[4];
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
    assert(buffer != 0u);
    assert(offset == 0u);
    assert(data != NULL);
    assert(size_bytes > 0u);
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

static int fake_create_graphics_pipeline(
    void* session, const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count, uint64_t* pipeline_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && pipeline_out != NULL);
    assert(desc->vertex_shader != 0u && desc->fragment_shader != 0u);
    assert(desc->primitive_topology == 1u);
    assert(attribute_count == 1u && attributes != NULL);
    assert(attributes[0].location == 0u);
    ++backend->pipeline_creates;
    *pipeline_out = ++backend->next_handle;
    return 0;
}

static int fake_create_command_list(void* session, uint32_t capabilities,
                                    uint64_t* command_list_out)
{
    FakeBackend* backend = session;
    assert(capabilities == RINGL_RIN_GPU_QUEUE_GRAPHICS);
    *command_list_out = ++backend->next_handle;
    record(backend, 'N');
    return 0;
}

static int fake_reset_command_list(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    record(backend, 'R');
    return 0;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && image == 700u);
    assert((old_state == RINGL_RIN_GPU_IMAGE_PRESENT &&
            new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET) ||
           (old_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET &&
            new_state == RINGL_RIN_GPU_IMAGE_PRESENT));
    record(backend, 'T');
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && pass != NULL && pass->color_target == 700u);
    if (pass->load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
        backend->clear[0] = pass->clear_red;
        backend->clear[1] = pass->clear_green;
        backend->clear[2] = pass->clear_blue;
        backend->clear[3] = pass->clear_alpha;
    } else {
        assert(pass->load_op == RINGL_RIN_GPU_RENDER_LOAD);
    }
    assert(pass->store_op == RINGL_RIN_GPU_RENDER_STORE);
    record(backend, 'B');
    return 0;
}

static int fake_draw_vertices(void* session, uint64_t command_list,
                              const RinGLRinGpuDrawVerticesV1* draw)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && draw != NULL);
    assert(draw->pipeline != 0u && draw->vertex_buffer != 0u);
    assert(draw->color_target == 700u);
    assert(draw->vertex_count == 3u && draw->first_vertex == 0u);
    assert(draw->instance_count == 1u);
    record(backend, 'D');
    return 0;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    record(backend, 'E');
    return 0;
}

static int fake_present(void* session, uint64_t command_list, uint64_t image,
                        uint32_t display_id)
{
    FakeBackend* backend = session;
    assert(command_list != 0u && image == 700u && display_id == 3u);
    record(backend, 'P');
    return 0;
}

static int fake_close_command_list(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    record(backend, 'C');
    return 0;
}

static int fake_queue_submit(void* session, uint64_t queue,
                             uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(queue == 900u && command_list != 0u);
    ++backend->submissions;
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
        .create_graphics_pipeline = fake_create_graphics_pipeline,
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass = fake_begin_render_pass,
        .draw_vertices = fake_draw_vertices,
        .end_render_pass = fake_end_render_pass,
        .present = fake_present,
        .close_command_list = fake_close_command_list,
        .queue_submit = fake_queue_submit,
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
        .color_format = 2u,
        .width = 640u,
        .height = 480u,
        .display_id = 3u,
    };
    RinGLContext* context = NULL;
    uint32_t buffer;
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    const float vertices[3] = {-0.5f, 0.0f, 0.5f};
    char commands[65];

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);

    ringl_gen_buffers(1, &buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(vertices), vertices,
                      RINGL_STATIC_DRAW);
    ringl_vertex_attrib_pointer(0u, 1, RINGL_FLOAT, RINGL_FALSE, 0, 0u);
    ringl_enable_vertex_attrib_array(0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(vertex,
                        "attribute float position; void main() { gl_Position = position; }",
                        -1);
    ringl_shader_source(fragment,
                        "void main() { gl_FragColor = 1.0; }", -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    ringl_use_program(program);

    ringl_clear_color(-1.0f, 0.25f, 2.0f, 1.0f);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_present() == 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    assert(backend.shader_creates == 2u);
    assert(backend.pipeline_creates == 1u);
    assert(backend.submissions == 3u);
    assert(backend.clear[0] == 0.0f);
    assert(backend.clear[1] == 0.25f);
    assert(backend.clear[2] == 1.0f);
    assert(backend.clear[3] == 1.0f);
    assert(backend.command_count < sizeof(commands));
    memcpy(commands, backend.commands, backend.command_count);
    commands[backend.command_count] = '\0';
    assert(strcmp(commands, "NTBECSRBDECSRTPCS") == 0);

    ringl_context_destroy(context);
    return 0;
}
