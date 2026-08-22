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

int main(void)
{
    RinGLContextDescV1 desc = {0};
    RinGLContext* context = NULL;
    uint32_t shader;
    uint32_t size;
    uint8_t blob[4096];
    Header header;
    const char* source =
        "attribute float x;\n"
        "void main() {\n"
        "  float y = x * 2.0;\n"
        "  gl_Position = y + 1.0;\n"
        "}\n";

    desc.struct_size = sizeof(desc);
    desc.api_version = RINGL_API_VERSION;
    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    shader = ringl_create_shader(RINGL_VERTEX_SHADER);
    assert(shader != 0u);
    ringl_shader_source(shader, source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);

    size = ringl_get_shader_rsh1_size(shader);
    assert(size >= sizeof(Header));
    assert(size <= sizeof(blob));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.magic == RSH1_MAGIC);
    assert(header.version == 1u);
    assert(header.stage == 1u);
    assert(header.total_size == size);
    assert(header.input_count == 1u);
    assert(header.output_count == 1u);
    assert(header.instruction_count >= 5u);
    assert(header.register_count >= 4u);

    ringl_shader_source(shader, "void main() { gl_Position = 0.0; }", -1);
    assert(ringl_get_shader_compile_status(shader) == RINGL_FALSE);
    assert(ringl_get_shader_rsh1_size(shader) == 0u);

    ringl_context_destroy(context);
    return 0;
}
