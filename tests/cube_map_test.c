/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#include "shader/glsl_lower.h"
#include "shader/rsh1_abi.h"
#include "../src/ringl_internal.h"

typedef struct FakeCubeBackend {
    uint64_t next_handle;
    uint32_t image_creates;
    uint32_t image_uploads;
    uint32_t sampler_creates;
    uint32_t destroys;
    uint32_t partial_fail_next_image;
    uint32_t empty_success_next_image;
} FakeCubeBackend;

static void upload_face(uint32_t face, const uint8_t* pixels);

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeCubeBackend* backend = session;
    assert(size_bytes != 0u && buffer_out != NULL);
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer,
                              uint64_t offset, const void* data,
                              uint64_t size_bytes)
{
    (void)session;
    assert(buffer != 0u && offset == 0u && data != NULL && size_bytes != 0u);
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    FakeCubeBackend* backend = session;
    assert(object != 0u);
    ++backend->destroys;
    return 0;
}

static int fake_create_image_array(
    void* session, const RinGLRinGpuImageArrayV1* desc,
    uint64_t* image_out)
{
    FakeCubeBackend* backend = session;

    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 2u && desc->height == 2u);
    assert(desc->array_layers == 6u && desc->mip_levels == 2u);
    ++backend->image_creates;
    if (backend->partial_fail_next_image != 0u) {
        --backend->partial_fail_next_image;
        *image_out = ++backend->next_handle;
        return -1;
    }
    if (backend->empty_success_next_image != 0u) {
        --backend->empty_success_next_image;
        *image_out = 0u;
        return 0;
    }
    *image_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_image_array(
    void* session, uint64_t image,
    const RinGLRinGpuImageUploadArrayV1* upload,
    const void* data, uint64_t size_bytes)
{
    FakeCubeBackend* backend = session;
    assert(image != 0u && upload != NULL && upload->mip_level == 0u);
    assert(upload->array_layer < 6u && upload->width == 2u &&
           upload->height == 2u);
    assert(data != NULL && size_bytes == 16u);
    ++backend->image_uploads;
    return 0;
}

static int fake_create_sampler(void* session,
                               const RinGLRinGpuSamplerV1* desc,
                               uint64_t* sampler_out)
{
    FakeCubeBackend* backend = session;
    assert(desc != NULL && sampler_out != NULL && desc->max_anisotropy == 1u);
    ++backend->sampler_creates;
    *sampler_out = ++backend->next_handle;
    return 0;
}

static void verify_cube_factory_failure_cleanup(const uint8_t* pixels)
{
    FakeCubeBackend backend = {0};
    RinGLRinGpuOpsV1 ops = {
        .struct_size = sizeof(ops),
        .api_version = RINGL_API_VERSION,
        .create_buffer = fake_create_buffer,
        .upload_buffer = fake_upload_buffer,
        .destroy_object = fake_destroy_object,
        .create_sampler = fake_create_sampler,
        .create_image_array_v1 = fake_create_image_array,
        .upload_image_array_v1 = fake_upload_image_array,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    uint32_t texture = 0u;
    const uint32_t faces[6] = {
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_X,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    };
    uint64_t image = UINT64_MAX;
    uint64_t sampler = UINT64_MAX;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(context->has_ringpu_ops != 0u);
    assert(context->ringpu_ops.create_image_array_v1 ==
           fake_create_image_array);
    ringl_gen_textures(1u, &texture);
    ringl_bind_texture(RINGL_TEXTURE_CUBE_MAP, texture);
    ringl_tex_parameteri(RINGL_TEXTURE_CUBE_MAP, RINGL_TEXTURE_WRAP_S,
                         RINGL_CLAMP_TO_EDGE);
    ringl_tex_parameteri(RINGL_TEXTURE_CUBE_MAP, RINGL_TEXTURE_WRAP_T,
                         RINGL_CLAMP_TO_EDGE);
    ringl_tex_parameteri(RINGL_TEXTURE_CUBE_MAP, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST);
    for (uint32_t index = 0u; index < 6u; ++index)
        upload_face(faces[index], pixels);
    assert(context->bound_texture_cube[0] == texture);
    assert(context->bound_texture_2d[0] == 0u);
    assert(context->textures[ringl_object_slot_index(texture)].target ==
           RINGL_TEXTURE_CUBE_MAP);
    assert(context->textures[ringl_object_slot_index(texture)].format ==
           RINGL_RGBA);
    assert(context->textures[ringl_object_slot_index(texture)].width == 2u);
    assert(context->textures[ringl_object_slot_index(texture)].ringpu_image ==
           0u);
    assert(context->textures[ringl_object_slot_index(texture)].wrap_s ==
           RINGL_CLAMP_TO_EDGE);
    assert(context->textures[ringl_object_slot_index(texture)].wrap_t ==
           RINGL_CLAMP_TO_EDGE);
    assert(backend.image_creates == 0u);

    backend.partial_fail_next_image = 1u;
    {
        int realize_result = ringl_texture_realize_unit(context, 0u,
                                                        &image, &sampler);
        assert(backend.image_creates == 1u);
        assert(backend.destroys == 1u);
        assert(realize_result != 0);
    }
    assert(image == 0u && sampler == 0u);
    assert(backend.image_creates == 1u);
    assert(backend.destroys == 1u);
    assert(backend.image_uploads == 0u && backend.sampler_creates == 0u);

    backend.empty_success_next_image = 1u;
    image = UINT64_MAX;
    sampler = UINT64_MAX;
    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) != 0);
    assert(image == 0u && sampler == 0u);
    assert(backend.image_creates == 2u && backend.destroys == 1u);

    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) == 0);
    assert(image != 0u && sampler != 0u);
    assert(backend.image_creates == 3u && backend.image_uploads == 6u);
    assert(backend.sampler_creates == 1u);
    ringl_context_destroy(context);
}

