/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#define RSH1_MAGIC UINT32_C(0x31485352)

typedef struct __attribute__((packed)) Header {
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
} Header;

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t shader_creates;
    uint32_t destroys;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
    (void)size_bytes;
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    (void)buffer;
    (void)offset;
    (void)data;
    (void)size_bytes;
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    FakeBackend* backend = session;
    assert(object != 0u);
    backend->destroys++;
    return 0;
}

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = session;
    Header header;

    assert(rsh1 != NULL);
    assert(size_bytes >= sizeof(header));
    memcpy(&header, rsh1, sizeof(header));
    if (header.magic != RSH1_MAGIC || header.total_size != size_bytes)
        return -1;
    backend->shader_creates++;
    *shader_module_out = ++backend->next_handle;
    return 0;
}

static Header lower_and_read_header(uint32_t shader, const char* source,
                                    uint8_t* blob, uint32_t capacity)
{
    Header header;
    uint32_t size;

    ringl_shader_source(shader, source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size >= sizeof(Header));
    assert(size <= capacity);
    assert(ringl_copy_shader_rsh1(shader, blob, capacity) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.magic == RSH1_MAGIC);
    assert(header.version == 1u);
    assert(header.total_size == size);
    return header;
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
    uint32_t vertex;
    uint32_t fragment;
    uint8_t blob[4096];
    Header header;
    uint64_t first_module;
    const char* scalar_source =
        "attribute float x;\n"
        "void main() {\n"
        "  float y = x * 2.0;\n"
        "  gl_Position = y + 1.0;\n"
        "}\n";
    const char* vector_source =
        "attribute vec2 position;\n"
        "void main() {\n"
        "  gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    const char* vector_arithmetic_source =
        "attribute vec4 position;\n"
        "void main() {\n"
        "  vec4 scaled = -position * 0.5;\n"
        "  vec4 shifted = scaled + vec4(0.25, 0.0, 0.0, 0.0);\n"
        "  gl_Position = shifted / 2.0 - vec4(0.0, 0.25, 0.0, 0.0);\n"
        "}\n";
    const char* swizzle_arithmetic_source =
        "attribute vec2 position;\n"
        "void main() {\n"
        "  vec2 reflected = position.yx;\n"
        "  gl_Position = vec4(reflected.yx, 0.0, 1.0) + 0.0;\n"
        "}\n";
    const char* fragment_source =
        "precision mediump float;\n"
        "precision highp int;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.25, 0.0, 1.0);\n"
        "}\n";
    const char* texture_source =
        "precision mediump float;\n"
        "precision lowp sampler2D;\n"
        "uniform sampler2D colorTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(colorTexture, vec2(0.25, 0.75));\n"
        "}\n";
    const char* varying_vertex_source =
        "precision highp float; attribute vec2 position; attribute vec2 texCoord; varying vec2 uv; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texCoord; }";
    const char* varying_fragment_source =
        "precision mediump float; uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv); }";
    const char* color_varying_vertex_source =
        "attribute vec2 position; attribute vec4 color; varying vec4 vertexColor; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); vertexColor = color; }";
    const char* color_varying_fragment_source =
        "varying vec4 vertexColor; "
        "void main() { gl_FragColor = vertexColor.stpq.bgra; }";
    const char* color3_varying_vertex_source =
        "attribute vec2 position; attribute vec3 color; varying vec3 vertexColor; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); vertexColor = color; }";
    const char* color3_varying_fragment_source =
        "varying vec3 vertexColor; "
        "void main() { gl_FragColor = vec4(vertexColor.bgr, 1.0); }";
    const char* two_vec2_varying_vertex_source =
        "attribute vec2 position; attribute vec2 colorRG; attribute vec2 colorBA; "
        "varying vec2 vertexRG; varying vec2 vertexBA; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); "
        "vertexRG = colorRG; vertexBA = colorBA; }";
    const char* two_vec2_varying_fragment_source =
        "varying vec2 vertexRG; varying vec2 vertexBA; "
        "void main() { gl_FragColor = vec4(vertexRG, vertexBA); }";
    const char* transformed_two_uv_vertex_source =
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; uniform mat4 transform; "
        "varying vec2 firstUv; varying vec2 secondUv; "
        "void main() { gl_Position = transform * vec4(position, 0.0, 1.0); "
        "firstUv = firstTexCoord; secondUv = secondTexCoord; }";
    const char* transformed_three_uv_vertex_source =
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; attribute vec2 thirdTexCoord; "
        "uniform mat4 transform; varying vec2 firstUv; varying vec2 secondUv; "
        "varying vec2 thirdUv; void main() { gl_Position = transform * "
        "vec4(position, 0.0, 1.0); firstUv = firstTexCoord; "
        "secondUv = secondTexCoord; thirdUv = thirdTexCoord; }";
    const char* transformed_four_uv_vertex_source =
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; attribute vec2 thirdTexCoord; "
        "attribute vec2 fourthTexCoord; uniform mat4 transform; "
        "varying vec2 firstUv; varying vec2 secondUv; varying vec2 thirdUv; "
        "varying vec2 fourthUv; void main() { gl_Position = transform * "
        "vec4(position, 0.0, 1.0); firstUv = firstTexCoord; "
        "secondUv = secondTexCoord; thirdUv = thirdTexCoord; "
        "fourthUv = fourthTexCoord; }";
    const char* transformed_five_uv_vertex_source =
        "attribute vec2 position; attribute vec2 firstTexCoord; "
        "attribute vec2 secondTexCoord; attribute vec2 thirdTexCoord; "
        "attribute vec2 fourthTexCoord; attribute vec2 fifthTexCoord; "
        "uniform mat4 transform; varying vec2 firstUv; varying vec2 secondUv; "
        "varying vec2 thirdUv; varying vec2 fourthUv; varying vec2 fifthUv; "
        "void main() { gl_Position = transform * vec4(position, 0.0, 1.0); "
        "firstUv = firstTexCoord; secondUv = secondTexCoord; "
        "thirdUv = thirdTexCoord; fourthUv = fourthTexCoord; "
        "fifthUv = fifthTexCoord; }";
    const char* transformed_vertex_color_texture_source =
        "attribute vec2 position; attribute vec2 texCoord; attribute vec4 color; "
        "uniform mat4 transform; varying vec2 uv; varying vec4 vertexColor; "
        "void main() { gl_Position = transform * vec4(position, 0.0, 1.0); "
        "uv = texCoord; vertexColor = color; }";
    const char* vertex_color_texture_fragment_source =
        "uniform sampler2D colorTexture; varying vec2 uv; varying vec4 vertexColor; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) * "
        "vertexColor.stpq.bgra; }";
    const char* tinted_vertex_color_texture_fragment_source =
        "uniform sampler2D colorTexture; uniform vec4 tint; varying vec2 uv; "
        "varying vec4 vertexColor; void main() { gl_FragColor = "
        "texture2D(colorTexture, uv) * vertexColor * tint; }";
    const char* opacity_vertex_color_texture_fragment_source =
        "uniform sampler2D colorTexture; uniform float opacity; varying vec2 uv; "
        "varying vec4 vertexColor; void main() { gl_FragColor = "
        "texture2D(colorTexture, uv) * vertexColor * opacity; }";
    const char* tinted_opacity_vertex_color_texture_fragment_source =
        "uniform sampler2D colorTexture; uniform vec4 tint; uniform float opacity; "
        "varying vec2 uv; varying vec4 vertexColor; void main() { gl_FragColor = "
        "texture2D(colorTexture, uv) * vertexColor * tint * opacity; }";
    const char* two_texture_vertex_color_material_fragment_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform vec4 tint; uniform float opacity; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec4 vertexColor; void main() { "
        "gl_FragColor = (texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv)) * vertexColor * tint * opacity; }";

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    assert(vertex != 0u && fragment != 0u);

    header = lower_and_read_header(vertex, scalar_source, blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 1u);
    assert(header.output_count == 1u);
    assert(header.instruction_count >= 5u);
    assert(header.register_count >= 4u);

    assert(ringl_realize_shader_module(vertex) == 0);
    first_module = ringl_get_shader_module(vertex);
    assert(first_module != 0u);
    assert(backend.shader_creates == 1u);

    assert(ringl_realize_shader_module(vertex) == 0);
    assert(ringl_get_shader_module(vertex) != 0u);
    assert(ringl_get_shader_module(vertex) != first_module);
    assert(backend.shader_creates == 2u);
    assert(backend.destroys == 1u);

    header = lower_and_read_header(vertex, vector_source, blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 2u);
    assert(header.output_count == 8u);
    assert(header.instruction_count >= 13u);
    assert(ringl_get_shader_module(vertex) == 0u);
    assert(backend.destroys == 2u);

    /* The parser has always accepted vector locals. Assert that these do not
     * merely compile: unary, matching-width, and scalar-broadcast arithmetic
     * must lower to executable scalar RSH1 operations. */
    header = lower_and_read_header(vertex, vector_arithmetic_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 4u);
    assert(header.output_count == 8u);
    assert(header.instruction_count >= 40u);
    assert(header.register_count >= 32u);

    /* A multi-component swizzle is a register permutation, not a separate
     * shader profile. vecN + scalar must still lower as one scalar operation
     * per component so the executable RSH1 module preserves GLSL order. */
    header = lower_and_read_header(vertex, swizzle_arithmetic_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 2u);
    assert(header.output_count == 8u);
    assert(header.instruction_count >= 17u);
    assert(header.register_count >= 11u);

    header = lower_and_read_header(fragment, fragment_source, blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.instruction_count >= 5u);

    header = lower_and_read_header(fragment, texture_source, blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 15u);

    header = lower_and_read_header(vertex, varying_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 4u);
    assert(header.output_count == 8u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 15u);

    header = lower_and_read_header(fragment, varying_fragment_source,
                                   blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 13u);

    header = lower_and_read_header(vertex, color_varying_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 6u);
    assert(header.output_count == 8u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 17u);

    header = lower_and_read_header(fragment, color_varying_fragment_source,
                                   blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 9u);

    header = lower_and_read_header(vertex, two_vec2_varying_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 6u);
    assert(header.output_count == 8u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 17u);

    header = lower_and_read_header(fragment, two_vec2_varying_fragment_source,
                                   blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 9u);

    header = lower_and_read_header(vertex, color3_varying_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 5u);
    assert(header.output_count == 8u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 16u);
    assert(header.register_count == 7u);

    header = lower_and_read_header(fragment, color3_varying_fragment_source,
                                   blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 9u);

    /* The transformed texture route emits every scalar UV interface that the
     * native pipeline accepts: one and two pairs use eight vertex outputs,
     * while the third and fourth pairs extend the interface to ten and
     * twelve. These are executable matrix/RSH1 modules, not CPU-expanded
     * texture coordinates. */
    header = lower_and_read_header(vertex, transformed_two_uv_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 6u);
    assert(header.output_count == 8u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 61u);

    header = lower_and_read_header(vertex, transformed_three_uv_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 8u);
    assert(header.output_count == 10u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 65u);

    header = lower_and_read_header(vertex, transformed_four_uv_vertex_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 10u);
    assert(header.output_count == 12u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 69u);

    /* The RSH1 native varying interface stops at eight interpolated scalars.
     * A source outside that exact profile must not accidentally publish a
     * truncated module. */
    ringl_shader_source(vertex, transformed_five_uv_vertex_source, -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(vertex) != 0);
    assert(ringl_get_shader_rsh1_size(vertex) == 0u);

    /* A texture sample can be modulated by an interpolated vertex RGBA value
     * on the six-scalar native interface. Both the matrix/attribute route and
     * the fragment multiply are executable RSH1, not a browser-side tint. */
    header = lower_and_read_header(vertex,
                                   transformed_vertex_color_texture_source,
                                   blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 8u);
    assert(header.output_count == 10u);
    assert(header.resource_count == 0u);
    assert(header.instruction_count == 65u);

    header = lower_and_read_header(fragment, vertex_color_texture_fragment_source,
                                   blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 6u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 19u);

    header = lower_and_read_header(
        fragment, tinted_vertex_color_texture_fragment_source,
        blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 6u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 27u);
    assert(header.register_count == 22u);

    header = lower_and_read_header(
        fragment, opacity_vertex_color_texture_fragment_source,
        blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 6u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 24u);
    assert(header.register_count == 19u);

    /* A material opacity is an RSH1 scalar broadcast after the sampled RGBA,
     * vertex color, and tint products. It is not folded into CPU-side color
     * data, so changing it retains the native image/sampler resource layout. */
    header = lower_and_read_header(
        fragment, tinted_opacity_vertex_color_texture_fragment_source,
        blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 6u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);
    assert(header.instruction_count == 32u);
    assert(header.register_count == 27u);

    /* Two UV/sampler pairs are added by RSH1 before vertex color, tint, and
     * opacity modulation. The header proves both typed resource pairs remain
     * part of the executable rather than being pre-composed by the host. */
    header = lower_and_read_header(
        fragment, two_texture_vertex_color_material_fragment_source,
        blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 8u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 4u);
    assert(header.instruction_count == 42u);
    assert(header.register_count == 37u);

    ringl_shader_source(vertex, "void main() { gl_Position = 0.0; }", -1);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
    assert(ringl_get_shader_rsh1_size(vertex) == 0u);
    assert(ringl_get_shader_module(vertex) == 0u);

    /* Do not silently consume an incomplete precision declaration. The
     * frontend accepts only the exact GLES qualifier/type grammar it can map
     * to the binary32 RSH1 execution domain. */
    ringl_shader_source(fragment,
                        "precision float; void main() { "
                        "gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);

    ringl_context_destroy(context);
    return 0;
}
