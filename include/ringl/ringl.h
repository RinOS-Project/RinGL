/* SPDX-License-Identifier: MIT */
#ifndef RINGL_RINGL_H
#define RINGL_RINGL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RINGL_API_VERSION 1u

#define RINGL_NO_ERROR          0x0000u
#define RINGL_INVALID_ENUM      0x0500u
#define RINGL_INVALID_VALUE     0x0501u
#define RINGL_INVALID_OPERATION 0x0502u
#define RINGL_OUT_OF_MEMORY     0x0505u
#define RINGL_CONTEXT_LOST_WEBGL 0x9242u

/* RinGPU adapters report this exact status when their device can no longer
 * execute commands. RinGL converts it to the sticky GL-visible context-loss
 * state instead of treating it as an ordinary INVALID_OPERATION. */
#define RINGL_RIN_GPU_ERROR_DEVICE_LOST (-10)

#define RINGL_FALSE 0u
#define RINGL_TRUE  1u
#define RINGL_ZERO  0u
#define RINGL_ONE   1u
#define RINGL_BYTE           0x1400u
#define RINGL_UNSIGNED_BYTE  0x1401u
#define RINGL_SHORT          0x1402u
#define RINGL_UNSIGNED_SHORT 0x1403u
#define RINGL_UNSIGNED_SHORT_4_4_4_4 0x8033u
#define RINGL_UNSIGNED_SHORT_5_5_5_1 0x8034u
#define RINGL_UNSIGNED_SHORT_5_6_5 0x8363u
#define RINGL_UNSIGNED_INT   0x1405u
#define RINGL_FLOAT          0x1406u
#define RINGL_FLOAT_VEC2     0x8b50u
#define RINGL_FLOAT_VEC3     0x8b51u
#define RINGL_FLOAT_VEC4     0x8b52u
#define RINGL_SAMPLER_2D     0x8b5eu

#define RINGL_ACTIVE_INFO_NAME_MAX 64u

#define RINGL_POINTS           0x0000u
#define RINGL_LINES            0x0001u
#define RINGL_LINE_LOOP        0x0002u
#define RINGL_LINE_STRIP       0x0003u
#define RINGL_TRIANGLES        0x0004u
#define RINGL_TRIANGLE_STRIP   0x0005u
#define RINGL_TRIANGLE_FAN     0x0006u
#define RINGL_STENCIL_BUFFER_BIT 0x00000400u
#define RINGL_DEPTH_BUFFER_BIT 0x00000100u
#define RINGL_COLOR_BUFFER_BIT 0x00004000u

#define RINGL_NEVER    0x0200u
#define RINGL_LESS     0x0201u
#define RINGL_EQUAL    0x0202u
#define RINGL_LEQUAL   0x0203u
#define RINGL_GREATER  0x0204u
#define RINGL_NOTEQUAL 0x0205u
#define RINGL_GEQUAL   0x0206u
#define RINGL_ALWAYS   0x0207u

#define RINGL_KEEP      0x1e00u
#define RINGL_REPLACE   0x1e01u
#define RINGL_INCR      0x1e02u
#define RINGL_DECR      0x1e03u
#define RINGL_INVERT    0x150au
#define RINGL_INCR_WRAP 0x8507u
#define RINGL_DECR_WRAP 0x8508u

#define RINGL_SRC_COLOR               0x0300u
#define RINGL_ONE_MINUS_SRC_COLOR     0x0301u
#define RINGL_SRC_ALPHA               0x0302u
#define RINGL_ONE_MINUS_SRC_ALPHA     0x0303u
#define RINGL_DST_ALPHA               0x0304u
#define RINGL_ONE_MINUS_DST_ALPHA     0x0305u
#define RINGL_DST_COLOR               0x0306u
#define RINGL_ONE_MINUS_DST_COLOR     0x0307u
#define RINGL_SRC_ALPHA_SATURATE      0x0308u
#define RINGL_CONSTANT_COLOR           0x8001u
#define RINGL_ONE_MINUS_CONSTANT_COLOR 0x8002u
#define RINGL_CONSTANT_ALPHA           0x8003u
#define RINGL_ONE_MINUS_CONSTANT_ALPHA 0x8004u
#define RINGL_FUNC_ADD                0x8006u
#define RINGL_MIN                     0x8007u
#define RINGL_MAX                     0x8008u
#define RINGL_BLEND_COLOR             0x8005u
#define RINGL_BLEND_EQUATION_RGB      0x8009u
#define RINGL_FUNC_SUBTRACT           0x800au
#define RINGL_FUNC_REVERSE_SUBTRACT   0x800bu
#define RINGL_BLEND_DST_RGB           0x80c8u
#define RINGL_BLEND_SRC_RGB           0x80c9u
#define RINGL_BLEND_DST_ALPHA         0x80cau
#define RINGL_BLEND_SRC_ALPHA         0x80cbu
#define RINGL_BLEND_EQUATION_ALPHA    0x883du

#define RINGL_FRONT          0x0404u
#define RINGL_BACK           0x0405u
#define RINGL_FRONT_AND_BACK 0x0408u
#define RINGL_CW             0x0900u
#define RINGL_CCW            0x0901u
#define RINGL_CULL_FACE      0x0b44u
#define RINGL_DEPTH_TEST     0x0b71u
#define RINGL_POLYGON_OFFSET_FILL 0x8037u
#define RINGL_SAMPLE_COVERAGE 0x80a0u
#define RINGL_LINE_WIDTH     0x0b21u
#define RINGL_STENCIL_TEST   0x0b90u
#define RINGL_STENCIL_FUNC       0x0b92u
#define RINGL_STENCIL_VALUE_MASK 0x0b93u
#define RINGL_STENCIL_FAIL       0x0b94u
#define RINGL_STENCIL_PASS_DEPTH_FAIL 0x0b95u
#define RINGL_STENCIL_PASS_DEPTH_PASS 0x0b96u
#define RINGL_STENCIL_REF        0x0b97u
#define RINGL_STENCIL_WRITEMASK  0x0b98u
#define RINGL_STENCIL_BACK_FUNC       0x8800u
#define RINGL_STENCIL_BACK_FAIL       0x8801u
#define RINGL_STENCIL_BACK_PASS_DEPTH_FAIL 0x8802u
#define RINGL_STENCIL_BACK_PASS_DEPTH_PASS 0x8803u
#define RINGL_STENCIL_BACK_REF        0x8ca3u
#define RINGL_STENCIL_BACK_VALUE_MASK 0x8ca4u
#define RINGL_STENCIL_BACK_WRITEMASK  0x8ca5u
#define RINGL_DEPTH_WRITEMASK 0x0b72u
#define RINGL_DEPTH_FUNC     0x0b74u
#define RINGL_BLEND          0x0be2u
#define RINGL_DEPTH_CLEAR_VALUE 0x0b73u
#define RINGL_STENCIL_CLEAR_VALUE 0x0b91u
#define RINGL_COLOR_CLEAR_VALUE 0x0c22u
#define RINGL_COLOR_WRITEMASK 0x0c23u
#define RINGL_SCISSOR_TEST   0x0c11u
#define RINGL_UNPACK_ALIGNMENT 0x0cf5u

