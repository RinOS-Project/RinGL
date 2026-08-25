/* SPDX-License-Identifier: MIT */
#ifndef RINGL_RSH1_ABI_H
#define RINGL_RSH1_ABI_H

#include <stdint.h>

#define RINGL_RSH1_MAGIC UINT32_C(0x31485352)
#define RINGL_RSH1_VERSION 1u
#define RINGL_RSH1_UNUSED UINT16_C(0xffff)
#define RINGL_RSH1_MAX_INSTRUCTIONS 128u
/* Eight independently sampled texture uniforms in the bounded fragment
 * profile need 80 scalar registers (coordinates, RGBA samples, raster
 * inputs, and the accumulation chain). RinGPU accepts 256, so retain a
 * compact RinGL-side limit while leaving headroom for that supported path. */
#define RINGL_RSH1_MAX_REGISTERS 96u

#define RINGL_RSH1_SAMPLE_COMPONENT_RED   0u
#define RINGL_RSH1_SAMPLE_COMPONENT_GREEN 1u
#define RINGL_RSH1_SAMPLE_COMPONENT_BLUE  2u
#define RINGL_RSH1_SAMPLE_COMPONENT_ALPHA 3u

enum RinGLRsh1Stage {
    RINGL_RSH1_STAGE_VERTEX = 1,
    RINGL_RSH1_STAGE_FRAGMENT = 2,
};

enum RinGLRsh1Opcode {
    RINGL_RSH1_OP_CONST_I32 = 1,
    RINGL_RSH1_OP_MOV = 2,
    RINGL_RSH1_OP_ADD_I32 = 3,
    RINGL_RSH1_OP_MUL_I32 = 4,
    RINGL_RSH1_OP_RETURN = 13,
    RINGL_RSH1_OP_CONST_F32 = 16,
    RINGL_RSH1_OP_SUB_I32 = 17,
    RINGL_RSH1_OP_DIV_I32 = 18,
    RINGL_RSH1_OP_MOD_I32 = 19,
    RINGL_RSH1_OP_ADD_F32 = 20,
    RINGL_RSH1_OP_SUB_F32 = 21,
    RINGL_RSH1_OP_MUL_F32 = 22,
    RINGL_RSH1_OP_DIV_F32 = 23,
    /* These opcode numbers are shared with RinShader. The bounded GLSL
     * min/max/clamp lowering emits them directly instead of folding author
     * values or asking the embedding to evaluate a component. */
    RINGL_RSH1_OP_MIN_F32 = 24,
    RINGL_RSH1_OP_MAX_F32 = 25,
    RINGL_RSH1_OP_I32_TO_F32 = 43,
    RINGL_RSH1_OP_F32_TO_I32 = 44,
    RINGL_RSH1_OP_LOAD_INPUT_F32 = 45,
    RINGL_RSH1_OP_STORE_OUTPUT_F32 = 46,
    /* RSH1 shares its wire opcode numbers with the RinGPU verifier. Keep these
     * builtin loads explicit instead of lowering point-sprite coordinates to
     * synthetic varyings: the rasterizer owns their per-fragment values. */
    RINGL_RSH1_OP_LOAD_BUILTIN_F32 = 52,
    RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32 = 55,
    RINGL_RSH1_OP_DFDX_F32 = 56,
    RINGL_RSH1_OP_DFDY_F32 = 57,
    RINGL_RSH1_OP_FWIDTH_F32 = 58,
};

enum RinGLRsh1Builtin {
    /* Keep the numbers synchronized with RinShaderBuiltin. These values are
     * part of the RSH1 bytecode ABI emitted by RinGL, not GLSL token values. */
    RINGL_RSH1_BUILTIN_POINT_COORD_X = 16,
    RINGL_RSH1_BUILTIN_POINT_COORD_Y = 17,
};

typedef struct __attribute__((packed)) RinGLRsh1HeaderV1 {
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
} RinGLRsh1HeaderV1;

typedef struct __attribute__((packed)) RinGLRsh1InstructionV1 {
    uint16_t opcode;
    uint16_t flags;
    uint16_t destination;
    uint16_t source0;
    uint16_t source1;
    uint16_t resource;
    uint32_t immediate;
} RinGLRsh1InstructionV1;

_Static_assert(sizeof(RinGLRsh1HeaderV1) == 64u, "RSH1 header ABI drift");
_Static_assert(sizeof(RinGLRsh1InstructionV1) == 16u, "RSH1 instruction ABI drift");

#endif