static RinGLContext* make_context(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    return context;
}

static void upload_face(uint32_t face, const uint8_t* pixels)
{
    ringl_tex_image_2d(face, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA,
                        RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
}

int main(void)
{
    static const uint8_t pixels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 255u, 255u,
    };
    static const char fragment_source[] =
        "uniform samplerCube environment;\n"
        "void main() { gl_FragColor = textureCube(environment, "
        "vec3(1.0, 0.0, 0.0)); }";
    RinGLContext* context = make_context();
    uint32_t texture;
    uint32_t framebuffer;
    uint32_t shader;
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    uint32_t faces[6] = {
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_X,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        RINGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
    };
    RinGLGlslLowerResult lowered;
    RinGLRsh1HeaderV1 header;
    const RinGLRsh1InstructionV1* instructions;
    RinGLActiveInfoV1 active = {
        .struct_size = sizeof(active),
        .api_version = RINGL_API_VERSION,
    };
    int32_t face_query = 0;
    uint32_t cube_samples = 0u;

    ringl_gen_textures(1, &texture);
    assert(texture != 0u && ringl_get_error() == RINGL_NO_ERROR);
    ringl_bind_texture(RINGL_TEXTURE_CUBE_MAP, texture);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    for (uint32_t index = 0u; index < 6u; ++index)
        upload_face(faces[index], pixels);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_CUBE_MAP,
                                    RINGL_TEXTURE_MIN_FILTER) ==
           (int32_t)RINGL_NEAREST_MIPMAP_LINEAR);

    ringl_gen_framebuffers(1, &framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                  RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Z, texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_get_framebuffer_attachment_parameteriv_bounded(
               RINGL_COLOR_ATTACHMENT0,
               RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE,
               &face_query, 1u) == 0);
    assert(face_query == (int32_t)RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Z);

    shader = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    ringl_shader_source(shader, fragment_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER, fragment_source,
                                 sizeof(fragment_source) - 1u,
                                 &lowered) == 0);
    assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
    memcpy(&header, lowered.bytes, sizeof(header));
    instructions = (const RinGLRsh1InstructionV1*)(
        lowered.bytes + header.header_size);
    for (uint32_t index = 0u; index < header.instruction_count; ++index) {
        if (instructions[index].opcode == RINGL_RSH1_OP_SAMPLE_IMAGE_CUBE_F32)
            ++cube_samples;
    }
    assert(cube_samples == 4u);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    assert(vertex != 0u && fragment != 0u && program != 0u);
    ringl_shader_source(vertex, "void main() { gl_Position = vec4(0.0); }",
                        -1);
    ringl_shader_source(fragment, fragment_source, -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(ringl_get_active_uniform(program, 0u, &active) == 0);
    assert(active.type == RINGL_SAMPLER_CUBE &&
           strcmp(active.name, "environment") == 0);

    ringl_delete_program(program);
    ringl_delete_shader(vertex);
    ringl_delete_shader(fragment);
    ringl_delete_shader(shader);
    ringl_delete_framebuffers(1, &framebuffer);
    ringl_delete_textures(1, &texture);
    ringl_context_destroy(context);
    verify_cube_factory_failure_cleanup(pixels);
    return 0;
}