#define RINGL_VIEWPORT                      0x0ba2u
#define RINGL_SCISSOR_BOX                   0x0c10u
#define RINGL_MAX_TEXTURE_SIZE_QUERY        0x0d33u
#define RINGL_TEXTURE_BINDING_2D            0x8069u
#define RINGL_ACTIVE_TEXTURE                0x84e0u
#define RINGL_MAX_VERTEX_ATTRIBS_QUERY      0x8869u
#define RINGL_MAX_TEXTURE_IMAGE_UNITS       0x8872u
#define RINGL_ARRAY_BUFFER_BINDING          0x8894u
#define RINGL_ELEMENT_ARRAY_BUFFER_BINDING  0x8895u
#define RINGL_CURRENT_PROGRAM               0x8b8du
#define RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8b4du
#define RINGL_CULL_FACE_MODE                0x0b45u
#define RINGL_FRONT_FACE                    0x0b46u
#define RINGL_DEPTH_RANGE                   0x0b70u
#define RINGL_ALIASED_LINE_WIDTH_RANGE       0x846eu
#define RINGL_SAMPLE_COVERAGE_VALUE           0x80a8u
#define RINGL_SAMPLE_COVERAGE_INVERT          0x80abu
#define RINGL_GENERATE_MIPMAP_HINT             0x8192u
#define RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT  0x8b8bu
#define RINGL_DONT_CARE                        0x1100u
#define RINGL_FASTEST                          0x1101u
#define RINGL_NICEST                           0x1102u

#define RINGL_ARRAY_BUFFER          0x8892u
#define RINGL_ELEMENT_ARRAY_BUFFER  0x8893u
#define RINGL_BUFFER_SIZE            0x8764u
#define RINGL_BUFFER_USAGE           0x8765u
#define RINGL_STREAM_DRAW           0x88e0u
#define RINGL_STATIC_DRAW           0x88e4u
#define RINGL_DYNAMIC_DRAW          0x88e8u

#define RINGL_TEXTURE_2D 0x0de1u
#define RINGL_TEXTURE0   0x84c0u
#define RINGL_ALPHA      0x1906u
#define RINGL_RGB        0x1907u
#define RINGL_RGBA       0x1908u
#define RINGL_RGBA4      0x8056u
#define RINGL_RGB5_A1    0x8057u
#define RINGL_RGB565     0x8d62u
#define RINGL_DEPTH_COMPONENT 0x1902u
#define RINGL_LUMINANCE  0x1909u
#define RINGL_LUMINANCE_ALPHA 0x190au
#define RINGL_NONE       0u
#define RINGL_TEXTURE    0x1702u
#define RINGL_DEPTH_STENCIL 0x84f9u
#define RINGL_RGBA8      0x8058u
#define RINGL_FLOAT       0x1406u
#define RINGL_UNSIGNED_INT_24_8 0x84fau
#define RINGL_TEXTURE_MAG_FILTER 0x2800u
#define RINGL_TEXTURE_MIN_FILTER 0x2801u
#define RINGL_TEXTURE_WRAP_S     0x2802u
#define RINGL_TEXTURE_WRAP_T     0x2803u
#define RINGL_NEAREST                0x2600u
#define RINGL_LINEAR                 0x2601u
#define RINGL_NEAREST_MIPMAP_NEAREST 0x2700u
#define RINGL_LINEAR_MIPMAP_NEAREST  0x2701u
#define RINGL_NEAREST_MIPMAP_LINEAR  0x2702u
#define RINGL_LINEAR_MIPMAP_LINEAR   0x2703u
#define RINGL_REPEAT          0x2901u
#define RINGL_CLAMP_TO_EDGE   0x812fu
#define RINGL_MIRRORED_REPEAT 0x8370u
#define RINGL_MAX_TEXTURE_UNITS 8u
#define RINGL_MAX_TEXTURE_SIZE  4096u

#define RINGL_FRAMEBUFFER          0x8d40u
#define RINGL_RENDERBUFFER          0x8d41u
#define RINGL_FRAMEBUFFER_BINDING   0x8ca6u
#define RINGL_RENDERBUFFER_BINDING  0x8ca7u
#define RINGL_RENDERBUFFER_WIDTH            0x8d42u
#define RINGL_RENDERBUFFER_HEIGHT           0x8d43u
#define RINGL_RENDERBUFFER_INTERNAL_FORMAT  0x8d44u
#define RINGL_RENDERBUFFER_RED_SIZE         0x8d50u
#define RINGL_RENDERBUFFER_GREEN_SIZE       0x8d51u
#define RINGL_RENDERBUFFER_BLUE_SIZE        0x8d52u
#define RINGL_RENDERBUFFER_ALPHA_SIZE       0x8d53u
#define RINGL_RENDERBUFFER_DEPTH_SIZE       0x8d54u
#define RINGL_RENDERBUFFER_STENCIL_SIZE     0x8d55u
#define RINGL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8cd0u
#define RINGL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8cd1u
#define RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL 0x8cd2u
#define RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE 0x8cd3u
#define RINGL_COLOR_ATTACHMENT0     0x8ce0u
#define RINGL_DEPTH_ATTACHMENT      0x8d00u
#define RINGL_STENCIL_ATTACHMENT    0x8d20u
#define RINGL_DEPTH_STENCIL_ATTACHMENT 0x821au
#define RINGL_DEPTH_COMPONENT16     0x81a5u
#define RINGL_STENCIL_INDEX8         0x8d48u
#define RINGL_DEPTH_COMPONENT32F    0x8cacu
#define RINGL_DEPTH24_STENCIL8       0x88f0u
#define RINGL_FRAMEBUFFER_COMPLETE              0x8cd5u
#define RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8cd6u
#define RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8cd7u

#define RINGL_FRAMEBUFFER_ATTACHMENT_NONE          0u
#define RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D    1u
#define RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER  2u

#define RINGL_VERTEX_SHADER   0x8b31u
#define RINGL_FRAGMENT_SHADER 0x8b30u

/* WebGL 1 shader/program query names. */
#define RINGL_DELETE_STATUS      0x8b80u
#define RINGL_COMPILE_STATUS     0x8b81u
#define RINGL_LINK_STATUS        0x8b82u
#define RINGL_VALIDATE_STATUS    0x8b83u
#define RINGL_ATTACHED_SHADERS   0x8b85u
#define RINGL_ACTIVE_UNIFORMS    0x8b86u
#define RINGL_ACTIVE_ATTRIBUTES  0x8b89u
#define RINGL_SHADER_TYPE        0x8b4fu

#define RINGL_MAX_VERTEX_ATTRIBS 16u

/* WebGL 1 vertex-attribute query names. */
#define RINGL_CURRENT_VERTEX_ATTRIB                 0x8626u
#define RINGL_VERTEX_ATTRIB_ARRAY_ENABLED           0x8622u
#define RINGL_VERTEX_ATTRIB_ARRAY_SIZE              0x8623u
#define RINGL_VERTEX_ATTRIB_ARRAY_STRIDE            0x8624u
#define RINGL_VERTEX_ATTRIB_ARRAY_TYPE              0x8625u
#define RINGL_VERTEX_ATTRIB_ARRAY_NORMALIZED        0x886au
#define RINGL_VERTEX_ATTRIB_ARRAY_POINTER           0x8645u
#define RINGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING    0x889fu

#define RINGL_DIRTY_PIPELINE    0x00000001u
#define RINGL_DIRTY_BINDINGS    0x00000002u
#define RINGL_DIRTY_FRAMEBUFFER 0x00000004u
#define RINGL_DIRTY_VIEWPORT    0x00000008u
#define RINGL_DIRTY_ALL         0x0000000fu

#define RINGL_RIN_GPU_QUEUE_GRAPHICS       0x00000004u
#define RINGL_RIN_GPU_IMAGE_UNDEFINED      0u
#define RINGL_RIN_GPU_IMAGE_COLOR_TARGET   3u
#define RINGL_RIN_GPU_IMAGE_PRESENT        4u
#define RINGL_RIN_GPU_IMAGE_DEPTH_TARGET   5u
#define RINGL_RIN_GPU_IMAGE_SHADER_READ    6u
#define RINGL_RIN_GPU_RENDER_LOAD          1u
#define RINGL_RIN_GPU_RENDER_CLEAR         2u
#define RINGL_RIN_GPU_RENDER_STORE         1u
#define RINGL_RIN_GPU_INDEX_UINT16         1u
#define RINGL_RIN_GPU_INDEX_UINT32         2u
#define RINGL_RIN_GPU_INDEX_UINT8          3u
#define RINGL_RIN_GPU_FORMAT_RGBA8_UNORM   2u
#define RINGL_RIN_GPU_FORMAT_D32_FLOAT     4u
#define RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT 5u
/* Native stencil-only image. It must never be used for depth comparison or
 * writes. */
#define RINGL_RIN_GPU_FORMAT_S8_UINT       9u
#define RINGL_RIN_GPU_FORMAT_RGB565_UNORM  6u
#define RINGL_RIN_GPU_FORMAT_RGBA4_UNORM    7u
#define RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM  8u
#define RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION 0x1u
#define RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED          0x2u
#define RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET     0x4u
#define RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE      0x8u
#define RINGL_RIN_GPU_IMAGE_USAGE_DEPTH_STENCIL    0x10u
#define RINGL_RIN_GPU_SAMPLER_NEAREST      1u
#define RINGL_RIN_GPU_SAMPLER_LINEAR       2u
#define RINGL_RIN_GPU_ADDRESS_CLAMP        1u
#define RINGL_RIN_GPU_ADDRESS_REPEAT       2u
#define RINGL_RIN_GPU_ADDRESS_MIRRORED     3u
#define RINGL_RIN_GPU_RESOURCE_READ        1u
#define RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE 2u
#define RINGL_RIN_GPU_RESOURCE_SAMPLER       3u
#define RINGL_RIN_GPU_COMPARE_LESS          1u
#define RINGL_RIN_GPU_COMPARE_LEQUAL        2u
#define RINGL_RIN_GPU_COMPARE_ALWAYS        3u
#define RINGL_RIN_GPU_COMPARE_NEVER         4u
#define RINGL_RIN_GPU_COMPARE_EQUAL         5u
#define RINGL_RIN_GPU_COMPARE_GREATER       6u
#define RINGL_RIN_GPU_COMPARE_NOTEQUAL      7u
#define RINGL_RIN_GPU_COMPARE_GEQUAL        8u
#define RINGL_RIN_GPU_STENCIL_KEEP           1u
#define RINGL_RIN_GPU_STENCIL_ZERO           2u
#define RINGL_RIN_GPU_STENCIL_REPLACE        3u
#define RINGL_RIN_GPU_STENCIL_INCREMENT_CLAMP 4u
#define RINGL_RIN_GPU_STENCIL_DECREMENT_CLAMP 5u
#define RINGL_RIN_GPU_STENCIL_INVERT         6u
#define RINGL_RIN_GPU_STENCIL_INCREMENT_WRAP 7u
#define RINGL_RIN_GPU_STENCIL_DECREMENT_WRAP 8u
#define RINGL_RIN_GPU_BLEND_ZERO             1u
#define RINGL_RIN_GPU_BLEND_ONE              2u
#define RINGL_RIN_GPU_BLEND_SRC_ALPHA        3u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_SRC_ALPHA 4u
#define RINGL_RIN_GPU_BLEND_DST_ALPHA        5u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_DST_ALPHA 6u
#define RINGL_RIN_GPU_BLEND_SRC_COLOR        7u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_SRC_COLOR 8u
#define RINGL_RIN_GPU_BLEND_DST_COLOR        9u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_DST_COLOR 10u
#define RINGL_RIN_GPU_BLEND_SRC_ALPHA_SATURATE 11u
#define RINGL_RIN_GPU_BLEND_CONSTANT_COLOR 12u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR 13u
#define RINGL_RIN_GPU_BLEND_CONSTANT_ALPHA 14u
#define RINGL_RIN_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA 15u
#define RINGL_RIN_GPU_BLEND_ADD              1u
#define RINGL_RIN_GPU_BLEND_SUBTRACT         2u
#define RINGL_RIN_GPU_BLEND_REVERSE_SUBTRACT 3u
#define RINGL_RIN_GPU_BLEND_MINIMUM          4u
#define RINGL_RIN_GPU_BLEND_MAXIMUM          5u
#define RINGL_RIN_GPU_CULL_NONE              1u
#define RINGL_RIN_GPU_CULL_FRONT             2u
#define RINGL_RIN_GPU_CULL_BACK              3u
#define RINGL_RIN_GPU_FRONT_FACE_CCW          1u
#define RINGL_RIN_GPU_FRONT_FACE_CW           2u
#define RINGL_RIN_GPU_COLOR_WRITE_RED         0x1u
#define RINGL_RIN_GPU_COLOR_WRITE_GREEN       0x2u
#define RINGL_RIN_GPU_COLOR_WRITE_BLUE        0x4u
#define RINGL_RIN_GPU_COLOR_WRITE_ALPHA       0x8u
#define RINGL_RIN_GPU_COLOR_WRITE_ALL         0x0fu
/* A constant vertex input is encoded as IEEE-754 binary32 bits in the
 * attribute offset field. It is deliberately opt-in at binding time so a
 * backend compiled before this contract cannot mistake the bits for an
 * address into a vertex buffer. */
#define RINGL_RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32 0x00000001u
#define RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32     0x00000001u
/* Independent vertex bindings carry a dense binding number, stride and
 * buffer handle for each enabled array source. It is additive to the V1
 * single-buffer ABI and must be advertised with matching V2 callbacks. */
#define RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER          0x00000002u

typedef struct RinGLContext RinGLContext;

typedef struct RinGLRinGpuVertexAttributeV1 {
    uint32_t location;
    uint32_t format;
    uint32_t offset;
    uint32_t reserved0;
    uint32_t flags;
} RinGLRinGpuVertexAttributeV1;

typedef struct RinGLRinGpuVertexAttributeV2 {
    uint32_t location;
    uint32_t format;
    uint32_t offset;
    uint32_t reserved0;
    uint32_t flags;
    uint32_t binding;
    uint32_t reserved1;
} RinGLRinGpuVertexAttributeV2;

typedef struct RinGLRinGpuVertexBufferLayoutV1 {
    uint32_t binding;
    uint32_t stride;
    uint32_t flags;
    uint32_t reserved0;
} RinGLRinGpuVertexBufferLayoutV1;

typedef struct RinGLRinGpuVertexBufferBindingV1 {
    uint32_t binding;
    uint32_t reserved0;
    uint64_t buffer;
    uint64_t offset;
} RinGLRinGpuVertexBufferBindingV1;

typedef struct RinGLRinGpuGraphicsPipelineV1 {
    uint64_t vertex_shader;
    uint64_t fragment_shader;
    uint32_t color_format;
    uint32_t primitive_topology;
    uint32_t vertex_stride;
    uint32_t attribute_count;
} RinGLRinGpuGraphicsPipelineV1;

typedef struct RinGLRinGpuGraphicsPipelineNativeV1 {
    uint64_t vertex_shader;
    uint64_t fragment_shader;
    uint32_t color_format;
    uint32_t primitive_topology;
    uint32_t vertex_stride;
    uint32_t position_output_location;
    uint32_t depth_format;
    uint32_t depth_compare;
    uint32_t depth_write_enabled;
    uint32_t blend_enabled;
    uint32_t source_color_factor;
    uint32_t destination_color_factor;
    uint32_t color_operation;
    uint32_t source_alpha_factor;
    uint32_t destination_alpha_factor;
    uint32_t alpha_operation;
    uint32_t color_write_mask;
    uint32_t cull_mode;
    uint32_t front_face;
    uint32_t reserved0;
    /* Front-face stencil state. A zero enable bit requires every remaining
     * stencil field to be zero; D32_FLOAT_S8_UINT is required when enabled. */
    uint32_t stencil_test_enabled;
    uint32_t stencil_compare;
    uint32_t stencil_reference;
    uint32_t stencil_read_mask;
    uint32_t stencil_write_mask;
    uint32_t stencil_fail_operation;
    uint32_t stencil_depth_fail_operation;
    uint32_t stencil_pass_operation;
    /* When separate_stencil_enabled is zero the back fields are canonical
     * zero and the front state applies to both faces. */
    uint32_t separate_stencil_enabled;
    uint32_t back_stencil_compare;
    uint32_t back_stencil_reference;
    uint32_t back_stencil_read_mask;
    uint32_t back_stencil_write_mask;
    uint32_t back_stencil_fail_operation;
    uint32_t back_stencil_depth_fail_operation;
    uint32_t back_stencil_pass_operation;
} RinGLRinGpuGraphicsPipelineNativeV1;

/* Additive native pipeline descriptor. Its V1 prefix remains byte-for-byte
 * stable, while V2 carries the immutable constant blend color. */
typedef struct RinGLRinGpuGraphicsPipelineNativeV2 {
    RinGLRinGpuGraphicsPipelineNativeV1 base;
    float blend_constant_red;
    float blend_constant_green;
    float blend_constant_blue;
    float blend_constant_alpha;
} RinGLRinGpuGraphicsPipelineNativeV2;

typedef struct RinGLRinGpuVaryingV1 {
    uint32_t vertex_output_location;
    uint32_t fragment_input_location;
    uint32_t type;
    uint32_t interpolation;
} RinGLRinGpuVaryingV1;

typedef struct RinGLRinGpuRasterStateV1 {
    float viewport_x;
    float viewport_y;
    float viewport_width;
    float viewport_height;
    float min_depth;
    float max_depth;
    int32_t scissor_x;
    int32_t scissor_y;
    uint32_t scissor_width;
    uint32_t scissor_height;
    uint32_t scissor_enabled;
    uint32_t polygon_offset_fill_enabled;
    float polygon_offset_factor;
    float polygon_offset_units;
    float line_width;
    uint32_t sample_coverage_enabled;
    float sample_coverage_value;
    uint32_t sample_coverage_invert;
    uint32_t reserved0;
} RinGLRinGpuRasterStateV1;

typedef struct RinGLRinGpuGraphicsBindingV1 {
    uint32_t binding;
    uint32_t kind;
    uint32_t access;
    uint32_t reserved0;
    uint64_t resource;
    uint32_t mip_level;
    uint32_t array_layer;
} RinGLRinGpuGraphicsBindingV1;

/* Clear regions use the WebGL lower-left pixel origin. A disabled region has
 * canonical zero coordinates and clears the complete attachment; an enabled
 * empty region is a valid no-op. */
typedef struct RinGLRinGpuClearRegionV1 {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t enabled;
    uint32_t reserved0;
} RinGLRinGpuClearRegionV1;

typedef struct RinGLRinGpuRenderPassV1 {
    uint64_t color_target;
    uint32_t load_op;
    uint32_t store_op;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    /* R/G/B/A bits, used only with CLEAR. */
    uint32_t color_write_mask;
    uint32_t reserved0;
    RinGLRinGpuClearRegionV1 clear_region;
} RinGLRinGpuRenderPassV1;

typedef struct RinGLRinGpuRenderPassDepthV1 {
    uint64_t color_target;
    uint64_t depth_target;
    uint32_t color_load_op;
    uint32_t color_store_op;
    uint32_t depth_load_op;
    uint32_t depth_store_op;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    float clear_depth;
    /* D32_FLOAT targets require these to remain zero. D32_FLOAT_S8_UINT
     * accepts LOAD/CLEAR and STORE for stencil. A stencil clear uses the
     * front-face write mask, as required by OpenGL ES 2.0. */
    uint32_t stencil_load_op;
    uint32_t stencil_store_op;
    uint32_t clear_stencil;
    uint32_t stencil_write_mask;
    /* R/G/B/A bits, used only with a color CLEAR. */
    uint32_t color_write_mask;
    uint32_t reserved0;
    RinGLRinGpuClearRegionV1 clear_region;
} RinGLRinGpuRenderPassDepthV1;

/* Native separate D32_FLOAT + S8_UINT attachment render pass. A pipeline
 * targets the logical D32_FLOAT_S8_UINT profile while the backend preserves
 * each aspect in its own image. */
typedef struct RinGLRinGpuRenderPassDepthStencilV1 {
    uint64_t color_target;
    uint64_t depth_target;
    uint64_t stencil_target;
    uint32_t color_load_op;
    uint32_t color_store_op;
    uint32_t depth_load_op;
    uint32_t depth_store_op;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    float clear_depth;
    uint32_t stencil_load_op;
    uint32_t stencil_store_op;
    uint32_t clear_stencil;
    uint32_t stencil_write_mask;
    uint32_t color_write_mask;
    uint32_t reserved0;
    RinGLRinGpuClearRegionV1 clear_region;
} RinGLRinGpuRenderPassDepthStencilV1;

typedef struct RinGLRinGpuDrawVerticesV1 {
    uint64_t pipeline;
    uint64_t color_target;
    uint64_t vertex_buffer;
    uint64_t vertex_offset;
    uint32_t vertex_count;
    uint32_t first_vertex;
    uint32_t instance_count;
    uint32_t first_instance;
} RinGLRinGpuDrawVerticesV1;

typedef struct RinGLRinGpuDrawVerticesV2 {
    uint64_t pipeline;
    uint64_t color_target;
    uint32_t vertex_count;
    uint32_t first_vertex;
    uint32_t instance_count;
    uint32_t first_instance;
    uint32_t binding_count;
    uint32_t reserved0;
    RinGLRinGpuVertexBufferBindingV1
        vertex_buffers[RINGL_MAX_VERTEX_ATTRIBS];
} RinGLRinGpuDrawVerticesV2;

typedef struct RinGLRinGpuDrawIndexedV1 {
    uint64_t pipeline;
    uint64_t color_target;
    uint64_t vertex_buffer;
    uint64_t index_buffer;
    uint64_t vertex_offset;
    uint64_t index_offset;
    uint32_t index_format;
    uint32_t index_count;
    uint32_t vertex_count;
    uint32_t first_index;
    uint32_t instance_count;
    uint32_t first_instance;
} RinGLRinGpuDrawIndexedV1;

typedef struct RinGLRinGpuDrawIndexedV2 {
    uint64_t pipeline;
    uint64_t color_target;
    uint64_t index_buffer;
    uint64_t index_offset;
    uint32_t index_format;
    uint32_t index_count;
    uint32_t vertex_count;
    uint32_t first_index;
    uint32_t instance_count;
    uint32_t first_instance;
    uint32_t binding_count;
    uint32_t reserved0;
    RinGLRinGpuVertexBufferBindingV1
        vertex_buffers[RINGL_MAX_VERTEX_ATTRIBS];
} RinGLRinGpuDrawIndexedV2;

typedef struct RinGLRinGpuSampledImage2DV1 {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t reserved0;
} RinGLRinGpuSampledImage2DV1;

typedef struct RinGLRinGpuImage2DV1 {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t usage;
} RinGLRinGpuImage2DV1;

typedef struct RinGLRinGpuImageUpload2DV1 {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t source_row_pitch_bytes;
} RinGLRinGpuImageUpload2DV1;

typedef struct RinGLRinGpuSamplerV1 {
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t mip_filter;
    uint32_t address_u;
    uint32_t address_v;
    uint32_t reserved0;
} RinGLRinGpuSamplerV1;

typedef int (*RinGLRinGpuCreateBufferFn)(void* session,
                                         uint64_t size_bytes,
                                         uint64_t* buffer_out);
typedef int (*RinGLRinGpuUploadBufferFn)(void* session,
                                         uint64_t buffer,
                                         uint64_t offset,
                                         const void* data,
                                         uint64_t size_bytes);
typedef int (*RinGLRinGpuDestroyObjectFn)(void* session, uint64_t object);
typedef int (*RinGLRinGpuCreateShaderModuleFn)(void* session,
                                               const void* rsh1,
                                               uint64_t size_bytes,
                                               uint64_t* shader_module_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineFn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineNativeFn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineNativeV2Fn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineVertexBindingsFn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineNativeVertexBindingsFn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateGraphicsPipelineNativeVertexBindingsV2Fn)(
    void* session,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
typedef int (*RinGLRinGpuCreateCommandListFn)(void* session,
                                              uint32_t capabilities,
                                              uint64_t* command_list_out);
typedef int (*RinGLRinGpuResetCommandListFn)(void* session,
                                             uint64_t command_list);
typedef int (*RinGLRinGpuTransitionImageFn)(void* session,
                                            uint64_t command_list,
                                            uint64_t image,
                                            uint32_t old_state,
                                            uint32_t new_state);
typedef int (*RinGLRinGpuBeginRenderPassFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuRenderPassV1* render_pass);
typedef int (*RinGLRinGpuBeginRenderPassDepthFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthV1* render_pass);
typedef int (*RinGLRinGpuBeginRenderPassDepthStencilFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthStencilV1* render_pass);
typedef int (*RinGLRinGpuSetRasterStateFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuRasterStateV1* state);
typedef int (*RinGLRinGpuCreateGraphicsBindGroupFn)(
    void* session, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV1* bindings,
    uint32_t binding_count, uint64_t* bind_group_out);
typedef int (*RinGLRinGpuBindGraphicsResourcesFn)(
    void* session, uint64_t command_list, uint64_t bind_group);
typedef int (*RinGLRinGpuDrawVerticesFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawVerticesV1* draw);
typedef int (*RinGLRinGpuDrawIndexedFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawIndexedV1* draw);
typedef int (*RinGLRinGpuDrawVerticesV2Fn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawVerticesV2* draw);
typedef int (*RinGLRinGpuDrawIndexedV2Fn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawIndexedV2* draw);
typedef int (*RinGLRinGpuEndRenderPassFn)(void* session,
                                          uint64_t command_list);
typedef int (*RinGLRinGpuPresentFn)(void* session,
                                    uint64_t command_list,
                                    uint64_t image,
                                    uint32_t display_id);
typedef int (*RinGLRinGpuCloseCommandListFn)(void* session,
                                             uint64_t command_list);
typedef int (*RinGLRinGpuQueueSubmitFn)(void* session,
                                        uint64_t queue,
                                        uint64_t command_list);
typedef int (*RinGLRinGpuCreateSampledImage2DFn)(
    void* session, const RinGLRinGpuSampledImage2DV1* desc,
    uint64_t* image_out);
typedef int (*RinGLRinGpuCreateImage2DFn)(
    void* session, const RinGLRinGpuImage2DV1* desc,
    uint64_t* image_out);
typedef int (*RinGLRinGpuUploadImage2DFn)(
    void* session, uint64_t image, const RinGLRinGpuImageUpload2DV1* upload,
    const void* data, uint64_t size_bytes);
typedef int (*RinGLRinGpuCreateSamplerFn)(
    void* session, const RinGLRinGpuSamplerV1* desc, uint64_t* sampler_out);

typedef struct RinGLRinGpuOpsV1 {
    uint32_t struct_size;
    uint32_t api_version;
    RinGLRinGpuCreateBufferFn create_buffer;
    RinGLRinGpuUploadBufferFn upload_buffer;
    RinGLRinGpuDestroyObjectFn destroy_object;
    RinGLRinGpuCreateShaderModuleFn create_shader_module;
    RinGLRinGpuCreateGraphicsPipelineFn create_graphics_pipeline;
    RinGLRinGpuCreateCommandListFn create_command_list;
    RinGLRinGpuResetCommandListFn reset_command_list;
    RinGLRinGpuTransitionImageFn transition_image;
    RinGLRinGpuBeginRenderPassFn begin_render_pass;
    RinGLRinGpuDrawVerticesFn draw_vertices;
    RinGLRinGpuEndRenderPassFn end_render_pass;
    RinGLRinGpuPresentFn present;
    RinGLRinGpuCloseCommandListFn close_command_list;
    RinGLRinGpuQueueSubmitFn queue_submit;
    RinGLRinGpuDrawIndexedFn draw_indexed;
    RinGLRinGpuCreateSampledImage2DFn create_sampled_image_2d;
    RinGLRinGpuUploadImage2DFn upload_image_2d;
    RinGLRinGpuCreateSamplerFn create_sampler;
    RinGLRinGpuCreateGraphicsPipelineNativeFn create_graphics_pipeline_native;
    RinGLRinGpuSetRasterStateFn set_raster_state;
    RinGLRinGpuCreateGraphicsBindGroupFn create_graphics_bind_group;
    RinGLRinGpuBindGraphicsResourcesFn bind_graphics_resources;
    RinGLRinGpuCreateImage2DFn create_image_2d;
    RinGLRinGpuBeginRenderPassDepthFn begin_render_pass_depth;
    RinGLRinGpuCreateGraphicsPipelineVertexBindingsFn
        create_graphics_pipeline_vertex_bindings;
    RinGLRinGpuCreateGraphicsPipelineNativeVertexBindingsFn
        create_graphics_pipeline_native_vertex_bindings;
    RinGLRinGpuDrawVerticesV2Fn draw_vertices_v2;
    RinGLRinGpuDrawIndexedV2Fn draw_indexed_v2;
    /* Optional V2 tail: needed only for constant blend factors. */
    RinGLRinGpuCreateGraphicsPipelineNativeV2Fn
        create_graphics_pipeline_native_v2;
    RinGLRinGpuCreateGraphicsPipelineNativeVertexBindingsV2Fn
        create_graphics_pipeline_native_vertex_bindings_v2;
    /* Optional V3 tail: distinct D32 and S8 framebuffer attachments. */
    RinGLRinGpuBeginRenderPassDepthStencilFn begin_render_pass_depth_stencil;
} RinGLRinGpuOpsV1;

typedef struct RinGLRinGpuBindingV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* session;
    const RinGLRinGpuOpsV1* ops;
    uint64_t graphics_queue;
    uint32_t queue_capabilities;
    /* RINGL_RIN_GPU_VERTEX_INPUT_* capabilities implemented by this binding. */
    uint32_t vertex_input_capabilities;
    uint32_t reserved0;
} RinGLRinGpuBindingV1;

typedef struct RinGLContextDescV1 {
    uint32_t struct_size;
    uint32_t api_version;
    const RinGLRinGpuBindingV1* ringpu;
    uint32_t flags;
    uint32_t reserved0;
} RinGLContextDescV1;

typedef struct RinGLVertexAttribInfoV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t enabled;
    uint32_t size;
    uint32_t type;
    uint32_t normalized;
    uint32_t stride;
    uint32_t buffer;
    uint64_t offset;
} RinGLVertexAttribInfoV1;

typedef struct RinGLProgramInfoV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t link_status;
    uint32_t validate_status;
    uint32_t attached_shader_count;
    uint32_t active_attribute_count;
    uint32_t active_uniform_count;
    uint32_t reserved0;
} RinGLProgramInfoV1;

/* A caller-owned active attribute/uniform record. The name is always
 * NUL-terminated after its exact byte length. */
typedef struct RinGLActiveInfoV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t type;
    uint32_t size;
    uint32_t name_length;
    char name[RINGL_ACTIVE_INFO_NAME_MAX];
    uint32_t reserved0;
} RinGLActiveInfoV1;

typedef struct RinGLRenderbufferInfoV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t width;
    uint32_t height;
    uint32_t internal_format;
    uint32_t red_size;
    uint32_t green_size;
    uint32_t blue_size;
    uint32_t alpha_size;
    uint32_t depth_size;
    uint32_t stencil_size;
    uint32_t samples;
} RinGLRenderbufferInfoV1;

typedef struct RinGLDefaultFramebufferV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint64_t color_target;
    uint32_t color_format;
    uint32_t width;
    uint32_t height;
    uint32_t display_id;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t depth_target;
    uint32_t depth_format;
    uint32_t reserved1;
} RinGLDefaultFramebufferV1;

/* Snapshot of the mutable clear values for an embedding that must issue an
 * internal clear without changing the WebGL-visible clear state. */
typedef struct RinGLClearValuesV1 {
    uint32_t struct_size;
    uint32_t api_version;
    float red;
    float green;
    float blue;
    float alpha;
    float depth;
    int32_t stencil;
    uint32_t reserved0;
} RinGLClearValuesV1;

/* Snapshot of the blend constant that RinGL supplies to the RinGPU pipeline.
 * This is a separate ABI from clear values because blendColor must remain
 * observable even when a caller changes its clear state. */
typedef struct RinGLBlendColorV1 {
    uint32_t struct_size;
    uint32_t api_version;
    float red;
    float green;
    float blue;
    float alpha;
    uint32_t reserved0;
} RinGLBlendColorV1;

/* Snapshot of the clamped WebGL depth-range endpoints consumed by the
 * RinGPU raster state. */
typedef struct RinGLDepthRangeV1 {
    uint32_t struct_size;
    uint32_t api_version;
    float z_near;
    float z_far;
    uint32_t reserved0;
} RinGLDepthRangeV1;

/* Snapshot of the current line width and the bounded aliased implementation
 * range exposed to a browser embedding. */
typedef struct RinGLLineWidthV1 {
    uint32_t struct_size;
    uint32_t api_version;
    float width;
    float minimum;
    float maximum;
    uint32_t reserved0;
} RinGLLineWidthV1;

/* Snapshot of WebGL sample-coverage state. With the current one-sample
 * target, a covered sample is either retained or suppressed; this remains an
 * executable raster state rather than an embedding-only cache. */
typedef struct RinGLSampleCoverageV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t enabled;
    float value;
    uint32_t invert;
    uint32_t reserved0;
} RinGLSampleCoverageV1;

/* This is a read-only description of one attachment of RinGL's currently
 * bound custom framebuffer. It is intentionally separate from GLES query
 * entry points so an embedding can inspect the bounded object-model slice
 * without claiming framebuffer-completeness or rendering support. */
typedef struct RinGLFramebufferAttachmentInfoV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t kind;
    uint32_t object;
    int32_t level;
    uint32_t reserved0;
} RinGLFramebufferAttachmentInfoV1;

int ringl_context_create(const RinGLContextDescV1* desc,
                         RinGLContext** context_out);
void ringl_context_destroy(RinGLContext* context);
int ringl_make_current(RinGLContext* context);
RinGLContext* ringl_get_current_context(void);
uint32_t ringl_get_error(void);
uint32_t ringl_context_is_lost(const RinGLContext* context);
uint32_t ringl_context_dirty_bits(const RinGLContext* context);
/* Writes the complete integer result only when `value_count` is large enough.
 * It returns zero on success and -1 on an invalid query, missing context, or
 * short/null output. Failures leave the caller's output unchanged. */
int ringl_get_integerv_bounded(uint32_t pname, int32_t* values,
                               size_t value_count);
/* Legacy unbounded form. New embedding code must use
 * ringl_get_integerv_bounded() so vector queries cannot overrun its output. */
void ringl_get_integerv(uint32_t pname, int32_t* values);
void ringl_enable(uint32_t capability);
void ringl_disable(uint32_t capability);
int ringl_is_enabled(uint32_t capability);
void ringl_viewport(int32_t x, int32_t y, int32_t width, int32_t height);
void ringl_scissor(int32_t x, int32_t y, int32_t width, int32_t height);
void ringl_cull_face(uint32_t mode);
void ringl_front_face(uint32_t mode);
void ringl_depth_func(uint32_t func);
void ringl_depth_mask(uint32_t enabled);
/* Finite endpoints are independently clamped to [0, 1]; reversed ranges are
 * valid and are forwarded to RinGPU without reordering. */
void ringl_depth_range(float z_near, float z_far);
/* RinGL implements aliased line widths in the finite inclusive [1, 64] range.
 * Out-of-range values report INVALID_VALUE and leave state unchanged. */
void ringl_line_width(float width);
/* Returns current width and the inclusive supported range only for a complete
 * v1 output header. */
int ringl_get_line_width(RinGLLineWidthV1* width);
/* `value` is finite-clamped to [0, 1]. `invert` must be RINGL_FALSE/TRUE;
 * invalid inputs preserve the prior state. */
void ringl_sample_coverage(float value, uint32_t invert);
/* Returns the exact enabled/value/invert state only for a complete v1 output
 * header; invalid output leaves caller storage unchanged. */
int ringl_get_sample_coverage(RinGLSampleCoverageV1* coverage);
/* The bounded renderer currently has no generated mip chain, so the only
 * accepted GLES hint is GENERATE_MIPMAP_HINT and its valid modes are advisory
 * no-ops. Unsupported hint targets are never reported as implemented. */
void ringl_hint(uint32_t target, uint32_t mode);
/* Applies GLES polygon offset to filled primitives. Both finite values are
 * carried in the dynamic RinGPU raster state; points and lines are unchanged. */
void ringl_polygon_offset(float factor, float units);
/* Returns the tracked depth range only for a complete v1 output header. */
int ringl_get_depth_range(RinGLDepthRangeV1* range);
void ringl_stencil_func(uint32_t func, int32_t reference, uint32_t mask);
void ringl_stencil_func_separate(uint32_t face, uint32_t func,
                                 int32_t reference, uint32_t mask);
void ringl_stencil_mask(uint32_t mask);
void ringl_stencil_mask_separate(uint32_t face, uint32_t mask);
void ringl_stencil_op(uint32_t stencil_fail, uint32_t depth_fail,
                      uint32_t depth_pass);
void ringl_stencil_op_separate(uint32_t face, uint32_t stencil_fail,
                               uint32_t depth_fail, uint32_t depth_pass);
void ringl_blend_func(uint32_t source_factor, uint32_t destination_factor);
void ringl_blend_func_separate(uint32_t source_rgb, uint32_t destination_rgb,
                               uint32_t source_alpha,
                               uint32_t destination_alpha);
/* Returns the tracked blend constant. The output must describe the complete
 * v1 structure; failures leave it unchanged. */
int ringl_get_blend_color(RinGLBlendColorV1* color);
void ringl_blend_color(float red, float green, float blue, float alpha);
void ringl_blend_equation(uint32_t mode);
void ringl_blend_equation_separate(uint32_t mode_rgb, uint32_t mode_alpha);
void ringl_color_mask(uint32_t red, uint32_t green, uint32_t blue,
                      uint32_t alpha);
/* Supports WebGL 1 UNPACK_ALIGNMENT values 1, 2, 4, and 8. */
void ringl_pixel_storei(uint32_t pname, int32_t param);

int ringl_set_default_framebuffer(const RinGLDefaultFramebufferV1* framebuffer);
int ringl_set_default_framebuffer_state(uint32_t state);
int ringl_set_default_depth_framebuffer_state(uint32_t state);
int ringl_get_default_framebuffer(RinGLDefaultFramebufferV1* framebuffer);
/* Returns the tracked RinGPU state of the default color target without
 * exposing RinGL internals to the embedding. Returns 1 when no default
 * framebuffer is configured and leaves state unchanged on failure. */
int ringl_get_default_framebuffer_state(uint32_t* state);
int ringl_get_default_depth_framebuffer_state(uint32_t* state);
/* Returns the current context's clear state. The output must describe the
 * complete v1 structure; failures leave it unchanged. */
int ringl_get_clear_values(RinGLClearValuesV1* values);
void ringl_clear_color(float red, float green, float blue, float alpha);
void ringl_clear_depth(float depth);
void ringl_clear_stencil(int32_t stencil);
void ringl_clear(uint32_t mask);
/* Clears the default drawing buffer to WebGL's post-presentation values:
 * transparent black, depth one, and stencil zero. This trusted embedding
 * helper always targets the default framebuffer and ignores application
 * scissor/write-mask state. It preserves all author-visible GL state,
 * including a pending error returned by ringl_get_error(). Returns zero on
 * success; otherwise returns -1 and writes the attempted operation's GL
 * error to error_out. error_out is required. */
int ringl_clear_default_framebuffer_for_embedding(uint32_t* error_out);
void ringl_draw_arrays(uint32_t mode, int32_t first, int32_t count);
void ringl_draw_elements(uint32_t mode, int32_t count, uint32_t type,
                         uint64_t offset);
int ringl_present(void);

void ringl_gen_buffers(int32_t count, uint32_t* buffers);
void ringl_delete_buffers(int32_t count, const uint32_t* buffers);
void ringl_bind_buffer(uint32_t target, uint32_t buffer);
int ringl_is_buffer(uint32_t buffer);
uint32_t ringl_get_bound_buffer(uint32_t target);
void ringl_buffer_data(uint32_t target,
                       int64_t size_bytes,
                       const void* data,
                       uint32_t usage);
/* Replaces a range of an initialized buffer. The update is staged in a
 * replacement RinGPU buffer, so an allocation/upload failure preserves the
 * previous logical buffer contents and binding-visible storage. */
void ringl_buffer_sub_data(uint32_t target,
                           int64_t offset_bytes,
                           int64_t size_bytes,
                           const void* data);
uint64_t ringl_get_buffer_size(uint32_t target);
uint32_t ringl_get_buffer_usage(uint32_t target);

void ringl_gen_textures(int32_t count, uint32_t* textures);
void ringl_delete_textures(int32_t count, const uint32_t* textures);
void ringl_bind_texture(uint32_t target, uint32_t texture);
int ringl_is_texture(uint32_t texture);
void ringl_active_texture(uint32_t texture_unit);
uint32_t ringl_get_active_texture(void);
uint32_t ringl_get_bound_texture(uint32_t target);
void ringl_tex_parameteri(uint32_t target, uint32_t pname, int32_t param);
int32_t ringl_get_tex_parameteri(uint32_t target, uint32_t pname);
void ringl_tex_image_2d(uint32_t target, int32_t level,
                        uint32_t internal_format, int32_t width, int32_t height,
                        int32_t border, uint32_t format, uint32_t type,
                        const void* pixels);
/* Bounded texture-import paths for callers that receive untrusted bytes.
 * When pixels is non-NULL it must cover every source row, including
 * RINGL_UNPACK_ALIGNMENT padding between rows. A short source records
 * RINGL_INVALID_VALUE before changing the texture. A NULL texImage source
 * defines zero-initialized storage; non-empty texSubImage sources must not be
 * NULL. The raw-pointer entry points above remain for trusted native callers
 * that can independently guarantee the source extent. */
void ringl_tex_image_2d_from_bytes(uint32_t target, int32_t level,
                                   uint32_t internal_format, int32_t width,
                                   int32_t height, int32_t border,
                                   uint32_t format, uint32_t type,
                                   const void* pixels, uint64_t pixels_size);
void ringl_tex_sub_image_2d(uint32_t target, int32_t level,
                            int32_t xoffset, int32_t yoffset,
                            int32_t width, int32_t height,
                            uint32_t format, uint32_t type,
                            const void* pixels);
void ringl_tex_sub_image_2d_from_bytes(uint32_t target, int32_t level,
                                       int32_t xoffset, int32_t yoffset,
                                       int32_t width, int32_t height,
                                       uint32_t format, uint32_t type,
                                       const void* pixels,
                                       uint64_t pixels_size);
/* Bounded copy path: the current complete RGBA color target to a level-zero
 * RGBA texture. Non-RGBA destinations remain rejected. */
void ringl_copy_tex_sub_image_2d(uint32_t target, int32_t level,
                                 int32_t xoffset, int32_t yoffset,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height);
/* Bounded definition path: snapshot the current complete RGBA color target
 * into a new level-zero RGBA texture definition. */
void ringl_copy_tex_image_2d(uint32_t target, int32_t level,
                             uint32_t internal_format, int32_t x, int32_t y,
                             int32_t width, int32_t height, int32_t border);

void ringl_gen_framebuffers(int32_t count, uint32_t* framebuffers);
void ringl_delete_framebuffers(int32_t count, const uint32_t* framebuffers);
void ringl_bind_framebuffer(uint32_t target, uint32_t framebuffer);
int ringl_is_framebuffer(uint32_t framebuffer);
uint32_t ringl_get_bound_framebuffer(uint32_t target);
void ringl_framebuffer_texture_2d(uint32_t target, uint32_t attachment,
                                  uint32_t textarget, uint32_t texture,
                                  int32_t level);
/* Describes COLOR_ATTACHMENT0, DEPTH_ATTACHMENT, STENCIL_ATTACHMENT, or
 * DEPTH_STENCIL_ATTACHMENT on the currently bound custom framebuffer. The
 * caller supplies a complete v1 header; failures leave its output fields
 * untouched. DEPTH_STENCIL_ATTACHMENT reports an object only when one image
 * is attached to both logical aspects. */
int ringl_get_framebuffer_attachment(
    uint32_t attachment, RinGLFramebufferAttachmentInfoV1* info);
/* Compatibility shorthand for COLOR_ATTACHMENT0. New embedding code should
 * use ringl_get_framebuffer_attachment() so it does not silently omit depth
 * and stencil state. */
int ringl_get_framebuffer_color_attachment(
    RinGLFramebufferAttachmentInfoV1* attachment);
uint32_t ringl_check_framebuffer_status(uint32_t target);

void ringl_gen_renderbuffers(int32_t count, uint32_t* renderbuffers);
void ringl_delete_renderbuffers(int32_t count,
                                const uint32_t* renderbuffers);
void ringl_bind_renderbuffer(uint32_t target, uint32_t renderbuffer);
int ringl_is_renderbuffer(uint32_t renderbuffer);
uint32_t ringl_get_bound_renderbuffer(uint32_t target);
int ringl_get_renderbuffer_info(uint32_t target, RinGLRenderbufferInfoV1* info);
void ringl_renderbuffer_storage(uint32_t target, uint32_t internal_format,
                                int32_t width, int32_t height);
void ringl_framebuffer_renderbuffer(uint32_t target, uint32_t attachment,
                                    uint32_t renderbuffer_target,
                                    uint32_t renderbuffer);

void ringl_enable_vertex_attrib_array(uint32_t index);
void ringl_disable_vertex_attrib_array(uint32_t index);
void ringl_vertex_attrib_pointer(uint32_t index,
                                 int32_t size,
                                 uint32_t type,
                                 uint32_t normalized,
                                 int32_t stride,
                                 uint64_t offset);
/* Set the current generic attribute value used when the corresponding array
 * is disabled. Like WebGL/OpenGL ES, 1f/2f/3f fill omitted components with
 * 0, 0, and 1 respectively. */
void ringl_vertex_attrib1f(uint32_t index, float x);
void ringl_vertex_attrib2f(uint32_t index, float x, float y);
void ringl_vertex_attrib3f(uint32_t index, float x, float y, float z);
void ringl_vertex_attrib4f(uint32_t index, float x, float y, float z,
                           float w);
int ringl_get_vertex_attrib(uint32_t index, RinGLVertexAttribInfoV1* info);
/* Copies the current generic attribute value into exactly four floats. The
 * caller owns the fixed-size output; invalid indices leave it unchanged. */
int ringl_get_vertex_attrib_current(uint32_t index, float values[4]);

uint32_t ringl_create_shader(uint32_t shader_type);
void ringl_delete_shader(uint32_t shader);
int ringl_is_shader(uint32_t shader);
void ringl_shader_source(uint32_t shader, const char* source, int64_t length);
void ringl_compile_shader(uint32_t shader);
uint32_t ringl_get_shader_compile_status(uint32_t shader);
uint32_t ringl_get_shader_type(uint32_t shader);
uint64_t ringl_get_shader_source_length(uint32_t shader);
/* Copies a NUL-terminated prefix of the current shader source when capacity
 * permits. Returns the complete source length (without the NUL terminator).
 * Invalid shader handles leave caller storage untouched and record GL error. */
uint64_t ringl_copy_shader_source(uint32_t shader, char* buffer,
                                  uint64_t buffer_size);
uint64_t ringl_get_shader_info_log(uint32_t shader,
                                   char* buffer,
                                   uint64_t buffer_size);
int ringl_lower_shader_rsh1(uint32_t shader);
uint32_t ringl_get_shader_rsh1_size(uint32_t shader);
uint32_t ringl_copy_shader_rsh1(uint32_t shader,
                                void* output,
                                uint32_t capacity);
int ringl_realize_shader_module(uint32_t shader);
uint64_t ringl_get_shader_module(uint32_t shader);

uint32_t ringl_create_program(void);
void ringl_delete_program(uint32_t program);
int ringl_is_program(uint32_t program);
void ringl_attach_shader(uint32_t program, uint32_t shader);
/* Marks a shader object for deletion. Attached shaders stay available to the
 * program until detached or the program itself is deleted. */
void ringl_detach_shader(uint32_t program, uint32_t shader);
void ringl_link_program(uint32_t program);
uint32_t ringl_get_program_link_status(uint32_t program);
void ringl_validate_program(uint32_t program);
int ringl_get_program_info(uint32_t program, RinGLProgramInfoV1* info);
/* Return one active linked attribute or sampler uniform into a versioned,
 * caller-owned record. Failures leave the record unchanged. */
int ringl_get_active_attrib(uint32_t program, uint32_t index,
                            RinGLActiveInfoV1* info);
int ringl_get_active_uniform(uint32_t program, uint32_t index,
                             RinGLActiveInfoV1* info);
uint64_t ringl_get_program_info_log(uint32_t program,
                                    char* buffer,
                                    uint64_t buffer_size);
/* Requests the generic vertex-array index for an active vertex attribute on
 * the program's next successful link. Existing linked executables retain
 * their locations until that relink succeeds. */
void ringl_bind_attrib_location(uint32_t program, uint32_t index,
                                const char* name);
void ringl_use_program(uint32_t program);
uint32_t ringl_get_current_program(void);
/* Returns the linked generic vertex-array index for an active program
 * attribute, or -1 when the name is not active. */
int32_t ringl_get_attrib_location(uint32_t program, const char* name);
int32_t ringl_get_uniform_location(uint32_t program, const char* name);
void ringl_uniform_1i(int32_t location, int32_t value);
/* Reads a linked sampler uniform without borrowing program storage. Invalid
 * program/location inputs leave value_out unchanged and record an error. */
int ringl_get_uniform_1i(uint32_t program, int32_t location,
                          int32_t* value_out);

#ifdef __cplusplus
}
#endif

#endif /* RINGL_RINGL_H */
