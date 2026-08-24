/* SPDX-License-Identifier: MIT */
#include <ringl/ringl_aquamarine_surface.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aq_surface.h"
#include <ringpu/core.h>
#include "rin_webgl_software.h"

#define RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES \
    (RIN_SHADER_MAX_RESOURCES / 2u)
#define RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS 13u

static int backend_primitive_topology_valid(uint32_t topology)
{
    return topology == RIN_GPU_PRIMITIVE_TRIANGLE_LIST ||
           topology == RIN_GPU_PRIMITIVE_POINT_LIST ||
           topology == RIN_GPU_PRIMITIVE_LINE_LIST ||
           topology == RIN_GPU_PRIMITIVE_LINE_STRIP ||
           topology == RIN_GPU_PRIMITIVE_LINE_LOOP ||
           topology == RIN_GPU_PRIMITIVE_TRIANGLE_STRIP ||
           topology == RIN_GPU_PRIMITIVE_TRIANGLE_FAN;
}

static uint32_t backend_software_primitive(uint32_t topology)
{
    if (topology == RIN_GPU_PRIMITIVE_TRIANGLE_LIST)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_TRIANGLES;
    if (topology == RIN_GPU_PRIMITIVE_POINT_LIST)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_POINTS;
    if (topology == RIN_GPU_PRIMITIVE_LINE_LIST)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_LINES;
    if (topology == RIN_GPU_PRIMITIVE_LINE_STRIP)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_LINE_STRIP;
    if (topology == RIN_GPU_PRIMITIVE_LINE_LOOP)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_LINE_LOOP;
    if (topology == RIN_GPU_PRIMITIVE_TRIANGLE_STRIP)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_TRIANGLE_STRIP;
    if (topology == RIN_GPU_PRIMITIVE_TRIANGLE_FAN)
        return RIN_WEBGL_SOFTWARE_PRIMITIVE_TRIANGLE_FAN;
    return UINT32_MAX;
}

static uint32_t backend_image_texel_bytes(uint32_t format)
{
    if (format == RIN_GPU_FORMAT_S8_UINT)
        return (uint32_t)sizeof(uint8_t);
    if (format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT)
        return (uint32_t)sizeof(uint64_t);
    if (format == RIN_GPU_FORMAT_RGB565_UNORM ||
        format == RIN_GPU_FORMAT_RGBA4_UNORM ||
        format == RIN_GPU_FORMAT_RGB5_A1_UNORM)
        return (uint32_t)sizeof(uint16_t);
    if (format == RIN_GPU_FORMAT_RGBA8_UNORM ||
        format == RIN_GPU_FORMAT_BGRA8_UNORM ||
        format == RIN_GPU_FORMAT_D32_FLOAT) {
        return (uint32_t)sizeof(uint32_t);
    }
    return 0u;
}

static uint16_t backend_pack_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)((((uint16_t)red * 31u + 127u) / 255u) << 11u |
                      (((uint16_t)green * 63u + 127u) / 255u) << 5u |
                      ((uint16_t)blue * 31u + 127u) / 255u);
}

static void backend_unpack_rgb565(const uint8_t* source, AqColor* color)
{
    uint16_t packed;

    memcpy(&packed, source, sizeof(packed));
    color->r = (uint8_t)((((packed >> 11u) & 0x1fu) * 255u + 15u) / 31u);
    color->g = (uint8_t)((((packed >> 5u) & 0x3fu) * 255u + 31u) / 63u);
    color->b = (uint8_t)(((packed & 0x1fu) * 255u + 15u) / 31u);
    color->a = UINT8_MAX;
}

static int backend_packed_color_format(uint32_t format)
{
    return format == RIN_GPU_FORMAT_RGB565_UNORM ||
           format == RIN_GPU_FORMAT_RGBA4_UNORM ||
           format == RIN_GPU_FORMAT_RGB5_A1_UNORM;
}

static uint16_t backend_pack_packed_color(uint32_t format, uint8_t red,
                                          uint8_t green, uint8_t blue,
                                          uint8_t alpha)
{
    if (format == RIN_GPU_FORMAT_RGB565_UNORM)
        return backend_pack_rgb565(red, green, blue);
    if (format == RIN_GPU_FORMAT_RGBA4_UNORM) {
        return (uint16_t)(
            (((uint16_t)red * 15u + 127u) / 255u) << 12u |
            (((uint16_t)green * 15u + 127u) / 255u) << 8u |
            (((uint16_t)blue * 15u + 127u) / 255u) << 4u |
            ((uint16_t)alpha * 15u + 127u) / 255u);
    }
    return (uint16_t)(
        (((uint16_t)red * 31u + 127u) / 255u) << 11u |
        (((uint16_t)green * 31u + 127u) / 255u) << 6u |
        (((uint16_t)blue * 31u + 127u) / 255u) << 1u |
        ((uint16_t)alpha + 127u) / 255u);
}

static void backend_unpack_packed_color(uint32_t format, const uint8_t* source,
                                        AqColor* color)
{
    uint16_t packed;

    if (format == RIN_GPU_FORMAT_RGB565_UNORM) {
        backend_unpack_rgb565(source, color);
        return;
    }
    memcpy(&packed, source, sizeof(packed));
    if (format == RIN_GPU_FORMAT_RGBA4_UNORM) {
        color->r = (uint8_t)((((packed >> 12u) & 0xfu) * 255u + 7u) / 15u);
        color->g = (uint8_t)((((packed >> 8u) & 0xfu) * 255u + 7u) / 15u);
        color->b = (uint8_t)((((packed >> 4u) & 0xfu) * 255u + 7u) / 15u);
        color->a = (uint8_t)(((packed & 0xfu) * 255u + 7u) / 15u);
        return;
    }
    color->r = (uint8_t)((((packed >> 11u) & 0x1fu) * 255u + 15u) / 31u);
    color->g = (uint8_t)((((packed >> 6u) & 0x1fu) * 255u + 15u) / 31u);
    color->b = (uint8_t)((((packed >> 1u) & 0x1fu) * 255u + 15u) / 31u);
    color->a = (packed & 1u) != 0u ? UINT8_MAX : 0u;
}

typedef struct RinGLAquamarineSurfaceBuffer {
    struct RinGLAquamarineSurfaceContext* owner;
    uint8_t* bytes;
    uint64_t size_bytes;
} RinGLAquamarineSurfaceBuffer;

typedef struct RinGLAquamarineSurfaceShader {
    struct RinGLAquamarineSurfaceContext* owner;
    uint8_t* bytes;
    uint32_t size_bytes;
    RinShaderInfoV1 info;
} RinGLAquamarineSurfaceShader;

typedef struct RinGLAquamarineSurfacePipeline {
    struct RinGLAquamarineSurfaceContext* owner;
    const RinGLAquamarineSurfaceShader* vertex_shader;
    const RinGLAquamarineSurfaceShader* fragment_shader;
    uint32_t primitive_topology;
    uint32_t input_count;
    uint32_t vertex_stride;
    uint32_t vertex_binding_count;
    RinGpuBackendVertexAttributeV1
        vertex_attributes[RIN_GPU_MAX_VERTEX_ATTRIBUTES];
    RinGpuVertexBufferLayoutV1
        vertex_bindings[RIN_GPU_MAX_VERTEX_BUFFER_BINDINGS];
    uint32_t resource_count;
    uint32_t depth_enabled;
    uint32_t depth_compare;
    uint32_t depth_write_enabled;
    uint32_t blend_enabled;
    uint32_t source_color_factor;
    uint32_t destination_color_factor;
    uint32_t color_operation;
    uint32_t source_alpha_factor;
    uint32_t destination_alpha_factor;
    uint32_t alpha_operation;
    float blend_constant_red;
    float blend_constant_green;
    float blend_constant_blue;
    float blend_constant_alpha;
    uint32_t color_write_mask;
    uint32_t stencil_enabled;
    uint32_t stencil_compare;
    uint32_t stencil_reference;
    uint32_t stencil_read_mask;
    uint32_t stencil_write_mask;
    uint32_t stencil_fail_operation;
    uint32_t stencil_depth_fail_operation;
    uint32_t stencil_pass_operation;
    uint32_t separate_stencil_enabled;
    uint32_t back_stencil_compare;
    uint32_t back_stencil_reference;
    uint32_t back_stencil_read_mask;
    uint32_t back_stencil_write_mask;
    uint32_t back_stencil_fail_operation;
    uint32_t back_stencil_depth_fail_operation;
    uint32_t back_stencil_pass_operation;
    uint32_t cull_mode;
    uint32_t front_face;
} RinGLAquamarineSurfacePipeline;

typedef struct RinGLAquamarineSurfaceSampler {
    struct RinGLAquamarineSurfaceContext* owner;
    RinGpuSamplerDescV1 descriptor;
} RinGLAquamarineSurfaceSampler;

typedef struct RinGLAquamarineSurfaceImage {
    struct RinGLAquamarineSurfaceContext* owner;
    RinGpuImageDescV1 descriptor;
    uint8_t* bytes;
    uint64_t allocation_bytes;
    uint64_t pitch_bytes;
    uint32_t target_kind;
} RinGLAquamarineSurfaceImage;

typedef struct RinGLAquamarineSurfaceBindGroup {
    struct RinGLAquamarineSurfaceContext* owner;
    const RinGLAquamarineSurfacePipeline* pipeline;
    const RinGLAquamarineSurfaceImage*
        sampled_images[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES];
    RinWebGLSoftwareSampler2DV2
        samplers[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES];
    float* texels[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES]
                 [RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS];
    uint32_t texel_counts[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES]
                         [RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS];
    uint32_t widths[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES]
                   [RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS];
    uint32_t heights[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES]
                    [RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS];
    uint32_t mip_counts[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES];
    uint32_t sampled_image_count;
} RinGLAquamarineSurfaceBindGroup;

typedef struct RinGLAquamarineSurfaceRasterState {
    RinWebGLSoftwareViewportV2 viewport;
    RinWebGLSoftwareScissorV1 scissor;
    uint32_t scissor_enabled;
    uint32_t polygon_offset_fill_enabled;
    float polygon_offset_factor;
    float polygon_offset_units;
    float line_width;
    uint32_t sample_coverage_enabled;
    float sample_coverage_value;
    uint32_t sample_coverage_invert;
} RinGLAquamarineSurfaceRasterState;

typedef struct RinGLAquamarineSurfaceActiveRenderPass {
    uint64_t color_target_cookie;
    uint64_t depth_target_cookie;
    uint64_t stencil_target_cookie;
    uint32_t color_mip_level;
    uint32_t depth_mip_level;
    uint32_t stencil_mip_level;
} RinGLAquamarineSurfaceActiveRenderPass;

#define RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR 1u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH 2u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR 3u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH 4u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL 5u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL 6u
#define RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_STENCIL 7u
#define RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES \
    ((uint64_t)RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION * \
     RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION * sizeof(uint64_t))
#define RINGL_AQUAMARINE_SURFACE_MAX_TOTAL_ALLOCATION_BYTES \
    (RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES * UINT64_C(4))

static int backend_multiply_u64(uint64_t left, uint64_t right,
                                uint64_t* value)
{
    if (!value || (right != 0u && left > UINT64_MAX / right))
        return 0;
    *value = left * right;
    return 1;
}

static int backend_add_u64(uint64_t left, uint64_t right, uint64_t* value)
{
    if (!value || left > UINT64_MAX - right)
        return 0;
    *value = left + right;
    return 1;
}

static int backend_align_u64(uint64_t value, uint64_t alignment,
                             uint64_t* aligned_out)
{
    uint64_t remainder;

    if (!aligned_out || alignment == 0u)
        return 0;
    remainder = value % alignment;
    if (remainder == 0u) {
        *aligned_out = value;
        return 1;
    }
    return backend_add_u64(value, alignment - remainder, aligned_out);
}

static int backend_image_mip_dimensions(const RinGpuImageDescV1* descriptor,
                                        uint32_t mip_level,
                                        uint32_t* width_out,
                                        uint32_t* height_out)
{
    uint32_t width;
    uint32_t height;

    if (!descriptor || !width_out || !height_out ||
        descriptor->dimension != RIN_GPU_IMAGE_DIMENSION_2D ||
        descriptor->width == 0u || descriptor->height == 0u ||
        mip_level >= descriptor->mip_levels) {
        return 0;
    }
    width = descriptor->width;
    height = descriptor->height;
    while (mip_level != 0u) {
        if (width > 1u) width >>= 1u;
        if (height > 1u) height >>= 1u;
        mip_level--;
    }
    *width_out = width;
    *height_out = height;
    return 1;
}

/* This only serves offscreen CPU storage. Render and sample commands retain
 * their level-zero restriction until their per-mip target/view handling is
 * implemented. The layout matches ringpu_image_allocation_size(): every
 * layer of one mip precedes the next mip. */
static int offscreen_image_storage(const RinGLAquamarineSurfaceImage* image,
                                   uint32_t mip_level,
                                   uint8_t** bytes_out,
                                   uint64_t* pitch_out,
                                   uint32_t* width_out,
                                   uint32_t* height_out)
{
    uint64_t offset = 0u;
    uint64_t level_bytes;
    uint64_t row_pitch;
    uint32_t texel_bytes;
    uint32_t width;
    uint32_t height;

    if (!image || !bytes_out || !pitch_out || !width_out || !height_out ||
        !image->bytes || image->descriptor.dimension !=
                            RIN_GPU_IMAGE_DIMENSION_2D ||
        image->descriptor.array_layers != 1u ||
        mip_level >= image->descriptor.mip_levels ||
        (image->target_kind != RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR &&
         image->target_kind != RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH &&
         image->target_kind != RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_STENCIL)) {
        return RIN_GPU_ERROR_BACKEND;
    }
    width = image->descriptor.width;
    height = image->descriptor.height;
    texel_bytes = backend_image_texel_bytes(image->descriptor.format);
    if (texel_bytes == 0u) return RIN_GPU_ERROR_BACKEND;
    for (uint32_t level = 0u; level < mip_level; ++level) {
        if (!backend_multiply_u64(width, texel_bytes, &row_pitch) ||
            !backend_multiply_u64(row_pitch, height, &level_bytes) ||
            !backend_add_u64(offset, level_bytes, &offset)) {
            return RIN_GPU_ERROR_BACKEND;
        }
        if (width > 1u) width >>= 1u;
        if (height > 1u) height >>= 1u;
    }
    if (!backend_multiply_u64(width, texel_bytes, &row_pitch) ||
        !backend_multiply_u64(row_pitch, height, &level_bytes) ||
        offset > image->allocation_bytes ||
        level_bytes > image->allocation_bytes - offset) {
        return RIN_GPU_ERROR_BACKEND;
    }
    *bytes_out = image->bytes + offset;
    *pitch_out = row_pitch;
    *width_out = width;
    *height_out = height;
    return RIN_GPU_OK;
}

/* D32S8 has an eight-byte CPU transfer texel, but this software backend keeps
 * depth and stencil as independent planes. Each mip gets its own compact
 * F32/S8 pair so a level's depth write cannot enter another level's stencil
 * storage. */
static int offscreen_depth_stencil_storage_at_mip(
    const RinGLAquamarineSurfaceImage* image, uint32_t mip_level,
    float** depth_out, uint8_t** stencil_out, uint32_t* pitch_out)
{
    uint64_t offset = 0u;
    uint64_t depth_bytes;
    uint64_t stencil_bytes;
    uint64_t level_bytes;
    uint32_t width;
    uint32_t height;

    if (!image || !depth_out || !stencil_out || !pitch_out || !image->bytes ||
        image->target_kind !=
            RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL ||
        image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT ||
        image->descriptor.dimension != RIN_GPU_IMAGE_DIMENSION_2D ||
        image->descriptor.array_layers != 1u ||
        mip_level >= image->descriptor.mip_levels) {
        return RIN_GPU_ERROR_BACKEND;
    }
    width = image->descriptor.width;
    height = image->descriptor.height;
    for (uint32_t level = 0u; level < mip_level; ++level) {
        if (!backend_multiply_u64(width, height, &stencil_bytes) ||
            !backend_multiply_u64(stencil_bytes, sizeof(float),
                                  &depth_bytes) ||
            !backend_add_u64(depth_bytes, stencil_bytes, &level_bytes) ||
            !backend_align_u64(level_bytes, _Alignof(float), &level_bytes) ||
            !backend_add_u64(offset, level_bytes, &offset)) {
            return RIN_GPU_ERROR_BACKEND;
        }
        if (width > 1u) width >>= 1u;
        if (height > 1u) height >>= 1u;
    }
    if (!backend_multiply_u64(width, height, &stencil_bytes) ||
        !backend_multiply_u64(stencil_bytes, sizeof(float), &depth_bytes) ||
        !backend_add_u64(depth_bytes, stencil_bytes, &level_bytes) ||
        !backend_align_u64(level_bytes, _Alignof(float), &level_bytes) ||
        offset > image->allocation_bytes ||
        level_bytes > image->allocation_bytes - offset) {
        return RIN_GPU_ERROR_BACKEND;
    }
    *depth_out = (float*)(void*)(image->bytes + offset);
    *stencil_out = image->bytes + offset + depth_bytes;
    *pitch_out = width;
    return RIN_GPU_OK;
}

static uint32_t backend_vertex_format_bytes(uint32_t format)
{
    switch (format) {
    case RIN_GPU_VERTEX_UINT8:
    case RIN_GPU_VERTEX_SINT8:
    case RIN_GPU_VERTEX_UNORM8:
    case RIN_GPU_VERTEX_SNORM8:
        return 1u;
    case RIN_GPU_VERTEX_UINT16:
    case RIN_GPU_VERTEX_SINT16:
    case RIN_GPU_VERTEX_UNORM16:
    case RIN_GPU_VERTEX_SNORM16:
        return 2u;
    case RIN_GPU_VERTEX_UINT32:
    case RIN_GPU_VERTEX_SINT32:
    case RIN_GPU_VERTEX_FLOAT32:
        return 4u;
    default:
        return 0u;
    }
}

static int backend_decode_vertex_component(uint32_t format,
                                           const uint8_t* source,
                                           float* value)
{
    uint16_t unsigned16;
    int16_t signed16;
    uint32_t unsigned32;
    int32_t signed32;

    if (!source || !value)
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    switch (format) {
    case RIN_GPU_VERTEX_UINT8:
        *value = (float)source[0];
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_SINT8:
        *value = (float)(int8_t)source[0];
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_UNORM8:
        *value = (float)source[0] / 255.0f;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_SNORM8:
        *value = source[0] == UINT8_C(0x80)
            ? -1.0f : (float)(int8_t)source[0] / 127.0f;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_UINT16:
        memcpy(&unsigned16, source, sizeof(unsigned16));
        *value = (float)unsigned16;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_SINT16:
        memcpy(&signed16, source, sizeof(signed16));
        *value = (float)signed16;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_UNORM16:
        memcpy(&unsigned16, source, sizeof(unsigned16));
        *value = (float)unsigned16 / 65535.0f;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_SNORM16:
        memcpy(&signed16, source, sizeof(signed16));
        *value = signed16 == INT16_MIN
            ? -1.0f : (float)signed16 / 32767.0f;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_UINT32:
        memcpy(&unsigned32, source, sizeof(unsigned32));
        *value = (float)unsigned32;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_SINT32:
        memcpy(&signed32, source, sizeof(signed32));
        *value = (float)signed32;
        return RIN_GPU_OK;
    case RIN_GPU_VERTEX_FLOAT32:
        memcpy(value, source, sizeof(*value));
        return RIN_GPU_OK;
    default:
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
}

static int backend_unpack_vertex_values(
    const RinGLAquamarineSurfacePipeline* pipeline, const uint8_t* source,
    uint64_t source_bytes, uint32_t vertex_count, float** values_out)
{
    uint64_t value_count;
    float* values;
    uint32_t vertex;

    if (!pipeline || !values_out || pipeline->input_count == 0u ||
        pipeline->input_count > RIN_GPU_MAX_VERTEX_ATTRIBUTES ||
        (pipeline->vertex_stride != 0u && source == NULL)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    *values_out = NULL;
    value_count = (uint64_t)vertex_count * pipeline->input_count;
    if (value_count == 0u || value_count > SIZE_MAX / sizeof(*values))
        return RIN_GPU_ERROR_LIMIT;
    values = calloc((size_t)value_count, sizeof(*values));
    if (!values)
        return RIN_GPU_ERROR_NO_MEMORY;

    for (vertex = 0u; vertex < vertex_count; ++vertex) {
        uint64_t vertex_base = (uint64_t)vertex * pipeline->vertex_stride;
        uint32_t attribute;

        if (pipeline->vertex_stride != 0u && vertex_base > source_bytes) {
            free(values);
            return RIN_GPU_ERROR_BOUNDS;
        }
        for (attribute = 0u; attribute < pipeline->input_count; ++attribute) {
            const RinGpuBackendVertexAttributeV1* layout =
                &pipeline->vertex_attributes[attribute];
            uint32_t component_bytes =
                backend_vertex_format_bytes(layout->format);
            uint64_t offset;
            int result;

            if (layout->location >= pipeline->input_count ||
                component_bytes == 0u) {
                free(values);
                return RIN_GPU_ERROR_INVALID_ARGUMENT;
            }
            if (layout->flags == RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32) {
                if (layout->format != RIN_GPU_VERTEX_FLOAT32) {
                    free(values);
                    return RIN_GPU_ERROR_INVALID_ARGUMENT;
                }
                memcpy(&values[(size_t)vertex * pipeline->input_count +
                               layout->location],
                       &layout->offset,
                       sizeof(values[(size_t)vertex * pipeline->input_count +
                                     layout->location]));
                continue;
            }
            if (layout->flags != 0u || source == NULL ||
                pipeline->vertex_stride == 0u ||
                layout->offset > pipeline->vertex_stride - component_bytes ||
                layout->offset > source_bytes - vertex_base) {
                free(values);
                return RIN_GPU_ERROR_INVALID_ARGUMENT;
            }
            offset = vertex_base + layout->offset;
            if (component_bytes > source_bytes - offset) {
                free(values);
                return RIN_GPU_ERROR_BOUNDS;
            }
            result = backend_decode_vertex_component(
                layout->format, source + (size_t)offset,
                &values[(size_t)vertex * pipeline->input_count +
                        layout->location]);
            if (result != RIN_GPU_OK) {
                free(values);
                return result;
            }
        }
    }
    *values_out = values;
    return RIN_GPU_OK;
}

static int backend_unpack_vertex_values_v2(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfacePipeline* pipeline,
    const RinGpuBackendVertexBufferBindingV1* bindings,
    uint32_t binding_count, uint32_t first_vertex, uint32_t vertex_count,
    float** values_out)
{
    uint64_t value_count;
    float* values;

    if (!context || !pipeline || !values_out || pipeline->input_count == 0u ||
        pipeline->input_count > RIN_GPU_MAX_VERTEX_ATTRIBUTES ||
        binding_count != pipeline->vertex_binding_count ||
        (binding_count != 0u && bindings == NULL)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    *values_out = NULL;
    value_count = (uint64_t)vertex_count * pipeline->input_count;
    if (value_count == 0u || value_count > SIZE_MAX / sizeof(*values))
        return RIN_GPU_ERROR_LIMIT;
    values = calloc((size_t)value_count, sizeof(*values));
    if (!values) return RIN_GPU_ERROR_NO_MEMORY;

    for (uint32_t vertex = 0u; vertex < vertex_count; ++vertex) {
        for (uint32_t attribute = 0u; attribute < pipeline->input_count;
             ++attribute) {
            const RinGpuBackendVertexAttributeV1* layout =
                &pipeline->vertex_attributes[attribute];
            uint32_t component_bytes =
                backend_vertex_format_bytes(layout->format);
            const RinGpuBackendVertexBufferBindingV1* binding;
            const RinGpuVertexBufferLayoutV1* binding_layout;
            const RinGLAquamarineSurfaceBuffer* buffer;
            uint64_t vertex_base;
            uint64_t offset;
            int result;

            if (layout->location >= pipeline->input_count ||
                component_bytes == 0u) {
                free(values);
                return RIN_GPU_ERROR_INVALID_ARGUMENT;
            }
            if (layout->flags == RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32) {
                if (layout->format != RIN_GPU_VERTEX_FLOAT32 ||
                    layout->binding != 0u) {
                    free(values);
                    return RIN_GPU_ERROR_INVALID_ARGUMENT;
                }
                memcpy(&values[(size_t)vertex * pipeline->input_count +
                               layout->location],
                       &layout->offset,
                       sizeof(values[(size_t)vertex * pipeline->input_count +
                                     layout->location]));
                continue;
            }
            if (layout->flags != 0u || layout->binding >= binding_count ||
                layout->binding >= pipeline->vertex_binding_count) {
                free(values);
                return RIN_GPU_ERROR_INVALID_ARGUMENT;
            }
            binding = &bindings[layout->binding];
            binding_layout = &pipeline->vertex_bindings[layout->binding];
            buffer = (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
                binding->buffer_cookie;
            if (binding->binding != layout->binding || !buffer ||
                buffer->owner != context || !buffer->bytes ||
                binding_layout->binding != layout->binding ||
                binding_layout->stride == 0u ||
                layout->offset > binding_layout->stride - component_bytes ||
                (first_vertex + vertex != 0u &&
                 (uint64_t)binding_layout->stride >
                     (UINT64_MAX - binding->offset) /
                         (first_vertex + vertex))) {
                free(values);
                return RIN_GPU_ERROR_BOUNDS;
            }
            vertex_base = binding->offset +
                (uint64_t)(first_vertex + vertex) * binding_layout->stride;
            if (layout->offset > UINT64_MAX - vertex_base) {
                free(values);
                return RIN_GPU_ERROR_BOUNDS;
            }
            offset = vertex_base + layout->offset;
            if (offset > buffer->size_bytes ||
                component_bytes > buffer->size_bytes - offset) {
                free(values);
                return RIN_GPU_ERROR_BOUNDS;
            }
            result = backend_decode_vertex_component(
                layout->format, buffer->bytes + (size_t)offset,
                &values[(size_t)vertex * pipeline->input_count +
                        layout->location]);
            if (result != RIN_GPU_OK) {
                free(values);
                return result;
            }
        }
    }
    *values_out = values;
    return RIN_GPU_OK;
}

struct RinGLAquamarineSurfaceContext {
    RinGpuCore core;
    RinGpuDisplayInfoV1 display;
    RinGLAquamarineSurfaceTargetV1 target;
    AqSurface aquamarine_surface;
    RinWebGLSoftwareContextV1 software_context;
    RinGpuHandle queue;
    RinGpuHandle command_list;
    RinGpuHandle color_image;
    RinGpuHandle depth_image;
    uint32_t color_state;
    uint32_t depth_state;
    uint32_t initialized;
};

static int target_valid(const RinGLAquamarineSurfaceTargetV1* target)
{
    uint64_t row_bytes;
    uint64_t total_bytes;

    if (!target ||
        (target->struct_size !=
             offsetof(RinGLAquamarineSurfaceTargetV1, stencil) &&
         target->struct_size != sizeof(*target)) ||
        target->version != RINGL_AQUAMARINE_SURFACE_VERSION ||
        !target->pixels || target->width == 0u || target->height == 0u ||
        target->width > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        target->height > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        target->pitch_bytes > RINGL_AQUAMARINE_SURFACE_MAX_PITCH_BYTES ||
        target->reserved0 != 0u) {
        return 0;
    }
    row_bytes = (uint64_t)target->width * 4u;
    total_bytes = (uint64_t)target->pitch_bytes * target->height;
    if (target->pitch_bytes < row_bytes || total_bytes == 0u ||
        total_bytes > UINT32_MAX) {
        return 0;
    }
    if ((target->depth == NULL) != (target->depth_pitch_floats == 0u) ||
        (target->depth &&
         (target->depth_pitch_floats < target->width ||
          target->depth_pitch_floats > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION))) {
        return 0;
    }
    if (target->struct_size == sizeof(*target) &&
        (target->reserved1 != 0u ||
         (target->stencil == NULL) != (target->stencil_pitch_bytes == 0u) ||
         (target->stencil &&
          (target->depth == NULL || target->stencil_pitch_bytes < target->width ||
           target->stencil_pitch_bytes >
               RINGL_AQUAMARINE_SURFACE_MAX_PITCH_BYTES)))) {
        return 0;
    }
    return 1;
}

static void initialize_aquamarine_surface(
    RinGLAquamarineSurfaceContext* context)
{
    AqSurface* surface = &context->aquamarine_surface;

    memset(surface, 0, sizeof(*surface));
    surface->pixels = context->target.pixels;
    surface->width = (int32_t)context->target.width;
    surface->height = (int32_t)context->target.height;
    surface->pitch = (int32_t)context->target.pitch_bytes;
    surface->format = AQ_FORMAT_BGRA32;
    surface->bpp = 32u;
    surface->owns_pixels = 0u;
    surface->clip = AQ_RECT(0, 0, surface->width, surface->height);
}

static int backend_placeholder_create(void* opaque, const void* descriptor,
                                      uint64_t* cookie)
{
    (void)opaque;
    if (!descriptor || !cookie) return RIN_GPU_ERROR_INVALID_ARGUMENT;
    *cookie = UINT64_C(1);
    return RIN_GPU_OK;
}

static int backend_create_buffer(void* opaque, const RinGpuBufferDescV1* desc,
                                 uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceBuffer* buffer;

    if (!context || !desc || !cookie || desc->size_bytes == 0u ||
        desc->size_bytes > SIZE_MAX) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    buffer = calloc(1u, sizeof(*buffer));
    if (!buffer) return RIN_GPU_ERROR_NO_MEMORY;
    buffer->bytes = calloc(1u, (size_t)desc->size_bytes);
    if (!buffer->bytes) {
        free(buffer);
        return RIN_GPU_ERROR_NO_MEMORY;
    }
    buffer->owner = context;
    buffer->size_bytes = desc->size_bytes;
    *cookie = (uint64_t)(uintptr_t)buffer;
    return RIN_GPU_OK;
}

static void backend_destroy_buffer(void* opaque, uint64_t cookie)
{
    RinGLAquamarineSurfaceBuffer* buffer =
        (RinGLAquamarineSurfaceBuffer*)(uintptr_t)cookie;
    (void)opaque;
    if (!buffer) return;
    free(buffer->bytes);
    free(buffer);
}

static int backend_upload_buffer(void* opaque, uint64_t cookie,
                                 uint64_t destination_offset,
                                 const void* source, uint64_t size_bytes)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceBuffer* buffer =
        (RinGLAquamarineSurfaceBuffer*)(uintptr_t)cookie;

    if (!context || !buffer || buffer->owner != context || !source ||
        size_bytes == 0u || destination_offset > buffer->size_bytes ||
        size_bytes > buffer->size_bytes - destination_offset) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    memcpy(buffer->bytes + (size_t)destination_offset, source,
           (size_t)size_bytes);
    return RIN_GPU_OK;
}

static void backend_destroy_placeholder(void* opaque, uint64_t cookie)
{
    (void)opaque;
    (void)cookie;
}

static int backend_create_image(void* opaque, const RinGpuImageDescV1* desc,
                                uint64_t allocation_bytes, uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceImage* image;
    int caller_owned_depth = 0;
    int caller_owned_depth_stencil = 0;

    if (!context || !desc || !cookie || allocation_bytes == 0u ||
        allocation_bytes > SIZE_MAX ||
        desc->dimension != RIN_GPU_IMAGE_DIMENSION_2D ||
        desc->width == 0u || desc->height == 0u || desc->depth != 1u ||
        desc->width > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        desc->height > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        desc->array_layers != 1u || desc->mip_levels == 0u ||
        desc->mip_levels > RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS ||
        desc->sample_count != 1u) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    if (desc->format == RIN_GPU_FORMAT_BGRA8_UNORM &&
        (desc->usage & RIN_GPU_IMAGE_PRESENT) != 0u &&
        desc->width == context->target.width &&
        desc->height == context->target.height) {
        /* The caller-owned display target is this image's backing store. */
    } else if (desc->format == RIN_GPU_FORMAT_D32_FLOAT &&
               (desc->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) != 0u &&
               context->target.depth != NULL &&
               desc->width == context->target.width &&
               desc->height == context->target.height) {
        /* The optional caller-owned depth target is likewise direct-backed. */
        caller_owned_depth = 1;
    } else if (desc->format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
               (desc->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) != 0u &&
               context->target.depth != NULL && context->target.stencil != NULL &&
               desc->width == context->target.width &&
               desc->height == context->target.height) {
        /* A caller may supply separate D32 and S8 planes for its surface. */
        caller_owned_depth_stencil = 1;
    } else if ((desc->format == RIN_GPU_FORMAT_RGBA8_UNORM ||
                backend_packed_color_format(desc->format)) &&
               (desc->usage & RIN_GPU_IMAGE_PRESENT) == 0u) {
        /* Generic RinGPU color images back custom WebGL framebuffers. */
    } else if (desc->format == RIN_GPU_FORMAT_D32_FLOAT &&
               (desc->usage & RIN_GPU_IMAGE_PRESENT) == 0u &&
               (desc->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) != 0u) {
        /* Generic D32 images back custom depth renderbuffer attachments. */
    } else if (desc->format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
               (desc->usage & RIN_GPU_IMAGE_PRESENT) == 0u &&
               (desc->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) != 0u) {
        /* Combined images back custom depth-stencil renderbuffers. */
    } else if (desc->format == RIN_GPU_FORMAT_S8_UINT &&
               (desc->usage & RIN_GPU_IMAGE_PRESENT) == 0u &&
               (desc->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) != 0u) {
        /* Native S8 images back custom stencil-only renderbuffers. */
    } else {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    /* The caller-owned scanout/depth planes only describe level zero. Accept
     * additional mips exclusively for independently allocated offscreen
     * storage; rendering and sampling those levels still fail closed below. */
    if (desc->mip_levels != 1u &&
        ((desc->usage & RIN_GPU_IMAGE_PRESENT) != 0u || caller_owned_depth ||
         caller_owned_depth_stencil)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    image = calloc(1u, sizeof(*image));
    if (!image) return RIN_GPU_ERROR_NO_MEMORY;
    image->owner = context;
    image->descriptor = *desc;
    image->descriptor.struct_size = sizeof(image->descriptor);
    image->allocation_bytes = allocation_bytes;
    if (desc->format == RIN_GPU_FORMAT_BGRA8_UNORM) {
        image->target_kind = RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR;
    } else if (caller_owned_depth) {
        image->target_kind = RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH;
    } else if (caller_owned_depth_stencil) {
        image->target_kind = RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL;
    } else {
        image->bytes = calloc(1u, (size_t)allocation_bytes);
        if (!image->bytes) {
            free(image);
            return RIN_GPU_ERROR_NO_MEMORY;
        }
        image->pitch_bytes = (uint64_t)desc->width *
            backend_image_texel_bytes(desc->format);
        image->target_kind = desc->format == RIN_GPU_FORMAT_D32_FLOAT
            ? RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH
            : (desc->format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
                   ? RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL
                   : (desc->format == RIN_GPU_FORMAT_S8_UINT
                          ? RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_STENCIL
                          : RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR));
    }
    *cookie = (uint64_t)(uintptr_t)image;
    return RIN_GPU_OK;
}

static void backend_destroy_image(void* opaque, uint64_t cookie)
{
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    (void)opaque;
    if (!image) return;
    free(image->bytes);
    free(image);
}

static int backend_upload_image(void* opaque, uint64_t cookie,
                                const RinGpuImageUploadV1* upload,
                                const void* source, uint64_t source_size)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    const uint8_t* source_bytes = source;
    uint8_t* target_bytes;
    uint64_t target_row_pitch;
    uint64_t row_bytes;
    uint64_t minimum_slice_pitch;
    uint64_t required_size;
    uint64_t texel_bytes;
    uint32_t mip_width;
    uint32_t mip_height;

    if (!context || !image || image->owner != context || !upload ||
        !source || source_size == 0u || upload->array_layer != 0u ||
        upload->z != 0u || upload->depth != 1u || upload->width == 0u ||
        upload->height == 0u ||
        upload->source_row_pitch_bytes == 0u ||
        upload->source_slice_pitch_bytes == 0u) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    texel_bytes = backend_image_texel_bytes(image->descriptor.format);
    if (texel_bytes == 0u)
        return RIN_GPU_ERROR_UNSUPPORTED;
    if (!backend_image_mip_dimensions(&image->descriptor, upload->mip_level,
                                      &mip_width, &mip_height) ||
        upload->x > mip_width || upload->width > mip_width - upload->x ||
        upload->y > mip_height || upload->height > mip_height - upload->y ||
        !backend_multiply_u64(upload->width, texel_bytes, &row_bytes) ||
        upload->source_row_pitch_bytes < row_bytes ||
        !backend_multiply_u64(upload->source_row_pitch_bytes, upload->height,
                              &minimum_slice_pitch) ||
        upload->source_slice_pitch_bytes < minimum_slice_pitch ||
        !backend_multiply_u64(upload->height - 1u,
                              upload->source_row_pitch_bytes,
                              &required_size) ||
        !backend_add_u64(required_size, row_bytes, &required_size) ||
        source_size < required_size) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    if (image->target_kind ==
            RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL ||
        image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
        float* depth;
        uint8_t* stencil;
        uint32_t depth_pitch;
        uint32_t stencil_pitch;

        if (image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
            depth = context->target.depth;
            stencil = context->target.stencil;
            depth_pitch = context->target.depth_pitch_floats;
            stencil_pitch = context->target.stencil_pitch_bytes;
        } else {
            if (offscreen_depth_stencil_storage_at_mip(
                    image, upload->mip_level, &depth, &stencil,
                    &depth_pitch) != RIN_GPU_OK)
                return RIN_GPU_ERROR_STATE;
            stencil_pitch = depth_pitch;
        }
        if (!depth || !stencil || depth_pitch < mip_width ||
            stencil_pitch < mip_width) {
            return RIN_GPU_ERROR_STATE;
        }

        /* CPU copies use native-endian f32 depth followed by S8 and padding. */
        for (uint32_t row = 0u; row < upload->height; ++row) {
            const uint8_t* source_row = source_bytes +
                (uint64_t)row * upload->source_row_pitch_bytes;
            float* depth_row = depth +
                ((uint64_t)upload->y + row) * depth_pitch + upload->x;
            uint8_t* stencil_row = stencil +
                ((uint64_t)upload->y + row) * stencil_pitch + upload->x;
            for (uint32_t column = 0u; column < upload->width; ++column) {
                memcpy(&depth_row[column], source_row +
                    (uint64_t)column * texel_bytes, sizeof(float));
                stencil_row[column] = source_row[
                    (uint64_t)column * texel_bytes + sizeof(float)];
            }
        }
        return RIN_GPU_OK;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR) {
        target_bytes = context->target.pixels;
        target_row_pitch = context->target.pitch_bytes;
    } else if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH &&
               context->target.depth != NULL) {
        target_bytes = (uint8_t*)context->target.depth;
        target_row_pitch =
            (uint64_t)context->target.depth_pitch_floats * sizeof(float);
    } else {
        if (offscreen_image_storage(image, upload->mip_level, &target_bytes,
                                    &target_row_pitch, &mip_width,
                                    &mip_height) != RIN_GPU_OK) {
            return RIN_GPU_ERROR_STATE;
        }
    }
    for (uint32_t row = 0u; row < upload->height; row++) {
        memcpy(target_bytes +
                   ((uint64_t)upload->y + row) * target_row_pitch +
                   (uint64_t)upload->x * texel_bytes,
               source_bytes + (uint64_t)row * upload->source_row_pitch_bytes,
               (size_t)row_bytes);
    }
    return RIN_GPU_OK;
}

static int backend_readback_image(void* opaque, uint64_t cookie,
                                  const RinGpuImageReadbackV1* readback,
                                  void* destination,
                                  uint64_t destination_size)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    const uint8_t* source_bytes;
    uint64_t source_row_pitch;
    uint64_t row_bytes;
    uint64_t minimum_slice_pitch;
    uint64_t required_size;
    uint64_t texel_bytes;
    uint8_t* offscreen_source_bytes;
    uint32_t mip_width;
    uint32_t mip_height;

    if (!context || !image || image->owner != context || !readback ||
        !destination || destination_size == 0u ||
        readback->array_layer != 0u || readback->z != 0u ||
        readback->depth != 1u || readback->width == 0u ||
        readback->height == 0u ||
        readback->destination_row_pitch_bytes == 0u ||
        readback->destination_slice_pitch_bytes == 0u) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    texel_bytes = backend_image_texel_bytes(image->descriptor.format);
    if (texel_bytes == 0u)
        return RIN_GPU_ERROR_UNSUPPORTED;
    if (!backend_image_mip_dimensions(&image->descriptor,
                                      readback->mip_level, &mip_width,
                                      &mip_height) ||
        readback->x > mip_width ||
        readback->width > mip_width - readback->x ||
        readback->y > mip_height ||
        readback->height > mip_height - readback->y ||
        !backend_multiply_u64(readback->width, texel_bytes, &row_bytes) ||
        readback->destination_row_pitch_bytes < row_bytes ||
        !backend_multiply_u64(readback->destination_row_pitch_bytes,
                              readback->height, &minimum_slice_pitch) ||
        readback->destination_slice_pitch_bytes < minimum_slice_pitch ||
        !backend_multiply_u64(readback->height - 1u,
                              readback->destination_row_pitch_bytes,
                              &required_size) ||
        !backend_add_u64(required_size, row_bytes, &required_size) ||
        destination_size < required_size) {
        return RIN_GPU_ERROR_BOUNDS;
    }
    if (image->target_kind ==
            RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL ||
        image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
        const float* depth;
        const uint8_t* stencil;
        uint32_t depth_pitch;
        uint32_t stencil_pitch;

        if (image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
            depth = context->target.depth;
            stencil = context->target.stencil;
            depth_pitch = context->target.depth_pitch_floats;
            stencil_pitch = context->target.stencil_pitch_bytes;
        } else {
            float* writable_depth;
            uint8_t* writable_stencil;

            if (offscreen_depth_stencil_storage_at_mip(
                    image, readback->mip_level, &writable_depth,
                    &writable_stencil, &depth_pitch) != RIN_GPU_OK) {
                return RIN_GPU_ERROR_STATE;
            }
            depth = writable_depth;
            stencil = writable_stencil;
            stencil_pitch = depth_pitch;
        }
        if (!depth || !stencil || depth_pitch < mip_width ||
            stencil_pitch < mip_width) {
            return RIN_GPU_ERROR_STATE;
        }

        /* Keep the three unused S8 padding bytes deterministic for callers. */
        for (uint32_t row = 0u; row < readback->height; ++row) {
            uint8_t* destination_row = (uint8_t*)destination +
                (uint64_t)row * readback->destination_row_pitch_bytes;
            const float* depth_row = depth +
                ((uint64_t)readback->y + row) * depth_pitch + readback->x;
            const uint8_t* stencil_row = stencil +
                ((uint64_t)readback->y + row) * stencil_pitch + readback->x;
            for (uint32_t column = 0u; column < readback->width; ++column) {
                uint8_t* destination_texel = destination_row +
                    (uint64_t)column * texel_bytes;
                memcpy(destination_texel, &depth_row[column], sizeof(float));
                destination_texel[sizeof(float)] = stencil_row[column];
                memset(destination_texel + sizeof(float) + 1u, 0,
                       texel_bytes - sizeof(float) - 1u);
            }
        }
        return RIN_GPU_OK;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR) {
        source_bytes = context->target.pixels;
        source_row_pitch = context->target.pitch_bytes;
    } else if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH &&
               context->target.depth != NULL) {
        source_bytes = (const uint8_t*)context->target.depth;
        source_row_pitch =
            (uint64_t)context->target.depth_pitch_floats * sizeof(float);
    } else {
        if (offscreen_image_storage(image, readback->mip_level,
                                    &offscreen_source_bytes,
                                    &source_row_pitch, &mip_width,
                                    &mip_height) != RIN_GPU_OK) {
            return RIN_GPU_ERROR_STATE;
        }
        source_bytes = offscreen_source_bytes;
    }
    for (uint32_t row = 0u; row < readback->height; row++) {
        memcpy((uint8_t*)destination +
                   (uint64_t)row * readback->destination_row_pitch_bytes,
               source_bytes + ((uint64_t)readback->y + row) *
                   source_row_pitch +
                   (uint64_t)readback->x * texel_bytes,
               (size_t)row_bytes);
    }
    return RIN_GPU_OK;
}

static int backend_wait_for_completion(void* opaque, uint64_t timeout_ns)
{
    (void)timeout_ns;
    return opaque ? RIN_GPU_OK : RIN_GPU_ERROR_INVALID_ARGUMENT;
}

static int backend_create_sampler(void* opaque, const RinGpuSamplerDescV1* desc,
                                  uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceSampler* sampler;

    if (!context || !desc || !cookie ||
        desc->abi_version != RIN_GPU_ABI_VERSION ||
        desc->struct_size != sizeof(*desc) ||
        (desc->min_filter != RIN_GPU_SAMPLER_FILTER_NEAREST &&
         desc->min_filter != RIN_GPU_SAMPLER_FILTER_LINEAR) ||
        (desc->mag_filter != RIN_GPU_SAMPLER_FILTER_NEAREST &&
         desc->mag_filter != RIN_GPU_SAMPLER_FILTER_LINEAR) ||
        (desc->mip_filter != RIN_GPU_SAMPLER_MIP_FILTER_NONE &&
         desc->mip_filter != RIN_GPU_SAMPLER_FILTER_NEAREST &&
         desc->mip_filter != RIN_GPU_SAMPLER_FILTER_LINEAR) ||
        desc->address_u < RIN_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE ||
        desc->address_u > RIN_GPU_SAMPLER_ADDRESS_MIRRORED_REPEAT ||
        desc->address_v < RIN_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE ||
        desc->address_v > RIN_GPU_SAMPLER_ADDRESS_MIRRORED_REPEAT ||
        desc->address_w != RIN_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE ||
        desc->mip_lod_bias != desc->mip_lod_bias ||
        desc->mip_lod_bias < -16.0f || desc->mip_lod_bias > 16.0f ||
        desc->min_lod != desc->min_lod || desc->min_lod < 0.0f ||
        desc->min_lod > 32.0f || desc->max_lod != desc->max_lod ||
        desc->max_lod < desc->min_lod || desc->max_lod > 32.0f ||
        desc->max_anisotropy != 1u ||
        desc->flags != 0u || desc->compare_op != 0u) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    sampler = calloc(1u, sizeof(*sampler));
    if (!sampler) return RIN_GPU_ERROR_NO_MEMORY;
    sampler->owner = context;
    sampler->descriptor = *desc;
    sampler->descriptor.struct_size = sizeof(sampler->descriptor);
    *cookie = (uint64_t)(uintptr_t)sampler;
    return RIN_GPU_OK;
}

static void backend_destroy_sampler(void* opaque, uint64_t cookie)
{
    RinGLAquamarineSurfaceSampler* sampler =
        (RinGLAquamarineSurfaceSampler*)(uintptr_t)cookie;
    (void)opaque;
    free(sampler);
}

static int backend_create_shader(void* opaque, const void* shader,
                                 uint64_t shader_size,
                                 const RinShaderInfoV1* shader_info,
                                 uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceShader* module;

    if (!context || !shader || !shader_info || !cookie ||
        shader_size == 0u || shader_size > UINT32_MAX ||
        shader_info->struct_size != sizeof(*shader_info) ||
        shader_info->abi_version != RIN_GPU_ABI_VERSION) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    module = calloc(1u, sizeof(*module));
    if (!module) return RIN_GPU_ERROR_NO_MEMORY;
    module->bytes = malloc((size_t)shader_size);
    if (!module->bytes) {
        free(module);
        return RIN_GPU_ERROR_NO_MEMORY;
    }
    memcpy(module->bytes, shader, (size_t)shader_size);
    module->owner = context;
    module->size_bytes = (uint32_t)shader_size;
    module->info = *shader_info;
    *cookie = (uint64_t)(uintptr_t)module;
    return RIN_GPU_OK;
}

static void backend_destroy_shader(void* opaque, uint64_t cookie)
{
    RinGLAquamarineSurfaceShader* module =
        (RinGLAquamarineSurfaceShader*)(uintptr_t)cookie;
    (void)opaque;
    if (!module) return;
    free(module->bytes);
    free(module);
}

static int backend_create_compute_pipeline(void* opaque,
                                           uint64_t shader_cookie,
                                           const RinShaderInfoV1* shader_info,
                                           uint64_t* cookie)
{
    return backend_placeholder_create(
        opaque, shader_cookie != 0u ? shader_info : NULL, cookie);
}

static int backend_stencil_face_valid(uint32_t compare, uint32_t reference,
                                      uint32_t read_mask, uint32_t write_mask,
                                      uint32_t fail_operation,
                                      uint32_t depth_fail_operation,
                                      uint32_t pass_operation)
{
    return compare >= RIN_GPU_COMPARE_LESS &&
           compare <= RIN_GPU_COMPARE_GREATER_EQUAL && reference <= 0xffu &&
           read_mask <= 0xffu && write_mask <= 0xffu &&
           fail_operation >= RIN_GPU_STENCIL_KEEP &&
           fail_operation <= RIN_GPU_STENCIL_DECREMENT_WRAP &&
           depth_fail_operation >= RIN_GPU_STENCIL_KEEP &&
           depth_fail_operation <= RIN_GPU_STENCIL_DECREMENT_WRAP &&
           pass_operation >= RIN_GPU_STENCIL_KEEP &&
           pass_operation <= RIN_GPU_STENCIL_DECREMENT_WRAP;
}

static int backend_stencil_pipeline_valid(
    const RinGpuBackendGraphicsPipelineDescV1* descriptor)
{
    if (!descriptor)
        return 0;
    if (descriptor->stencil_test_enabled == 0u) {
        return descriptor->stencil_compare == 0u &&
               descriptor->stencil_reference == 0u &&
               descriptor->stencil_read_mask == 0u &&
               descriptor->stencil_write_mask == 0u &&
               descriptor->stencil_fail_operation == 0u &&
               descriptor->stencil_depth_fail_operation == 0u &&
               descriptor->stencil_pass_operation == 0u &&
               descriptor->separate_stencil_enabled == 0u &&
               descriptor->back_stencil_compare == 0u &&
               descriptor->back_stencil_reference == 0u &&
               descriptor->back_stencil_read_mask == 0u &&
               descriptor->back_stencil_write_mask == 0u &&
               descriptor->back_stencil_fail_operation == 0u &&
               descriptor->back_stencil_depth_fail_operation == 0u &&
               descriptor->back_stencil_pass_operation == 0u;
    }
    if (descriptor->stencil_test_enabled != 1u ||
        (descriptor->depth_format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
         descriptor->depth_format != RIN_GPU_FORMAT_S8_UINT) ||
        !backend_stencil_face_valid(
            descriptor->stencil_compare, descriptor->stencil_reference,
            descriptor->stencil_read_mask, descriptor->stencil_write_mask,
            descriptor->stencil_fail_operation,
            descriptor->stencil_depth_fail_operation,
            descriptor->stencil_pass_operation)) {
        return 0;
    }
    if (descriptor->separate_stencil_enabled == 0u) {
        return descriptor->back_stencil_compare == 0u &&
               descriptor->back_stencil_reference == 0u &&
               descriptor->back_stencil_read_mask == 0u &&
               descriptor->back_stencil_write_mask == 0u &&
               descriptor->back_stencil_fail_operation == 0u &&
               descriptor->back_stencil_depth_fail_operation == 0u &&
               descriptor->back_stencil_pass_operation == 0u;
    }
    return descriptor->separate_stencil_enabled == 1u &&
           backend_stencil_face_valid(
               descriptor->back_stencil_compare,
               descriptor->back_stencil_reference,
               descriptor->back_stencil_read_mask,
               descriptor->back_stencil_write_mask,
               descriptor->back_stencil_fail_operation,
               descriptor->back_stencil_depth_fail_operation,
               descriptor->back_stencil_pass_operation);
}

static int backend_blend_pipeline_valid(
    const RinGpuBackendGraphicsPipelineDescV1* descriptor)
{
    uint32_t bits;

    if (!descriptor || descriptor->color_write_mask == 0u ||
        (descriptor->color_write_mask & ~RIN_GPU_COLOR_WRITE_ALL) != 0u) {
        return 0;
    }
    memcpy(&bits, &descriptor->blend_constant_red, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u ||
        descriptor->blend_constant_red < 0.0f ||
        descriptor->blend_constant_red > 1.0f)
        return 0;
    memcpy(&bits, &descriptor->blend_constant_green, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u ||
        descriptor->blend_constant_green < 0.0f ||
        descriptor->blend_constant_green > 1.0f)
        return 0;
    memcpy(&bits, &descriptor->blend_constant_blue, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u ||
        descriptor->blend_constant_blue < 0.0f ||
        descriptor->blend_constant_blue > 1.0f)
        return 0;
    memcpy(&bits, &descriptor->blend_constant_alpha, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u ||
        descriptor->blend_constant_alpha < 0.0f ||
        descriptor->blend_constant_alpha > 1.0f)
        return 0;
    if (descriptor->blend_enabled == 0u) {
        return descriptor->source_color_factor == 0u &&
               descriptor->destination_color_factor == 0u &&
               descriptor->color_operation == 0u &&
               descriptor->source_alpha_factor == 0u &&
               descriptor->destination_alpha_factor == 0u &&
               descriptor->alpha_operation == 0u &&
               descriptor->blend_constant_red == 0.0f &&
               descriptor->blend_constant_green == 0.0f &&
               descriptor->blend_constant_blue == 0.0f &&
               descriptor->blend_constant_alpha == 0.0f;
    }
    return descriptor->blend_enabled == 1u &&
           descriptor->source_color_factor >= RIN_GPU_BLEND_ZERO &&
           descriptor->source_color_factor <=
               RIN_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA &&
           descriptor->destination_color_factor >= RIN_GPU_BLEND_ZERO &&
           ((descriptor->destination_color_factor <=
             RIN_GPU_BLEND_ONE_MINUS_DESTINATION_COLOR) ||
            descriptor->destination_color_factor >= RIN_GPU_BLEND_CONSTANT_COLOR) &&
           descriptor->color_operation >= RIN_GPU_BLEND_ADD &&
           descriptor->color_operation <= RIN_GPU_BLEND_MAXIMUM &&
           descriptor->source_alpha_factor >= RIN_GPU_BLEND_ZERO &&
           descriptor->source_alpha_factor <=
               RIN_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA &&
           descriptor->destination_alpha_factor >= RIN_GPU_BLEND_ZERO &&
           ((descriptor->destination_alpha_factor <=
             RIN_GPU_BLEND_ONE_MINUS_DESTINATION_COLOR) ||
            descriptor->destination_alpha_factor >= RIN_GPU_BLEND_CONSTANT_COLOR) &&
           descriptor->alpha_operation >= RIN_GPU_BLEND_ADD &&
           descriptor->alpha_operation <= RIN_GPU_BLEND_MAXIMUM;
}

static int backend_texture_resource_layout_valid(
    const RinGpuBackendGraphicsPipelineDescV1* descriptor,
    const RinShaderInfoV1* vertex_shader_info,
    const RinShaderInfoV1* fragment_shader_info)
{
    uint32_t resource;

    if (!descriptor || !vertex_shader_info || !fragment_shader_info ||
        vertex_shader_info->resource_count != 0u ||
        descriptor->resource_count != fragment_shader_info->resource_count ||
        descriptor->resource_count > RIN_SHADER_MAX_RESOURCES ||
        (descriptor->resource_count & 1u) != 0u) {
        return 0;
    }
    for (resource = 0u; resource < descriptor->resource_count; ++resource) {
        uint32_t expected = (resource & 1u) == 0u
            ? RIN_SHADER_RESOURCE_SAMPLED_IMAGE
            : RIN_SHADER_RESOURCE_SAMPLER;
        if (descriptor->resource_kinds[resource] != expected)
            return 0;
    }
    return 1;
}

static int backend_create_graphics_pipeline(
    void* opaque, uint64_t vertex_shader_cookie,
    const RinShaderInfoV1* vertex_shader_info, uint64_t fragment_shader_cookie,
    const RinShaderInfoV1* fragment_shader_info,
    const RinGpuBackendGraphicsPipelineDescV1* descriptor, uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceShader* vertex_shader =
        (RinGLAquamarineSurfaceShader*)(uintptr_t)vertex_shader_cookie;
    RinGLAquamarineSurfaceShader* fragment_shader =
        (RinGLAquamarineSurfaceShader*)(uintptr_t)fragment_shader_cookie;
    RinGLAquamarineSurfacePipeline* pipeline;
    uint32_t attribute;
    uint32_t resource;

    if (vertex_shader_cookie == 0u || fragment_shader_cookie == 0u ||
        !context || !vertex_shader || !fragment_shader ||
        vertex_shader->owner != context || fragment_shader->owner != context ||
        !vertex_shader_info || !fragment_shader_info || !descriptor ||
        !cookie ||
        (descriptor->color_format != RIN_GPU_FORMAT_BGRA8_UNORM &&
         descriptor->color_format != RIN_GPU_FORMAT_RGBA8_UNORM &&
         !backend_packed_color_format(descriptor->color_format)) ||
        !backend_primitive_topology_valid(descriptor->primitive_topology) ||
        descriptor->flags != 0u || descriptor->reserved != 0u ||
        vertex_shader_info->stage != RIN_SHADER_STAGE_VERTEX ||
        fragment_shader_info->stage != RIN_SHADER_STAGE_FRAGMENT ||
        !backend_texture_resource_layout_valid(descriptor, vertex_shader_info,
                                               fragment_shader_info) ||
        descriptor->vertex_input_count == 0u ||
        descriptor->vertex_input_count != vertex_shader_info->input_count ||
        descriptor->vertex_stride > RIN_GPU_MAX_VERTEX_STRIDE ||
         !backend_blend_pipeline_valid(descriptor) ||
         ((descriptor->depth_format != 0u &&
           descriptor->depth_format != RIN_GPU_FORMAT_S8_UINT &&
           ((descriptor->depth_format != RIN_GPU_FORMAT_D32_FLOAT &&
             descriptor->depth_format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
             descriptor->depth_format != RIN_GPU_FORMAT_S8_UINT) ||
            (descriptor->depth_compare != RIN_GPU_COMPARE_NEVER &&
             descriptor->depth_compare != RIN_GPU_COMPARE_LESS &&
             descriptor->depth_compare != RIN_GPU_COMPARE_EQUAL &&
             descriptor->depth_compare != RIN_GPU_COMPARE_LESS_EQUAL &&
             descriptor->depth_compare != RIN_GPU_COMPARE_GREATER &&
             descriptor->depth_compare != RIN_GPU_COMPARE_NOT_EQUAL &&
             descriptor->depth_compare != RIN_GPU_COMPARE_GREATER_EQUAL &&
             descriptor->depth_compare != RIN_GPU_COMPARE_ALWAYS) ||
            descriptor->depth_write_enabled > 1u)) ||
          (descriptor->depth_format == RIN_GPU_FORMAT_S8_UINT &&
           (descriptor->depth_compare != 0u ||
            descriptor->depth_write_enabled != 0u))) ||
         !backend_stencil_pipeline_valid(descriptor) ||
         descriptor->cull_mode < RIN_GPU_CULL_NONE ||
         descriptor->cull_mode > RIN_GPU_CULL_BACK ||
         (descriptor->front_face != RIN_GPU_FRONT_FACE_COUNTER_CLOCKWISE &&
          descriptor->front_face != RIN_GPU_FRONT_FACE_CLOCKWISE)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    if (descriptor->vertex_binding_count > RIN_GPU_MAX_VERTEX_BUFFER_BINDINGS ||
        (descriptor->vertex_stride != 0u &&
         (descriptor->vertex_binding_count != 1u ||
          descriptor->vertex_bindings[0].binding != 0u ||
          descriptor->vertex_bindings[0].stride != descriptor->vertex_stride ||
          descriptor->vertex_bindings[0].flags != 0u ||
          descriptor->vertex_bindings[0].reserved != 0u))) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    if (descriptor->vertex_stride == 0u) {
        for (uint32_t binding = 0u;
             binding < descriptor->vertex_binding_count; ++binding) {
            const RinGpuVertexBufferLayoutV1* layout =
                &descriptor->vertex_bindings[binding];
            if (layout->binding != binding || layout->stride == 0u ||
                layout->stride > RIN_GPU_MAX_VERTEX_STRIDE ||
                layout->flags != 0u || layout->reserved != 0u) {
                return RIN_GPU_ERROR_INVALID_ARGUMENT;
            }
        }
    }
    for (resource = descriptor->resource_count;
         resource < RIN_SHADER_MAX_RESOURCES; ++resource) {
        if (descriptor->resource_kinds[resource] != RIN_SHADER_RESOURCE_NONE)
            return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    for (attribute = 0u; attribute < descriptor->vertex_input_count;
         ++attribute) {
        const RinGpuBackendVertexAttributeV1* source =
            &descriptor->vertex_attributes[attribute];
        uint32_t component_bytes = backend_vertex_format_bytes(source->format);

        if (source->location != attribute || component_bytes == 0u ||
            (source->flags == RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32
                 ? source->format != RIN_GPU_VERTEX_FLOAT32 ||
                       source->binding != 0u
                 : (source->flags != 0u ||
                    (descriptor->vertex_stride != 0u
                         ? (source->binding != 0u ||
                            source->offset > descriptor->vertex_stride -
                                component_bytes)
                         : (source->binding >=
                                descriptor->vertex_binding_count ||
                            source->offset >
                                descriptor->vertex_bindings[source->binding]
                                    .stride - component_bytes))))) {
            return RIN_GPU_ERROR_INVALID_ARGUMENT;
        }
    }
    pipeline = calloc(1u, sizeof(*pipeline));
    if (!pipeline) return RIN_GPU_ERROR_NO_MEMORY;
    pipeline->owner = context;
    pipeline->vertex_shader = vertex_shader;
    pipeline->fragment_shader = fragment_shader;
    pipeline->primitive_topology = descriptor->primitive_topology;
    pipeline->input_count = descriptor->vertex_input_count;
    pipeline->vertex_stride = descriptor->vertex_stride;
    pipeline->vertex_binding_count = descriptor->vertex_binding_count;
    memcpy(pipeline->vertex_attributes, descriptor->vertex_attributes,
           sizeof(pipeline->vertex_attributes));
    memcpy(pipeline->vertex_bindings, descriptor->vertex_bindings,
           sizeof(pipeline->vertex_bindings));
    pipeline->resource_count = descriptor->resource_count;
    pipeline->depth_enabled =
        descriptor->depth_format == RIN_GPU_FORMAT_D32_FLOAT ||
        descriptor->depth_format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    pipeline->depth_compare = descriptor->depth_compare;
    pipeline->depth_write_enabled = descriptor->depth_write_enabled;
    pipeline->blend_enabled = descriptor->blend_enabled;
    pipeline->source_color_factor = descriptor->source_color_factor;
    pipeline->destination_color_factor = descriptor->destination_color_factor;
    pipeline->color_operation = descriptor->color_operation;
    pipeline->source_alpha_factor = descriptor->source_alpha_factor;
    pipeline->destination_alpha_factor = descriptor->destination_alpha_factor;
    pipeline->alpha_operation = descriptor->alpha_operation;
    pipeline->blend_constant_red = descriptor->blend_constant_red;
    pipeline->blend_constant_green = descriptor->blend_constant_green;
    pipeline->blend_constant_blue = descriptor->blend_constant_blue;
    pipeline->blend_constant_alpha = descriptor->blend_constant_alpha;
    pipeline->color_write_mask = descriptor->color_write_mask;
    pipeline->stencil_enabled = descriptor->stencil_test_enabled;
    pipeline->stencil_compare = descriptor->stencil_compare;
    pipeline->stencil_reference = descriptor->stencil_reference;
    pipeline->stencil_read_mask = descriptor->stencil_read_mask;
    pipeline->stencil_write_mask = descriptor->stencil_write_mask;
    pipeline->stencil_fail_operation = descriptor->stencil_fail_operation;
    pipeline->stencil_depth_fail_operation =
        descriptor->stencil_depth_fail_operation;
    pipeline->stencil_pass_operation = descriptor->stencil_pass_operation;
    pipeline->separate_stencil_enabled = descriptor->separate_stencil_enabled;
    pipeline->back_stencil_compare = descriptor->back_stencil_compare;
    pipeline->back_stencil_reference = descriptor->back_stencil_reference;
    pipeline->back_stencil_read_mask = descriptor->back_stencil_read_mask;
    pipeline->back_stencil_write_mask = descriptor->back_stencil_write_mask;
    pipeline->back_stencil_fail_operation =
        descriptor->back_stencil_fail_operation;
    pipeline->back_stencil_depth_fail_operation =
        descriptor->back_stencil_depth_fail_operation;
    pipeline->back_stencil_pass_operation =
        descriptor->back_stencil_pass_operation;
    pipeline->cull_mode = descriptor->cull_mode;
    pipeline->front_face = descriptor->front_face;
    *cookie = (uint64_t)(uintptr_t)pipeline;
    return RIN_GPU_OK;
}

static void backend_destroy_graphics_pipeline(void* opaque, uint64_t cookie)
{
    (void)opaque;
    free((void*)(uintptr_t)cookie);
}

static int backend_snapshot_sampled_mip(
    const RinGLAquamarineSurfaceImage* image, uint32_t mip_level,
    float** texels_out, uint32_t* texel_count_out, uint32_t* width_out,
    uint32_t* height_out)
{
    uint64_t texel_count;
    float* texels;
    float* depth_storage = NULL;
    uint8_t* source = NULL;
    uint8_t* stencil_storage = NULL;
    uint64_t source_pitch = 0u;
    uint32_t depth_pitch = 0u;
    uint32_t width;
    uint32_t height;

    if (!image || !texels_out || !texel_count_out || !width_out || !height_out ||
        !backend_image_mip_dimensions(&image->descriptor, mip_level, &width,
                                      &height)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    texel_count = (uint64_t)width * height;
    if (texel_count == 0u ||
        texel_count > RIN_WEBGL_SOFTWARE_MAX_SAMPLED_IMAGE_F32_TEXELS ||
        texel_count > SIZE_MAX / (4u * sizeof(*texels))) {
        return RIN_GPU_ERROR_BOUNDS;
    }
    texels = calloc((size_t)texel_count * 4u, sizeof(*texels));
    if (!texels) return RIN_GPU_ERROR_NO_MEMORY;
    if (image->descriptor.format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) {
        if (offscreen_depth_stencil_storage_at_mip(
                image, mip_level, &depth_storage, &stencil_storage,
                &depth_pitch) != RIN_GPU_OK) {
            free(texels);
            return RIN_GPU_ERROR_STATE;
        }
    } else if (offscreen_image_storage(image, mip_level, &source,
                                       &source_pitch, &width, &height) !=
               RIN_GPU_OK) {
        free(texels);
        return RIN_GPU_ERROR_STATE;
    }
    (void)stencil_storage;
    for (uint32_t y = 0u; y < height; ++y) {
        const uint8_t* source_row = source
            ? source + (uint64_t)y * source_pitch : NULL;
        float* destination = texels + (uint64_t)y * width * 4u;
        for (uint32_t x = 0u; x < width; ++x) {
            float* destination_texel = destination + (uint64_t)x * 4u;

            if (backend_packed_color_format(image->descriptor.format)) {
                AqColor color;
                backend_unpack_packed_color(image->descriptor.format,
                                            source_row + (uint64_t)x *
                                                sizeof(uint16_t), &color);
                destination_texel[0] = (float)color.r / 255.0f;
                destination_texel[1] = (float)color.g / 255.0f;
                destination_texel[2] = (float)color.b / 255.0f;
                destination_texel[3] = (float)color.a / 255.0f;
            } else if (image->descriptor.format != RIN_GPU_FORMAT_RGBA8_UNORM) {
                float depth;
                if (image->descriptor.format == RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) {
                    depth = depth_storage[(uint64_t)y * depth_pitch + x];
                } else {
                    memcpy(&depth, source_row + (uint64_t)x * sizeof(depth),
                           sizeof(depth));
                }
                destination_texel[0] = depth;
                destination_texel[1] = 0.0f;
                destination_texel[2] = 0.0f;
                destination_texel[3] = 1.0f;
            } else {
                for (uint32_t component = 0u; component < 4u; ++component) {
                    destination_texel[component] =
                        (float)source_row[(uint64_t)x * 4u + component] / 255.0f;
                }
            }
        }
    }
    *texels_out = texels;
    *texel_count_out = (uint32_t)texel_count;
    *width_out = width;
    *height_out = height;
    return RIN_GPU_OK;
}

static void backend_release_bind_group_texels(RinGLAquamarineSurfaceBindGroup* group)
{
    if (!group) return;
    for (uint32_t image = 0u;
         image < RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES; ++image) {
        for (uint32_t mip = 0u;
             mip < RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS; ++mip) {
            free(group->texels[image][mip]);
            group->texels[image][mip] = NULL;
        }
    }
}

static int backend_create_compute_bind_group(
    void* opaque, uint64_t pipeline_cookie,
    const RinGpuBackendBufferBindingV1* bindings, uint32_t binding_count,
    uint64_t* cookie)
{
    (void)bindings;
    (void)binding_count;
    return backend_placeholder_create(
        opaque, pipeline_cookie != 0u ? &pipeline_cookie : NULL, cookie);
}

static int backend_create_graphics_bind_group(
    void* opaque, uint64_t pipeline_cookie,
    const RinGpuBackendGraphicsBindingV1* bindings, uint32_t binding_count,
    uint64_t* cookie)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    const RinGLAquamarineSurfacePipeline* pipeline =
        (const RinGLAquamarineSurfacePipeline*)(uintptr_t)pipeline_cookie;
    RinGLAquamarineSurfaceBindGroup* group;
    uint32_t sampled_image_count;
    uint32_t sampled_index;

    if (!context || !pipeline || pipeline->owner != context || !cookie ||
        binding_count != pipeline->resource_count ||
        binding_count > RIN_SHADER_MAX_RESOURCES ||
        ((binding_count == 0u && bindings != NULL) ||
         (binding_count != 0u && bindings == NULL)) ||
        (binding_count & 1u) != 0u) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }

    sampled_image_count = binding_count / 2u;
    if (sampled_image_count > RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES)
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    group = calloc(1u, sizeof(*group));
    if (!group) return RIN_GPU_ERROR_NO_MEMORY;
    group->owner = context;
    group->pipeline = pipeline;
    group->sampled_image_count = sampled_image_count;
    for (sampled_index = 0u; sampled_index < sampled_image_count;
         ++sampled_index) {
        const RinGpuBackendGraphicsBindingV1* image_binding =
            &bindings[sampled_index * 2u];
        const RinGpuBackendGraphicsBindingV1* sampler_binding =
            &bindings[sampled_index * 2u + 1u];
        const RinGLAquamarineSurfaceImage* image =
            (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                image_binding->resource_cookie;
        const RinGLAquamarineSurfaceSampler* sampler =
            (const RinGLAquamarineSurfaceSampler*)(uintptr_t)
                sampler_binding->resource_cookie;
        uint32_t mip_count;
        uint32_t mip_width;
        uint32_t mip_height;

        if (image_binding->binding != sampled_index * 2u ||
            image_binding->kind != RIN_SHADER_RESOURCE_SAMPLED_IMAGE ||
            image_binding->access != RIN_GPU_RESOURCE_READ ||
            image_binding->offset != 0u || image_binding->size_bytes != 0u ||
            image_binding->array_layer != 0u ||
            (image_binding->flags &
             ~RIN_GPU_GRAPHICS_BINDING_SAMPLED_MIP_CHAIN) != 0u ||
            sampler_binding->binding != sampled_index * 2u + 1u ||
            sampler_binding->kind != RIN_SHADER_RESOURCE_SAMPLER ||
            sampler_binding->access != 0u || sampler_binding->offset != 0u ||
            sampler_binding->size_bytes != 0u || sampler_binding->mip_level != 0u ||
            sampler_binding->array_layer != 0u || sampler_binding->flags != 0u) {
            goto invalid_argument;
        }
        if (!image || image->owner != context || !image->bytes ||
            (image->target_kind != RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR &&
             image->target_kind != RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH &&
             image->target_kind !=
                 RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL) ||
            (image->descriptor.format != RIN_GPU_FORMAT_RGBA8_UNORM &&
             !backend_packed_color_format(image->descriptor.format) &&
             image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT &&
             image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) ||
            (image->descriptor.usage & RIN_GPU_IMAGE_SAMPLED) == 0u ||
            !backend_image_mip_dimensions(&image->descriptor,
                                          image_binding->mip_level,
                                          &mip_width, &mip_height) ||
            !sampler || sampler->owner != context) {
            goto state_error;
        }
        mip_count = (image_binding->flags &
                     RIN_GPU_GRAPHICS_BINDING_SAMPLED_MIP_CHAIN) != 0u
            ? image->descriptor.mip_levels - image_binding->mip_level : 1u;
        if (mip_count == 0u ||
            mip_count > RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS) {
            goto unsupported;
        }
        for (uint32_t mip = 0u; mip < mip_count; ++mip) {
            int snapshot_result = backend_snapshot_sampled_mip(
                image, image_binding->mip_level + mip,
                &group->texels[sampled_index][mip],
                &group->texel_counts[sampled_index][mip],
                &group->widths[sampled_index][mip],
                &group->heights[sampled_index][mip]);
            if (snapshot_result == RIN_GPU_ERROR_NO_MEMORY)
                goto no_memory;
            if (snapshot_result == RIN_GPU_ERROR_BOUNDS)
                goto unsupported;
            if (snapshot_result != RIN_GPU_OK)
                goto state_error;
        }
        group->sampled_images[sampled_index] = image;
        group->mip_counts[sampled_index] = mip_count;
        group->samplers[sampled_index].base.struct_size =
            sizeof(group->samplers[sampled_index]);
        group->samplers[sampled_index].base.version = RIN_WEBGL_SOFTWARE_VERSION;
        group->samplers[sampled_index].base.min_filter = sampler->descriptor.min_filter;
        group->samplers[sampled_index].base.mag_filter = sampler->descriptor.mag_filter;
        group->samplers[sampled_index].base.mip_filter = sampler->descriptor.mip_filter;
        group->samplers[sampled_index].base.address_u = sampler->descriptor.address_u;
        group->samplers[sampled_index].base.address_v = sampler->descriptor.address_v;
        group->samplers[sampled_index].mip_lod_bias = sampler->descriptor.mip_lod_bias;
        group->samplers[sampled_index].min_lod = sampler->descriptor.min_lod;
        group->samplers[sampled_index].max_lod = sampler->descriptor.max_lod;
    }
    *cookie = (uint64_t)(uintptr_t)group;
    return RIN_GPU_OK;

no_memory:
    backend_release_bind_group_texels(group);
    free(group);
    return RIN_GPU_ERROR_NO_MEMORY;
unsupported:
    backend_release_bind_group_texels(group);
    free(group);
    return RIN_GPU_ERROR_UNSUPPORTED;
state_error:
    backend_release_bind_group_texels(group);
    free(group);
    return RIN_GPU_ERROR_STATE;
invalid_argument:
    backend_release_bind_group_texels(group);
    free(group);
    return RIN_GPU_ERROR_INVALID_ARGUMENT;
}

static void backend_destroy_graphics_bind_group(void* opaque, uint64_t cookie)
{
    RinGLAquamarineSurfaceBindGroup* group =
        (RinGLAquamarineSurfaceBindGroup*)(uintptr_t)cookie;
    (void)opaque;
    if (!group) return;
    backend_release_bind_group_texels(group);
    free(group);
}

static uint8_t clear_component(float component)
{
    return (uint8_t)(component * 255.0f + 0.5f);
}

static int image_matches(const RinGLAquamarineSurfaceContext* context,
                         uint64_t cookie, uint32_t expected_format)
{
    const RinGLAquamarineSurfaceImage* image =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    return image && image->owner == context &&
           image->descriptor.format == expected_format;
}

static int image_has_depth_aspect(const RinGLAquamarineSurfaceImage* image)
{
    return image && (image->descriptor.format == RIN_GPU_FORMAT_D32_FLOAT ||
                     image->descriptor.format ==
                         RIN_GPU_FORMAT_D32_FLOAT_S8_UINT);
}

static int image_has_stencil_aspect(const RinGLAquamarineSurfaceImage* image)
{
    return image && (image->descriptor.format == RIN_GPU_FORMAT_S8_UINT ||
                     image->descriptor.format ==
                         RIN_GPU_FORMAT_D32_FLOAT_S8_UINT);
}

static int image_is_depth_target(const RinGLAquamarineSurfaceContext* context,
                                 uint64_t cookie)
{
    const RinGLAquamarineSurfaceImage* image =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;

    return image && image->owner == context &&
           (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH ||
            image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL ||
            image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH ||
            image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL ||
            image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_STENCIL);
}

static int image_depth_storage_at_mip(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfaceImage* image, uint32_t mip_level,
    float** depth_out, uint32_t* pitch_out)
{
    if (!context || !image || !depth_out || !pitch_out ||
        image->owner != context ||
        !image_is_depth_target(context, (uint64_t)(uintptr_t)image) ||
        (image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT &&
         image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) ||
        mip_level >= image->descriptor.mip_levels) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH ||
        image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
        if (mip_level != 0u || !context->target.depth)
            return RIN_GPU_ERROR_BACKEND;
        *depth_out = context->target.depth;
        *pitch_out = context->target.depth_pitch_floats;
    } else if (image->target_kind ==
               RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL) {
        uint8_t* stencil;

        if (offscreen_depth_stencil_storage_at_mip(image, mip_level,
                                                   depth_out, &stencil,
                                                   pitch_out) != RIN_GPU_OK) {
            return RIN_GPU_ERROR_BACKEND;
        }
    } else {
        uint8_t* bytes;
        uint64_t pitch;
        uint32_t width;
        uint32_t height;

        if (offscreen_image_storage(image, mip_level, &bytes, &pitch, &width,
                                    &height) != RIN_GPU_OK ||
            pitch / sizeof(float) > UINT32_MAX) {
            return RIN_GPU_ERROR_BACKEND;
        }
        *depth_out = (float*)(void*)bytes;
        *pitch_out = (uint32_t)(pitch / sizeof(float));
    }
    {
        uint32_t width;
        uint32_t height;

        return backend_image_mip_dimensions(&image->descriptor, mip_level,
                                            &width, &height) &&
                       *pitch_out >= width
                   ? RIN_GPU_OK
                   : RIN_GPU_ERROR_BACKEND;
    }
}

static int image_stencil_storage_at_mip(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfaceImage* image, uint32_t mip_level,
    uint8_t** stencil_out, uint32_t* pitch_out)
{

    if (!context || !image || !stencil_out || !pitch_out ||
        image->owner != context ||
        (image->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
         image->descriptor.format != RIN_GPU_FORMAT_S8_UINT) ||
        mip_level >= image->descriptor.mip_levels) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_STENCIL) {
        uint8_t* bytes;
        uint64_t pitch;
        uint32_t width;
        uint32_t height;

        if (offscreen_image_storage(image, mip_level, &bytes, &pitch, &width,
                                    &height) != RIN_GPU_OK ||
            pitch > UINT32_MAX)
            return RIN_GPU_ERROR_BACKEND;
        *stencil_out = bytes;
        *pitch_out = (uint32_t)pitch;
        return *pitch_out >= width ? RIN_GPU_OK : RIN_GPU_ERROR_BACKEND;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_DEPTH_STENCIL) {
        if (mip_level != 0u || !context->target.stencil ||
            context->target.stencil_pitch_bytes < image->descriptor.width) {
            return RIN_GPU_ERROR_BACKEND;
        }
        *stencil_out = context->target.stencil;
        *pitch_out = context->target.stencil_pitch_bytes;
        return RIN_GPU_OK;
    }
    if (image->target_kind !=
        RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_DEPTH_STENCIL) {
        return RIN_GPU_ERROR_BACKEND;
    }
    {
        float* depth;

        return offscreen_depth_stencil_storage_at_mip(image, mip_level,
                                                       &depth, stencil_out,
                                                       pitch_out);
    }
}

static int image_is_color_target(const RinGLAquamarineSurfaceContext* context,
                                 uint64_t cookie)
{
    const RinGLAquamarineSurfaceImage* image =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;

    return image && image->owner == context &&
           (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR ||
            image->target_kind ==
                RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR);
}

static int clear_region_bounds_for_extent(const RinGpuClearRegionV1* region,
                                          uint32_t width, uint32_t height,
                                          uint32_t* x0_out,
                                          uint32_t* y0_out,
                                          uint32_t* x1_out,
                                          uint32_t* y1_out)
{
    uint32_t x;
    uint32_t y;

    if (!region || width == 0u || height == 0u || !x0_out || !y0_out ||
        !x1_out || !y1_out ||
        region->enabled > 1u || region->reserved != 0u)
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    if (region->enabled == 0u) {
        if (region->x != 0 || region->y != 0 || region->width != 0u ||
            region->height != 0u)
            return RIN_GPU_ERROR_INVALID_ARGUMENT;
        *x0_out = 0u;
        *y0_out = 0u;
        *x1_out = width;
        *y1_out = height;
        return RIN_GPU_OK;
    }
    if (region->x < 0 || region->y < 0)
        return RIN_GPU_ERROR_BOUNDS;
    x = (uint32_t)region->x;
    y = (uint32_t)region->y;
    if (x > width || region->width > width - x || y > height ||
        region->height > height - y)
        return RIN_GPU_ERROR_BOUNDS;
    *x0_out = x;
    *x1_out = x + region->width;
    /* WebGL scissor coordinates are lower-left; backing rows are top-left. */
    *y0_out = height - (y + region->height);
    *y1_out = height - y;
    return RIN_GPU_OK;
}

static int clear_color_target(RinGLAquamarineSurfaceContext* context,
                              float red, float green, float blue,
                              float alpha, uint32_t color_write_mask,
                              const RinGpuClearRegionV1* region,
                              uint64_t cookie, uint32_t mip_level)
{
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    AqColor color = {
        .r = clear_component(red),
        .g = clear_component(green),
        .b = clear_component(blue),
        .a = clear_component(alpha),
    };
    uint8_t* pixels;
    uint64_t pitch_bytes;
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t y;
    int bgra;
    int packed_color;
    uint32_t width;
    uint32_t height;

    if (!image || image->owner != context ||
        (color_write_mask & ~RIN_GPU_COLOR_WRITE_ALL) != 0u ||
        !backend_image_mip_dimensions(&image->descriptor, mip_level, &width,
                                      &height) ||
        clear_region_bounds_for_extent(region, width, height, &x0, &y0, &x1,
                                       &y1) != RIN_GPU_OK)
        return RIN_GPU_ERROR_BACKEND;
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR &&
        mip_level == 0u && color_write_mask == RIN_GPU_COLOR_WRITE_ALL &&
        region->enabled == 0u) {
        /* The RinGPU command stream owns ordering; Aquamarine owns BGRA stores. */
        aq_surface_clear(&context->aquamarine_surface, color);
        return RIN_GPU_OK;
    }
    if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR) {
        if (mip_level != 0u)
            return RIN_GPU_ERROR_BACKEND;
        pixels = context->target.pixels;
        pitch_bytes = context->target.pitch_bytes;
        bgra = 1;
    } else if (image->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_OFFSCREEN_COLOR &&
               offscreen_image_storage(image, mip_level, &pixels,
                                       &pitch_bytes, &width, &height) ==
                   RIN_GPU_OK) {
        bgra = 0;
    } else {
        return RIN_GPU_ERROR_BACKEND;
    }
    packed_color = backend_packed_color_format(image->descriptor.format);
    for (y = y0; y < y1; ++y) {
        uint8_t* row = pixels + (uint64_t)y * pitch_bytes;
        uint32_t x;
        for (x = x0; x < x1; ++x) {
            uint8_t* pixel = row + (uint64_t)x *
                (packed_color ? sizeof(uint16_t) : sizeof(uint32_t));
            if (packed_color) {
                AqColor stored;
                uint16_t packed;

                backend_unpack_packed_color(image->descriptor.format, pixel,
                                            &stored);
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_RED) != 0u)
                    stored.r = color.r;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_GREEN) != 0u)
                    stored.g = color.g;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_BLUE) != 0u)
                    stored.b = color.b;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_ALPHA) != 0u)
                    stored.a = color.a;
                packed = backend_pack_packed_color(image->descriptor.format,
                                                    stored.r, stored.g, stored.b,
                                                    stored.a);
                memcpy(pixel, &packed, sizeof(packed));
                continue;
            }
            if (bgra) {
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_BLUE) != 0u)
                    pixel[0] = color.b;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_GREEN) != 0u)
                    pixel[1] = color.g;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_RED) != 0u)
                    pixel[2] = color.r;
            } else {
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_RED) != 0u)
                    pixel[0] = color.r;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_GREEN) != 0u)
                    pixel[1] = color.g;
                if ((color_write_mask & RIN_GPU_COLOR_WRITE_BLUE) != 0u)
                    pixel[2] = color.b;
            }
            if ((color_write_mask & RIN_GPU_COLOR_WRITE_ALPHA) != 0u)
                pixel[3] = color.a;
        }
    }
    return RIN_GPU_OK;
}

static int clear_depth_target(RinGLAquamarineSurfaceContext* context,
                              float depth, const RinGpuClearRegionV1* region,
                              uint64_t cookie, uint32_t mip_level)
{
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    float* storage;
    uint32_t pitch;
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t y;
    uint32_t width;
    uint32_t height;

    if (image_depth_storage_at_mip(context, image, mip_level, &storage,
                                   &pitch) != RIN_GPU_OK ||
        !backend_image_mip_dimensions(&image->descriptor, mip_level, &width,
                                      &height) ||
        clear_region_bounds_for_extent(region, width, height, &x0, &y0, &x1,
                                       &y1) != RIN_GPU_OK)
        return RIN_GPU_ERROR_BACKEND;
    for (y = y0; y < y1; ++y) {
        float* row = storage + (size_t)y * pitch;
        uint32_t x;
        for (x = x0; x < x1; ++x) row[x] = depth;
    }
    return RIN_GPU_OK;
}

static int clear_stencil_target(RinGLAquamarineSurfaceContext* context,
                                uint32_t stencil, uint32_t write_mask,
                                const RinGpuClearRegionV1* region,
                                uint64_t cookie, uint32_t mip_level)
{
    RinGLAquamarineSurfaceImage* image =
        (RinGLAquamarineSurfaceImage*)(uintptr_t)cookie;
    uint8_t* storage;
    uint32_t pitch;
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;
    uint32_t width;
    uint32_t height;

    if (stencil > 0xffu || write_mask > 0xffu ||
        image_stencil_storage_at_mip(context, image, mip_level, &storage,
                                     &pitch) != RIN_GPU_OK ||
        !backend_image_mip_dimensions(&image->descriptor, mip_level, &width,
                                      &height) ||
        clear_region_bounds_for_extent(region, width, height, &x0, &y0, &x1,
                                       &y1) != RIN_GPU_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    for (uint32_t y = y0; y < y1; ++y) {
        uint8_t* row = storage + (size_t)y * pitch;
        for (uint32_t x = x0; x < x1; ++x) {
            row[x] = (uint8_t)((row[x] & ~write_mask) |
                               (stencil & write_mask));
        }
    }
    return RIN_GPU_OK;
}

static int backend_prepare_software_context(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfacePipeline* pipeline,
    const RinGLAquamarineSurfaceImage* color_target,
    uint32_t color_mip_level,
    const RinGLAquamarineSurfaceImage* depth_target,
    uint32_t depth_mip_level,
    const RinGLAquamarineSurfaceImage* stencil_target,
    uint32_t stencil_mip_level,
    const RinGLAquamarineSurfaceRasterState* raster_state)
{
    RinWebGLSoftwareTargetV1 target;
    uint32_t color_width;
    uint32_t color_height;
    uint32_t depth_width;
    uint32_t depth_height;
    uint32_t stencil_width;
    uint32_t stencil_height;

    if (!context || !pipeline || pipeline->owner != context ||
        !color_target || color_target->owner != context ||
        !raster_state ||
        !image_is_color_target(context, (uint64_t)(uintptr_t)color_target) ||
        !backend_image_mip_dimensions(&color_target->descriptor,
                                      color_mip_level, &color_width,
                                      &color_height) ||
        (pipeline->depth_enabled != 0u &&
         (!depth_target || depth_target->owner != context ||
          !backend_image_mip_dimensions(&depth_target->descriptor,
                                        depth_mip_level, &depth_width,
                                        &depth_height) ||
          depth_width != color_width || depth_height != color_height ||
          (depth_target->descriptor.format != RIN_GPU_FORMAT_D32_FLOAT &&
           depth_target->descriptor.format !=
               RIN_GPU_FORMAT_D32_FLOAT_S8_UINT))) ||
        (pipeline->stencil_enabled != 0u &&
         (!stencil_target || stencil_target->owner != context ||
          !backend_image_mip_dimensions(&stencil_target->descriptor,
                                        stencil_mip_level, &stencil_width,
                                        &stencil_height) ||
          stencil_width != color_width || stencil_height != color_height ||
          (stencil_target->descriptor.format !=
               RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
           stencil_target->descriptor.format != RIN_GPU_FORMAT_S8_UINT)))) {
        return RIN_GPU_ERROR_BACKEND;
    }
    memset(&target, 0, sizeof(target));
    target.struct_size = sizeof(target);
    target.version = RIN_WEBGL_SOFTWARE_VERSION;
    if (color_target->target_kind == RINGL_AQUAMARINE_SURFACE_IMAGE_COLOR) {
        if (color_mip_level != 0u)
            return RIN_GPU_ERROR_BACKEND;
        target.pixels = context->target.pixels;
        target.pitch_bytes = context->target.pitch_bytes;
        target.pixel_format = RIN_WEBGL_SOFTWARE_PIXEL_FORMAT_BGRA8_UNORM;
    } else {
        uint8_t* pixels;
        uint64_t pitch_bytes;

        if (offscreen_image_storage(color_target, color_mip_level, &pixels,
                                    &pitch_bytes, &color_width,
                                    &color_height) != RIN_GPU_OK ||
            pitch_bytes > UINT32_MAX) {
            return RIN_GPU_ERROR_BACKEND;
        }
        target.pixels = pixels;
        target.pitch_bytes = (uint32_t)pitch_bytes;
        switch (color_target->descriptor.format) {
        case RIN_GPU_FORMAT_RGB565_UNORM:
            target.pixel_format = RIN_WEBGL_SOFTWARE_PIXEL_FORMAT_RGB565_UNORM;
            break;
        case RIN_GPU_FORMAT_RGBA4_UNORM:
            target.pixel_format = RIN_WEBGL_SOFTWARE_PIXEL_FORMAT_RGBA4_UNORM;
            break;
        case RIN_GPU_FORMAT_RGB5_A1_UNORM:
            target.pixel_format = RIN_WEBGL_SOFTWARE_PIXEL_FORMAT_RGB5_A1_UNORM;
            break;
        default:
            target.pixel_format = RIN_WEBGL_SOFTWARE_PIXEL_FORMAT_RGBA8_UNORM;
            break;
        }
    }
    target.width = color_width;
    target.height = color_height;
    if (pipeline->depth_enabled != 0u) {
        float* depth_storage;
        uint32_t depth_pitch;

        if (image_depth_storage_at_mip(context, depth_target, depth_mip_level,
                                       &depth_storage, &depth_pitch) !=
            RIN_GPU_OK) {
            return RIN_GPU_ERROR_BACKEND;
        }
        target.flags |= RIN_WEBGL_SOFTWARE_DEPTH_TEST;
        target.depth = depth_storage;
        target.depth_pitch_floats = depth_pitch;
    }
    if (pipeline->stencil_enabled != 0u) {
        uint8_t* stencil_storage;
        uint32_t stencil_pitch;

        if (image_stencil_storage_at_mip(context, stencil_target,
                                         stencil_mip_level, &stencil_storage,
                                         &stencil_pitch) != RIN_GPU_OK ||
            stencil_storage == NULL || stencil_pitch < target.width) {
            return RIN_GPU_ERROR_BACKEND;
        }
        target.flags |= RIN_WEBGL_SOFTWARE_STENCIL_TEST;
        target.stencil = stencil_storage;
        target.stencil_pitch_bytes = stencil_pitch;
    }
    if (rin_webgl_software_init(&context->software_context, &target) !=
        RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (rin_webgl_software_set_face_state(
            &context->software_context, pipeline->cull_mode,
            pipeline->front_face) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (rin_webgl_software_set_blend_state(
            &context->software_context, pipeline->blend_enabled,
            pipeline->source_color_factor, pipeline->destination_color_factor,
            pipeline->color_operation, pipeline->source_alpha_factor,
            pipeline->destination_alpha_factor, pipeline->alpha_operation,
            pipeline->blend_constant_red, pipeline->blend_constant_green,
            pipeline->blend_constant_blue, pipeline->blend_constant_alpha,
            pipeline->color_write_mask) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (pipeline->depth_enabled != 0u &&
        rin_webgl_software_set_depth_state(
            &context->software_context, pipeline->depth_compare,
            pipeline->depth_write_enabled) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (rin_webgl_software_set_polygon_offset(
            &context->software_context,
            raster_state->polygon_offset_fill_enabled,
            raster_state->polygon_offset_factor,
            raster_state->polygon_offset_units) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (rin_webgl_software_set_line_width(&context->software_context,
                                          raster_state->line_width) !=
        RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (rin_webgl_software_set_sample_coverage(
            &context->software_context, raster_state->sample_coverage_enabled,
            raster_state->sample_coverage_value,
            raster_state->sample_coverage_invert) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (pipeline->stencil_enabled != 0u &&
        rin_webgl_software_set_stencil_state_separate(
            &context->software_context, pipeline->stencil_compare,
            pipeline->stencil_reference, pipeline->stencil_read_mask,
            pipeline->stencil_write_mask, pipeline->stencil_fail_operation,
            pipeline->stencil_depth_fail_operation,
            pipeline->stencil_pass_operation,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_compare : pipeline->stencil_compare,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_reference : pipeline->stencil_reference,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_read_mask : pipeline->stencil_read_mask,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_write_mask : pipeline->stencil_write_mask,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_fail_operation
                : pipeline->stencil_fail_operation,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_depth_fail_operation
                : pipeline->stencil_depth_fail_operation,
            pipeline->separate_stencil_enabled != 0u
                ? pipeline->back_stencil_pass_operation
                : pipeline->stencil_pass_operation) != RIN_WEBGL_SOFTWARE_OK) {
        return RIN_GPU_ERROR_BACKEND;
    }
    return RIN_GPU_OK;
}

static int raster_float_to_integer(float value, int32_t* result)
{
    if (!result || value != value ||
        value < -(float)RIN_WEBGL_SOFTWARE_MAX_DIMENSION ||
        value > (float)RIN_WEBGL_SOFTWARE_MAX_DIMENSION ||
        value != (float)(int32_t)value) {
        return 0;
    }
    *result = (int32_t)value;
    return 1;
}

static void backend_default_raster_state(
    const RinGLAquamarineSurfaceImage* color_target,
    uint32_t mip_level,
    RinGLAquamarineSurfaceRasterState* state)
{
    uint32_t width;
    uint32_t height;

    if (!color_target || !state ||
        !backend_image_mip_dimensions(&color_target->descriptor, mip_level,
                                      &width, &height)) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->viewport.base.struct_size = sizeof(state->viewport.base);
    state->viewport.base.version = RIN_WEBGL_SOFTWARE_VERSION;
    state->viewport.base.width = width;
    state->viewport.base.height = height;
    state->viewport.min_depth = 0.0f;
    state->viewport.max_depth = 1.0f;
    state->scissor.struct_size = sizeof(state->scissor);
    state->scissor.version = RIN_WEBGL_SOFTWARE_VERSION;
    state->line_width = 1.0f;
    state->sample_coverage_value = 1.0f;
}

/* The software rasterizer has an integer-pixel viewport, but its V2
 * descriptor applies the exact finite [0, 1] depth range supplied by RinGPU. */
static int backend_raster_state_from_command(
    const RinGpuBackendRasterStateV1* source,
    RinGLAquamarineSurfaceRasterState* destination)
{
    int32_t width;
    int32_t height;

    if (!source || !destination || source->scissor_enabled > 1u ||
        source->polygon_offset_fill_enabled > 1u ||
        source->sample_coverage_enabled > 1u ||
        source->sample_coverage_invert > 1u ||
        source->min_depth < 0.0f || source->min_depth > 1.0f ||
        source->max_depth < 0.0f || source->max_depth > 1.0f ||
        source->polygon_offset_factor != source->polygon_offset_factor ||
        source->polygon_offset_units != source->polygon_offset_units ||
        source->polygon_offset_factor > 3.402823466e+38f ||
        source->polygon_offset_factor < -3.402823466e+38f ||
        source->polygon_offset_units > 3.402823466e+38f ||
        source->polygon_offset_units < -3.402823466e+38f ||
        source->line_width != source->line_width ||
        source->line_width < 1.0f || source->line_width > 64.0f ||
        source->sample_coverage_value != source->sample_coverage_value ||
        source->sample_coverage_value < 0.0f ||
        source->sample_coverage_value > 1.0f ||
        !raster_float_to_integer(source->viewport_x,
                                 &destination->viewport.base.x) ||
        !raster_float_to_integer(source->viewport_y,
                                 &destination->viewport.base.y) ||
        !raster_float_to_integer(source->viewport_width, &width) ||
        !raster_float_to_integer(source->viewport_height, &height) ||
        width <= 0 || height <= 0) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    destination->viewport.base.struct_size = sizeof(destination->viewport);
    destination->viewport.base.version = RIN_WEBGL_SOFTWARE_VERSION;
    destination->viewport.base.width = (uint32_t)width;
    destination->viewport.base.height = (uint32_t)height;
    destination->viewport.min_depth = source->min_depth;
    destination->viewport.max_depth = source->max_depth;
    destination->scissor_enabled = source->scissor_enabled;
    destination->polygon_offset_fill_enabled =
        source->polygon_offset_fill_enabled;
    destination->polygon_offset_factor = source->polygon_offset_factor;
    destination->polygon_offset_units = source->polygon_offset_units;
    destination->line_width = source->line_width;
    destination->sample_coverage_enabled = source->sample_coverage_enabled;
    destination->sample_coverage_value = source->sample_coverage_value;
    destination->sample_coverage_invert = source->sample_coverage_invert;
    if (source->scissor_enabled != 0u) {
        destination->scissor.x = source->scissor_x;
        destination->scissor.y = source->scissor_y;
        destination->scissor.width = source->scissor_width;
        destination->scissor.height = source->scissor_height;
    }
    return RIN_GPU_OK;
}

static int backend_bind_group_valid(
    const RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfacePipeline* pipeline,
    const RinGLAquamarineSurfaceBindGroup* group)
{
    uint32_t sampled_index;

    if (!context || !pipeline || !group || group->owner != context ||
        group->pipeline != pipeline ||
        group->sampled_image_count >
            RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES ||
        group->sampled_image_count != pipeline->resource_count / 2u) {
        return 0;
    }
    for (sampled_index = 0u; sampled_index < group->sampled_image_count;
         ++sampled_index) {
        if (!group->sampled_images[sampled_index] ||
            group->mip_counts[sampled_index] == 0u ||
            group->mip_counts[sampled_index] >
                RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS) {
            return 0;
        }
        for (uint32_t mip = 0u; mip < group->mip_counts[sampled_index]; ++mip) {
            if (!group->texels[sampled_index][mip] ||
                group->texel_counts[sampled_index][mip] == 0u ||
                group->widths[sampled_index][mip] == 0u ||
                group->heights[sampled_index][mip] == 0u) {
                return 0;
            }
        }
    }
    return 1;
}

static int backend_draw_vertices_valid(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawVerticesV1* draw,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass)
{
    const RinGLAquamarineSurfacePipeline* pipeline;
    const RinGLAquamarineSurfaceImage* color_target;
    const RinGLAquamarineSurfaceBuffer* vertex_buffer;
    const RinGLAquamarineSurfaceBindGroup* bind_group;
    uint64_t vertex_bytes;
    uint64_t first_byte;

    if (!context || !draw || !active_pass ||
        active_pass->color_target_cookie == 0u || !draw->pipeline_cookie ||
        !draw->color_target_cookie ||
        draw->mip_level != active_pass->color_mip_level ||
        draw->array_layer != 0u ||
        draw->vertex_count == 0u ||
        draw->vertex_count > RIN_WEBGL_SOFTWARE_MAX_VERTICES ||
        draw->instance_count != 1u || draw->first_instance != 0u) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    pipeline = (const RinGLAquamarineSurfacePipeline*)(uintptr_t)
        draw->pipeline_cookie;
    color_target = (const RinGLAquamarineSurfaceImage*)(uintptr_t)
        draw->color_target_cookie;
    vertex_buffer = (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
        draw->vertex_buffer_cookie;
    bind_group = (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
        draw->bind_group_cookie;
    if (!pipeline || pipeline->owner != context || !pipeline->vertex_shader ||
        !pipeline->fragment_shader || !color_target ||
        color_target->owner != context ||
        !image_is_color_target(context, draw->color_target_cookie) ||
        draw->color_target_cookie != active_pass->color_target_cookie ||
        (pipeline->depth_enabled != 0u &&
         active_pass->depth_target_cookie == 0u) ||
        (pipeline->stencil_enabled != 0u &&
         active_pass->stencil_target_cookie == 0u) ||
        draw->vertex_stride != pipeline->vertex_stride ||
        pipeline->input_count == 0u ||
        (pipeline->vertex_stride == 0u
             ? (draw->vertex_buffer_cookie != 0u || draw->vertex_offset != 0u)
             : (!vertex_buffer || vertex_buffer->owner != context ||
                !vertex_buffer->bytes)) ||
        (pipeline->resource_count == 0u
             ? draw->bind_group_cookie != 0u
             : !backend_bind_group_valid(context, pipeline, bind_group))) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (pipeline->vertex_stride != 0u) {
        vertex_bytes = (uint64_t)draw->vertex_count * pipeline->vertex_stride;
        first_byte = draw->vertex_offset +
            (uint64_t)draw->first_vertex * pipeline->vertex_stride;
        if (draw->vertex_offset > vertex_buffer->size_bytes ||
            first_byte < draw->vertex_offset ||
            first_byte > vertex_buffer->size_bytes ||
            vertex_bytes > vertex_buffer->size_bytes - first_byte) {
            return RIN_GPU_ERROR_BOUNDS;
        }
    }
    return RIN_GPU_OK;
}

static int backend_vertex_bindings_valid(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfacePipeline* pipeline,
    const RinGpuBackendVertexBufferBindingV1* bindings,
    uint32_t binding_count, uint32_t first_vertex, uint32_t vertex_count)
{
    uint64_t vertex_end = (uint64_t)first_vertex + vertex_count;

    if (!context || !pipeline ||
        binding_count != pipeline->vertex_binding_count ||
        (binding_count != 0u && bindings == NULL)) {
        return RIN_GPU_ERROR_BACKEND;
    }
    for (uint32_t binding = 0u; binding < binding_count; ++binding) {
        const RinGpuBackendVertexBufferBindingV1* source = &bindings[binding];
        const RinGpuVertexBufferLayoutV1* layout =
            &pipeline->vertex_bindings[binding];
        const RinGLAquamarineSurfaceBuffer* buffer =
            (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
                source->buffer_cookie;
        uint64_t required_bytes;

        if (source->binding != binding || source->reserved != 0u ||
            !buffer || buffer->owner != context || !buffer->bytes ||
            layout->binding != binding || layout->stride == 0u ||
            !backend_multiply_u64(vertex_end, layout->stride, &required_bytes) ||
            source->offset > buffer->size_bytes ||
            required_bytes > buffer->size_bytes - source->offset) {
            return RIN_GPU_ERROR_BOUNDS;
        }
    }
    return RIN_GPU_OK;
}

static int backend_draw_vertices_valid_v2(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawVerticesV2* draw,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass)
{
    const RinGLAquamarineSurfacePipeline* pipeline;
    const RinGLAquamarineSurfaceImage* color_target;
    const RinGLAquamarineSurfaceBindGroup* bind_group;

    if (!context || !draw || !active_pass ||
        active_pass->color_target_cookie == 0u || !draw->pipeline_cookie ||
        !draw->color_target_cookie ||
        draw->mip_level != active_pass->color_mip_level ||
        draw->array_layer != 0u || draw->vertex_count == 0u ||
        draw->vertex_count > RIN_WEBGL_SOFTWARE_MAX_VERTICES ||
        draw->instance_count != 1u || draw->first_instance != 0u ||
        draw->vertex_binding_count > RIN_GPU_MAX_VERTEX_BUFFER_BINDINGS ||
        draw->reserved != 0u) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    pipeline = (const RinGLAquamarineSurfacePipeline*)(uintptr_t)
        draw->pipeline_cookie;
    color_target = (const RinGLAquamarineSurfaceImage*)(uintptr_t)
        draw->color_target_cookie;
    bind_group = (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
        draw->bind_group_cookie;
    if (!pipeline || pipeline->owner != context || !pipeline->vertex_shader ||
        !pipeline->fragment_shader || !color_target ||
        color_target->owner != context ||
        !image_is_color_target(context, draw->color_target_cookie) ||
        draw->color_target_cookie != active_pass->color_target_cookie ||
        (pipeline->depth_enabled != 0u &&
         active_pass->depth_target_cookie == 0u) ||
        (pipeline->stencil_enabled != 0u &&
         active_pass->stencil_target_cookie == 0u) ||
        pipeline->input_count == 0u ||
        (pipeline->resource_count == 0u
             ? draw->bind_group_cookie != 0u
             : !backend_bind_group_valid(context, pipeline, bind_group))) {
        return RIN_GPU_ERROR_BACKEND;
    }
    return backend_vertex_bindings_valid(
        context, pipeline, draw->vertex_buffers, draw->vertex_binding_count,
        draw->first_vertex, draw->vertex_count);
}

static int backend_execute_software_draw(
    RinGLAquamarineSurfaceContext* context,
    const RinGLAquamarineSurfacePipeline* pipeline,
    const RinGLAquamarineSurfaceImage* color_target,
    uint32_t color_mip_level,
    const RinGLAquamarineSurfaceImage* depth_target,
    uint32_t depth_mip_level,
    const RinGLAquamarineSurfaceImage* stencil_target,
    uint32_t stencil_mip_level,
    const RinWebGLSoftwareVertexBufferV1* vertices,
    const RinWebGLSoftwareIndexBufferV1* indices,
    const RinGLAquamarineSurfaceRasterState* raster_state,
    const RinGLAquamarineSurfaceBindGroup* bind_group)
{
    RinWebGLSoftwareProgramV1 program;
    RinWebGLSoftwareDrawStateV1 state;
    RinWebGLSoftwareVertexBufferV1 line_vertices;
    RinWebGLSoftwareIndexBufferV1 line_indices;
    RinWebGLSoftwareSampledImage2DRGBAF32MipV2
        sampled_images[RINGL_AQUAMARINE_SURFACE_MAX_SAMPLED_IMAGES];
    RinWebGLSoftwareResourceBindingV1 bindings[RIN_SHADER_MAX_RESOURCES];
    RinWebGLSoftwareResourceBindingsV1 resources;
    RinWebGLSoftwareResourceDrawStateV1 resource_state;
    int result;

    result = backend_prepare_software_context(
        context, pipeline, color_target, color_mip_level, depth_target,
        depth_mip_level, stencil_target, stencil_mip_level, raster_state);
    if (result != RIN_GPU_OK) return result;
    memset(&program, 0, sizeof(program));
    program.struct_size = sizeof(program);
    program.version = RIN_WEBGL_SOFTWARE_VERSION;
    program.vertex_shader = pipeline->vertex_shader->bytes;
    program.vertex_shader_size = pipeline->vertex_shader->size_bytes;
    program.fragment_shader = pipeline->fragment_shader->bytes;
    program.fragment_shader_size = pipeline->fragment_shader->size_bytes;
    memset(&state, 0, sizeof(state));
    state.struct_size = sizeof(state);
    state.version = RIN_WEBGL_SOFTWARE_VERSION;
    state.primitive = backend_software_primitive(pipeline->primitive_topology);
    if (state.primitive == UINT32_MAX)
        return RIN_GPU_ERROR_BACKEND;
    if (pipeline->primitive_topology == RIN_GPU_PRIMITIVE_LINE_LIST) {
        if (indices != NULL) {
            if (indices->index_count < 2u)
                return RIN_GPU_OK;
            line_indices = *indices;
            line_indices.index_count &= ~1u;
            line_indices.byte_length =
                line_indices.index_count * sizeof(*line_indices.values);
            indices = &line_indices;
        } else {
            if (vertices->vertex_count < 2u)
                return RIN_GPU_OK;
            line_vertices = *vertices;
            line_vertices.vertex_count &= ~1u;
            line_vertices.byte_length =
                (vertices->byte_length / vertices->vertex_count) *
                line_vertices.vertex_count;
            vertices = &line_vertices;
        }
    }
    if (pipeline->primitive_topology == RIN_GPU_PRIMITIVE_LINE_STRIP ||
        pipeline->primitive_topology == RIN_GPU_PRIMITIVE_LINE_LOOP) {
        if ((indices != NULL && indices->index_count < 2u) ||
            (indices == NULL && vertices->vertex_count < 2u)) {
            return RIN_GPU_OK;
        }
    }
    if (pipeline->primitive_topology == RIN_GPU_PRIMITIVE_TRIANGLE_STRIP ||
        pipeline->primitive_topology == RIN_GPU_PRIMITIVE_TRIANGLE_FAN) {
        if ((indices != NULL && indices->index_count < 3u) ||
            (indices == NULL && vertices->vertex_count < 3u)) {
            return RIN_GPU_OK;
        }
    }
    state.program = &program;
    state.vertices = vertices;
    state.indices = indices;
    state.viewport = &raster_state->viewport.base;
    if (raster_state->scissor_enabled != 0u) {
        state.flags = RIN_WEBGL_SOFTWARE_DRAW_SCISSOR;
        state.scissor = &raster_state->scissor;
    }
    if (pipeline->resource_count == 0u) {
        result = rin_webgl_software_draw_state_v1(&context->software_context,
                                                  &state);
    } else if (!backend_bind_group_valid(context, pipeline, bind_group)) {
        return RIN_GPU_ERROR_BACKEND;
    } else {
        uint32_t sampled_index;

        memset(sampled_images, 0, sizeof(sampled_images));
        memset(bindings, 0, sizeof(bindings));
        for (sampled_index = 0u;
             sampled_index < bind_group->sampled_image_count;
             ++sampled_index) {
            RinWebGLSoftwareSampledImage2DRGBAF32MipV2* sampled_image =
                &sampled_images[sampled_index];
            RinWebGLSoftwareResourceBindingV1* image_binding =
                &bindings[sampled_index * 2u];
            RinWebGLSoftwareResourceBindingV1* sampler_binding =
                &bindings[sampled_index * 2u + 1u];

            sampled_image->struct_size = sizeof(*sampled_image);
            sampled_image->version = RIN_WEBGL_SOFTWARE_VERSION;
            sampled_image->mip_level_count = bind_group->mip_counts[sampled_index];
            for (uint32_t mip = 0u; mip < sampled_image->mip_level_count; ++mip) {
                sampled_image->texels[mip] = bind_group->texels[sampled_index][mip];
                sampled_image->widths[mip] = bind_group->widths[sampled_index][mip];
                sampled_image->heights[mip] = bind_group->heights[sampled_index][mip];
                sampled_image->row_pitch_texels[mip] = sampled_image->widths[mip];
                sampled_image->byte_lengths[mip] =
                    bind_group->texel_counts[sampled_index][mip] * 4u *
                    (uint32_t)sizeof(*bind_group->texels[sampled_index][mip]);
            }
            image_binding->struct_size = sizeof(*image_binding);
            image_binding->version = RIN_WEBGL_SOFTWARE_VERSION;
            image_binding->resource = sampled_index * 2u;
            image_binding->kind =
                RIN_WEBGL_SOFTWARE_RESOURCE_SAMPLED_IMAGE_2D_RGBA_F32_MIP;
            image_binding->object = sampled_image;
            sampler_binding->struct_size = sizeof(*sampler_binding);
            sampler_binding->version = RIN_WEBGL_SOFTWARE_VERSION;
            sampler_binding->resource = sampled_index * 2u + 1u;
            sampler_binding->kind = RIN_WEBGL_SOFTWARE_RESOURCE_SAMPLER_2D;
            sampler_binding->object = &bind_group->samplers[sampled_index];
        }
        memset(&resources, 0, sizeof(resources));
        resources.struct_size = sizeof(resources);
        resources.version = RIN_WEBGL_SOFTWARE_VERSION;
        resources.bindings = bindings;
        resources.binding_count = pipeline->resource_count;
        resources.byte_length = pipeline->resource_count * sizeof(bindings[0]);
        memset(&resource_state, 0, sizeof(resource_state));
        resource_state.struct_size = sizeof(resource_state);
        resource_state.version = RIN_WEBGL_SOFTWARE_VERSION;
        resource_state.draw = &state;
        resource_state.fragment_resources = &resources;
        result = rin_webgl_software_draw_resource_state_v1(
            &context->software_context, &resource_state);
    }
    if (result == RIN_WEBGL_SOFTWARE_OK) return RIN_GPU_OK;
    if (result == RIN_WEBGL_SOFTWARE_UNSUPPORTED) return RIN_GPU_ERROR_UNSUPPORTED;
    if (result == RIN_WEBGL_SOFTWARE_BOUNDS) return RIN_GPU_ERROR_BOUNDS;
    return RIN_GPU_ERROR_BACKEND;
}

static int backend_draw_vertices(RinGLAquamarineSurfaceContext* context,
                                 const RinGpuBackendDrawVerticesV1* draw,
                                 const RinGLAquamarineSurfaceRasterState*
                                     raster_state,
                                 const RinGLAquamarineSurfaceActiveRenderPass*
                                     active_pass)
{
    const RinGLAquamarineSurfacePipeline* pipeline =
        (const RinGLAquamarineSurfacePipeline*)(uintptr_t)draw->pipeline_cookie;
    const RinGLAquamarineSurfaceImage* color_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)draw->color_target_cookie;
    const RinGLAquamarineSurfaceImage* depth_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->depth_target_cookie;
    const RinGLAquamarineSurfaceImage* stencil_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->stencil_target_cookie;
    const RinGLAquamarineSurfaceBuffer* vertex_buffer =
        (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)draw->vertex_buffer_cookie;
    const RinGLAquamarineSurfaceBindGroup* bind_group =
        (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
            draw->bind_group_cookie;
    RinWebGLSoftwareVertexBufferV1 vertices;
    float* values = NULL;
    const uint8_t* source = NULL;
    uint64_t source_bytes = 0u;
    uint64_t byte_length;
    int result;

    if (pipeline->vertex_stride != 0u) {
        uint64_t first_byte = draw->vertex_offset +
            (uint64_t)draw->first_vertex * pipeline->vertex_stride;
        source = vertex_buffer->bytes + (size_t)first_byte;
        source_bytes = vertex_buffer->size_bytes - first_byte;
    }
    result = backend_unpack_vertex_values(pipeline, source, source_bytes,
                                          draw->vertex_count, &values);
    if (result != RIN_GPU_OK)
        return result;
    byte_length = (uint64_t)draw->vertex_count * pipeline->input_count *
        sizeof(*values);
    if (byte_length > UINT32_MAX) {
        free(values);
        return RIN_GPU_ERROR_LIMIT;
    }

    memset(&vertices, 0, sizeof(vertices));
    vertices.struct_size = sizeof(vertices);
    vertices.version = RIN_WEBGL_SOFTWARE_VERSION;
    vertices.values = values;
    vertices.input_count = pipeline->input_count;
    vertices.vertex_count = draw->vertex_count;
    vertices.byte_length = (uint32_t)byte_length;
    result = backend_execute_software_draw(context, pipeline, color_target,
                                           active_pass->color_mip_level,
                                           depth_target,
                                           active_pass->depth_mip_level,
                                           stencil_target,
                                           active_pass->stencil_mip_level,
                                           &vertices, NULL,
                                           raster_state, bind_group);
    free(values);
    return result;
}

static int backend_draw_vertices_v2(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawVerticesV2* draw,
    const RinGLAquamarineSurfaceRasterState* raster_state,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass)
{
    const RinGLAquamarineSurfacePipeline* pipeline =
        (const RinGLAquamarineSurfacePipeline*)(uintptr_t)draw->pipeline_cookie;
    const RinGLAquamarineSurfaceImage* color_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)draw->color_target_cookie;
    const RinGLAquamarineSurfaceImage* depth_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->depth_target_cookie;
    const RinGLAquamarineSurfaceImage* stencil_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->stencil_target_cookie;
    const RinGLAquamarineSurfaceBindGroup* bind_group =
        (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
            draw->bind_group_cookie;
    RinWebGLSoftwareVertexBufferV1 vertices;
    float* values = NULL;
    uint64_t byte_length;
    int result;

    result = backend_unpack_vertex_values_v2(
        context, pipeline, draw->vertex_buffers, draw->vertex_binding_count,
        draw->first_vertex, draw->vertex_count, &values);
    if (result != RIN_GPU_OK) return result;
    byte_length = (uint64_t)draw->vertex_count * pipeline->input_count *
        sizeof(*values);
    if (byte_length > UINT32_MAX) {
        free(values);
        return RIN_GPU_ERROR_LIMIT;
    }
    memset(&vertices, 0, sizeof(vertices));
    vertices.struct_size = sizeof(vertices);
    vertices.version = RIN_WEBGL_SOFTWARE_VERSION;
    vertices.values = values;
    vertices.input_count = pipeline->input_count;
    vertices.vertex_count = draw->vertex_count;
    vertices.byte_length = (uint32_t)byte_length;
    result = backend_execute_software_draw(context, pipeline, color_target,
                                           active_pass->color_mip_level,
                                           depth_target,
                                           active_pass->depth_mip_level,
                                           stencil_target,
                                           active_pass->stencil_mip_level,
                                           &vertices, NULL,
                                           raster_state, bind_group);
    free(values);
    return result;
}

static uint32_t backend_index_format_stride(uint32_t index_format)
{
    if (index_format == RIN_GPU_INDEX_UINT8) return 1u;
    if (index_format == RIN_GPU_INDEX_UINT16) return 2u;
    if (index_format == RIN_GPU_INDEX_UINT32) return 4u;
    return 0u;
}

static int backend_draw_indexed_valid(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawIndexedV1* draw,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass,
    uint32_t** native_indices)
{
    const RinGLAquamarineSurfacePipeline* pipeline;
    const RinGLAquamarineSurfaceImage* color_target;
    const RinGLAquamarineSurfaceBuffer* vertex_buffer;
    const RinGLAquamarineSurfaceBuffer* index_buffer;
    const RinGLAquamarineSurfaceBindGroup* bind_group;
    const uint8_t* source;
    uint32_t* values;
    uint32_t index_stride;
    uint32_t index;
    uint64_t vertex_bytes;
    uint64_t index_bytes;
    uint64_t first_index_byte;

    if (native_indices) *native_indices = NULL;
    if (!context || !draw || !active_pass || !native_indices ||
        active_pass->color_target_cookie == 0u || !draw->pipeline_cookie ||
        !draw->color_target_cookie ||
        !draw->index_buffer_cookie ||
        draw->mip_level != active_pass->color_mip_level ||
        draw->array_layer != 0u || draw->index_count == 0u ||
        draw->index_count > RIN_WEBGL_SOFTWARE_MAX_INDICES ||
        draw->vertex_count == 0u ||
        draw->vertex_count > RIN_WEBGL_SOFTWARE_MAX_VERTICES ||
        draw->instance_count != 1u || draw->first_instance != 0u ||
        draw->reserved != 0u) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    index_stride = backend_index_format_stride(draw->index_format);
    if (index_stride == 0u) return RIN_GPU_ERROR_UNSUPPORTED;
    pipeline = (const RinGLAquamarineSurfacePipeline*)(uintptr_t)
        draw->pipeline_cookie;
    color_target = (const RinGLAquamarineSurfaceImage*)(uintptr_t)
        draw->color_target_cookie;
    vertex_buffer = (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
        draw->vertex_buffer_cookie;
    index_buffer = (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
        draw->index_buffer_cookie;
    bind_group = (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
        draw->bind_group_cookie;
    if (!pipeline || pipeline->owner != context || !pipeline->vertex_shader ||
        !pipeline->fragment_shader || !color_target ||
        color_target->owner != context ||
        !image_is_color_target(context, draw->color_target_cookie) ||
        draw->color_target_cookie != active_pass->color_target_cookie ||
        (pipeline->depth_enabled != 0u &&
         active_pass->depth_target_cookie == 0u) ||
        (pipeline->stencil_enabled != 0u &&
         active_pass->stencil_target_cookie == 0u) ||
        draw->vertex_stride != pipeline->vertex_stride ||
        pipeline->input_count == 0u ||
        (pipeline->vertex_stride == 0u
             ? (draw->vertex_buffer_cookie != 0u || draw->vertex_offset != 0u)
             : (!vertex_buffer || vertex_buffer->owner != context ||
                !vertex_buffer->bytes)) ||
        !index_buffer || index_buffer->owner != context ||
        !index_buffer->bytes ||
        (draw->index_offset & (uint64_t)(index_stride - 1u)) != 0u ||
        (pipeline->resource_count == 0u
             ? draw->bind_group_cookie != 0u
             : !backend_bind_group_valid(context, pipeline, bind_group))) {
        return RIN_GPU_ERROR_BACKEND;
    }
    if (pipeline->vertex_stride != 0u) {
        vertex_bytes = (uint64_t)draw->vertex_count * pipeline->vertex_stride;
        if (draw->vertex_offset > vertex_buffer->size_bytes ||
            vertex_bytes > vertex_buffer->size_bytes - draw->vertex_offset) {
            return RIN_GPU_ERROR_BOUNDS;
        }
    }
    index_bytes = (uint64_t)draw->index_count * index_stride;
    first_index_byte = draw->index_offset +
        (uint64_t)draw->first_index * index_stride;
    if (first_index_byte < draw->index_offset ||
        draw->index_offset > index_buffer->size_bytes ||
        first_index_byte > index_buffer->size_bytes ||
        index_bytes > index_buffer->size_bytes - first_index_byte) {
        return RIN_GPU_ERROR_BOUNDS;
    }
    values = calloc(draw->index_count, sizeof(*values));
    if (!values) return RIN_GPU_ERROR_NO_MEMORY;
    source = index_buffer->bytes + (size_t)first_index_byte;
    for (index = 0u; index < draw->index_count; ++index) {
        uint32_t value = 0u;

        if (index_stride == 1u) {
            value = source[index];
        } else if (index_stride == 2u) {
            uint16_t value16;
            memcpy(&value16, source + (size_t)index * index_stride,
                   sizeof(value16));
            value = value16;
        } else {
            memcpy(&value, source + (size_t)index * index_stride,
                   sizeof(value));
        }
        if (value >= draw->vertex_count) {
            free(values);
            return RIN_GPU_ERROR_BOUNDS;
        }
        values[index] = value;
    }
    *native_indices = values;
    return RIN_GPU_OK;
}

static int backend_draw_indexed(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawIndexedV1* draw,
    const RinGLAquamarineSurfaceRasterState* raster_state,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass,
    const uint32_t* native_indices)
{
    const RinGLAquamarineSurfacePipeline* pipeline =
        (const RinGLAquamarineSurfacePipeline*)(uintptr_t)draw->pipeline_cookie;
    const RinGLAquamarineSurfaceImage* color_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)draw->color_target_cookie;
    const RinGLAquamarineSurfaceImage* depth_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->depth_target_cookie;
    const RinGLAquamarineSurfaceImage* stencil_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->stencil_target_cookie;
    const RinGLAquamarineSurfaceBuffer* vertex_buffer =
        (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)draw->vertex_buffer_cookie;
    const RinGLAquamarineSurfaceBindGroup* bind_group =
        (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
            draw->bind_group_cookie;
    RinWebGLSoftwareVertexBufferV1 vertices;
    RinWebGLSoftwareIndexBufferV1 indices;
    float* values = NULL;
    uint64_t byte_length;
    int result;

    if (!native_indices) return RIN_GPU_ERROR_BACKEND;
    result = backend_unpack_vertex_values(
        pipeline,
        pipeline->vertex_stride != 0u
            ? vertex_buffer->bytes + (size_t)draw->vertex_offset : NULL,
        pipeline->vertex_stride != 0u
            ? vertex_buffer->size_bytes - draw->vertex_offset : 0u,
        draw->vertex_count, &values);
    if (result != RIN_GPU_OK)
        return result;
    byte_length = (uint64_t)draw->vertex_count * pipeline->input_count *
        sizeof(*values);
    if (byte_length > UINT32_MAX) {
        free(values);
        return RIN_GPU_ERROR_LIMIT;
    }
    memset(&vertices, 0, sizeof(vertices));
    vertices.struct_size = sizeof(vertices);
    vertices.version = RIN_WEBGL_SOFTWARE_VERSION;
    vertices.values = values;
    vertices.input_count = pipeline->input_count;
    vertices.vertex_count = draw->vertex_count;
    vertices.byte_length = (uint32_t)byte_length;
    memset(&indices, 0, sizeof(indices));
    indices.struct_size = sizeof(indices);
    indices.version = RIN_WEBGL_SOFTWARE_VERSION;
    indices.values = native_indices;
    indices.index_count = draw->index_count;
    indices.byte_length = draw->index_count * sizeof(*native_indices);
    result = backend_execute_software_draw(context, pipeline, color_target,
                                           active_pass->color_mip_level,
                                           depth_target,
                                           active_pass->depth_mip_level,
                                           stencil_target,
                                           active_pass->stencil_mip_level,
                                           &vertices, &indices,
                                           raster_state, bind_group);
    free(values);
    return result;
}

static int backend_draw_indexed_valid_v2(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawIndexedV2* draw,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass,
    uint32_t** native_indices)
{
    const RinGLAquamarineSurfacePipeline* pipeline;
    const RinGLAquamarineSurfaceImage* color_target;
    const RinGLAquamarineSurfaceBuffer* index_buffer;
    const RinGLAquamarineSurfaceBindGroup* bind_group;
    const uint8_t* source;
    uint32_t* values;
    uint32_t index_stride;
    uint64_t index_bytes;
    uint64_t first_index_byte;

    if (native_indices) *native_indices = NULL;
    if (!context || !draw || !active_pass || !native_indices ||
        active_pass->color_target_cookie == 0u || !draw->pipeline_cookie ||
        !draw->color_target_cookie || !draw->index_buffer_cookie ||
        draw->mip_level != active_pass->color_mip_level ||
        draw->array_layer != 0u ||
        draw->index_count == 0u ||
        draw->index_count > RIN_WEBGL_SOFTWARE_MAX_INDICES ||
        draw->vertex_count == 0u ||
        draw->vertex_count > RIN_WEBGL_SOFTWARE_MAX_VERTICES ||
        draw->instance_count != 1u || draw->first_instance != 0u ||
        draw->vertex_binding_count > RIN_GPU_MAX_VERTEX_BUFFER_BINDINGS ||
        draw->reserved != 0u) {
        return RIN_GPU_ERROR_UNSUPPORTED;
    }
    index_stride = backend_index_format_stride(draw->index_format);
    if (index_stride == 0u) return RIN_GPU_ERROR_UNSUPPORTED;
    pipeline = (const RinGLAquamarineSurfacePipeline*)(uintptr_t)
        draw->pipeline_cookie;
    color_target = (const RinGLAquamarineSurfaceImage*)(uintptr_t)
        draw->color_target_cookie;
    index_buffer = (const RinGLAquamarineSurfaceBuffer*)(uintptr_t)
        draw->index_buffer_cookie;
    bind_group = (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
        draw->bind_group_cookie;
    if (!pipeline || pipeline->owner != context || !pipeline->vertex_shader ||
        !pipeline->fragment_shader || !color_target ||
        color_target->owner != context ||
        !image_is_color_target(context, draw->color_target_cookie) ||
        draw->color_target_cookie != active_pass->color_target_cookie ||
        (pipeline->depth_enabled != 0u &&
         active_pass->depth_target_cookie == 0u) ||
        (pipeline->stencil_enabled != 0u &&
         active_pass->stencil_target_cookie == 0u) ||
        pipeline->input_count == 0u || !index_buffer ||
        index_buffer->owner != context || !index_buffer->bytes ||
        (draw->index_offset & (uint64_t)(index_stride - 1u)) != 0u ||
        (pipeline->resource_count == 0u
             ? draw->bind_group_cookie != 0u
             : !backend_bind_group_valid(context, pipeline, bind_group))) {
        return RIN_GPU_ERROR_BACKEND;
    }
    {
        int vertex_result = backend_vertex_bindings_valid(
            context, pipeline, draw->vertex_buffers, draw->vertex_binding_count,
            0u, draw->vertex_count);
        if (vertex_result != RIN_GPU_OK)
            return vertex_result;
    }
    index_bytes = (uint64_t)draw->index_count * index_stride;
    first_index_byte = draw->index_offset +
        (uint64_t)draw->first_index * index_stride;
    if (first_index_byte < draw->index_offset ||
        draw->index_offset > index_buffer->size_bytes ||
        first_index_byte > index_buffer->size_bytes ||
        index_bytes > index_buffer->size_bytes - first_index_byte) {
        return RIN_GPU_ERROR_BOUNDS;
    }
    values = calloc(draw->index_count, sizeof(*values));
    if (!values) return RIN_GPU_ERROR_NO_MEMORY;
    source = index_buffer->bytes + (size_t)first_index_byte;
    for (uint32_t index = 0u; index < draw->index_count; ++index) {
        uint32_t value;
        if (index_stride == 1u) {
            value = source[index];
        } else if (index_stride == 2u) {
            uint16_t value16;
            memcpy(&value16, source + (size_t)index * index_stride,
                   sizeof(value16));
            value = value16;
        } else {
            memcpy(&value, source + (size_t)index * index_stride,
                   sizeof(value));
        }
        if (value >= draw->vertex_count) {
            free(values);
            return RIN_GPU_ERROR_BOUNDS;
        }
        values[index] = value;
    }
    *native_indices = values;
    return RIN_GPU_OK;
}

static int backend_draw_indexed_v2(
    RinGLAquamarineSurfaceContext* context,
    const RinGpuBackendDrawIndexedV2* draw,
    const RinGLAquamarineSurfaceRasterState* raster_state,
    const RinGLAquamarineSurfaceActiveRenderPass* active_pass,
    const uint32_t* native_indices)
{
    const RinGLAquamarineSurfacePipeline* pipeline =
        (const RinGLAquamarineSurfacePipeline*)(uintptr_t)draw->pipeline_cookie;
    const RinGLAquamarineSurfaceImage* color_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)draw->color_target_cookie;
    const RinGLAquamarineSurfaceImage* depth_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->depth_target_cookie;
    const RinGLAquamarineSurfaceImage* stencil_target =
        (const RinGLAquamarineSurfaceImage*)(uintptr_t)active_pass->stencil_target_cookie;
    const RinGLAquamarineSurfaceBindGroup* bind_group =
        (const RinGLAquamarineSurfaceBindGroup*)(uintptr_t)
            draw->bind_group_cookie;
    RinWebGLSoftwareVertexBufferV1 vertices;
    RinWebGLSoftwareIndexBufferV1 indices;
    float* values = NULL;
    uint64_t byte_length;
    int result;

    if (!native_indices) return RIN_GPU_ERROR_BACKEND;
    result = backend_unpack_vertex_values_v2(
        context, pipeline, draw->vertex_buffers, draw->vertex_binding_count,
        0u, draw->vertex_count, &values);
    if (result != RIN_GPU_OK) return result;
    byte_length = (uint64_t)draw->vertex_count * pipeline->input_count *
        sizeof(*values);
    if (byte_length > UINT32_MAX) {
        free(values);
        return RIN_GPU_ERROR_LIMIT;
    }
    memset(&vertices, 0, sizeof(vertices));
    vertices.struct_size = sizeof(vertices);
    vertices.version = RIN_WEBGL_SOFTWARE_VERSION;
    vertices.values = values;
    vertices.input_count = pipeline->input_count;
    vertices.vertex_count = draw->vertex_count;
    vertices.byte_length = (uint32_t)byte_length;
    memset(&indices, 0, sizeof(indices));
    indices.struct_size = sizeof(indices);
    indices.version = RIN_WEBGL_SOFTWARE_VERSION;
    indices.values = native_indices;
    indices.index_count = draw->index_count;
    indices.byte_length = draw->index_count * sizeof(*native_indices);
    result = backend_execute_software_draw(context, pipeline, color_target,
                                           active_pass->color_mip_level,
                                           depth_target,
                                           active_pass->depth_mip_level,
                                           stencil_target,
                                           active_pass->stencil_mip_level,
                                           &vertices, &indices,
                                           raster_state, bind_group);
    free(values);
    return result;
}

static int backend_submit(void* opaque,
                          const RinGpuBackendCommandV1* commands,
                          uint32_t command_count)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    RinGLAquamarineSurfaceRasterState raster_state;
    RinGLAquamarineSurfaceActiveRenderPass active_pass;
    uint32_t** indexed_values = NULL;
    uint32_t indexed_draw_count = 0u;
    uint32_t indexed_value_index = 0u;
    uint32_t index;
    int result = RIN_GPU_OK;

    if (!context || (!commands && command_count != 0u))
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    for (index = 0u; index < command_count; ++index) {
        if (commands[index].type == RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED ||
            commands[index].type == RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED_V2) {
            if (indexed_draw_count == UINT32_MAX)
                return RIN_GPU_ERROR_NO_MEMORY;
            indexed_draw_count++;
        }
    }
    if (indexed_draw_count != 0u) {
        if (indexed_draw_count > UINT32_MAX / sizeof(*indexed_values))
            return RIN_GPU_ERROR_NO_MEMORY;
        indexed_values = calloc(indexed_draw_count, sizeof(*indexed_values));
        if (!indexed_values) return RIN_GPU_ERROR_NO_MEMORY;
    }

    memset(&raster_state, 0, sizeof(raster_state));
    memset(&active_pass, 0, sizeof(active_pass));

    /* Validate every command before writing the caller-owned target. */
    for (index = 0u; index < command_count; ++index) {
        const RinGpuBackendCommandV1* command = &commands[index];
        if (command->type == RIN_GPU_BACKEND_COMMAND_TRANSITION_IMAGE) {
            if (!image_is_color_target(
                    context, command->value.image_transition.image_cookie) &&
                !image_is_depth_target(
                    context, command->value.image_transition.image_cookie)) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS) {
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    command->value.render_pass_begin.color_target_cookie;
            if (command->value.render_pass_begin.array_layer != 0u ||
                active_pass.color_target_cookie != 0u || !image_is_color_target(
                    context,
                    command->value.render_pass_begin.color_target_cookie) ||
                command->value.render_pass_begin.mip_level >=
                    color_target->descriptor.mip_levels) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            active_pass.color_target_cookie =
                command->value.render_pass_begin.color_target_cookie;
            active_pass.color_mip_level =
                command->value.render_pass_begin.mip_level;
            backend_default_raster_state(
                color_target, active_pass.color_mip_level, &raster_state);
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS_DEPTH) {
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    command->value.render_pass_depth_begin.color_target_cookie;
            const RinGLAquamarineSurfaceImage* depth_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    command->value.render_pass_depth_begin.depth_target_cookie;
            uint32_t color_width;
            uint32_t color_height;
            uint32_t depth_width;
            uint32_t depth_height;

            if (command->value.render_pass_depth_begin.color_array_layer != 0u ||
                command->value.render_pass_depth_begin.depth_array_layer != 0u ||
                active_pass.color_target_cookie != 0u ||
                !image_is_color_target(
                    context,
                    command->value.render_pass_depth_begin.color_target_cookie) ||
                !image_is_depth_target(
                    context,
                    command->value.render_pass_depth_begin.depth_target_cookie) ||
                !backend_image_mip_dimensions(
                    &color_target->descriptor,
                    command->value.render_pass_depth_begin.color_mip_level,
                    &color_width, &color_height) ||
                !backend_image_mip_dimensions(
                    &depth_target->descriptor,
                    command->value.render_pass_depth_begin.depth_mip_level,
                    &depth_width, &depth_height) ||
                color_width != depth_width || color_height != depth_height) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            active_pass.color_target_cookie =
                command->value.render_pass_depth_begin.color_target_cookie;
            active_pass.depth_target_cookie =
                command->value.render_pass_depth_begin.depth_target_cookie;
            active_pass.stencil_target_cookie =
                command->value.render_pass_depth_begin.depth_target_cookie;
            active_pass.color_mip_level =
                command->value.render_pass_depth_begin.color_mip_level;
            active_pass.depth_mip_level =
                command->value.render_pass_depth_begin.depth_mip_level;
            active_pass.stencil_mip_level = active_pass.depth_mip_level;
            backend_default_raster_state(
                color_target, active_pass.color_mip_level, &raster_state);
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS_DEPTH_STENCIL) {
            const RinGpuBackendRenderPassDepthStencilBeginV1* pass =
                &command->value.render_pass_depth_stencil_begin;
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    pass->color_target_cookie;
            const RinGLAquamarineSurfaceImage* depth_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    pass->depth_target_cookie;
            const RinGLAquamarineSurfaceImage* stencil_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    pass->stencil_target_cookie;
            uint32_t color_width;
            uint32_t color_height;
            uint32_t depth_width;
            uint32_t depth_height;
            uint32_t stencil_width;
            uint32_t stencil_height;

            if (pass->color_array_layer != 0u ||
                pass->depth_array_layer != 0u ||
                pass->stencil_array_layer != 0u ||
                active_pass.color_target_cookie != 0u ||
                !image_is_color_target(context, pass->color_target_cookie) ||
                !image_is_depth_target(context, pass->depth_target_cookie) ||
                !image_is_depth_target(context, pass->stencil_target_cookie) ||
                !image_has_depth_aspect(depth_target) ||
                !image_has_stencil_aspect(stencil_target) ||
                !backend_image_mip_dimensions(&color_target->descriptor,
                                              pass->color_mip_level,
                                              &color_width, &color_height) ||
                !backend_image_mip_dimensions(&depth_target->descriptor,
                                              pass->depth_mip_level,
                                              &depth_width, &depth_height) ||
                !backend_image_mip_dimensions(&stencil_target->descriptor,
                                              pass->stencil_mip_level,
                                              &stencil_width, &stencil_height) ||
                color_width != depth_width || color_height != depth_height ||
                color_width != stencil_width || color_height != stencil_height) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            active_pass.color_target_cookie = pass->color_target_cookie;
            active_pass.depth_target_cookie = pass->depth_target_cookie;
            active_pass.stencil_target_cookie = pass->stencil_target_cookie;
            active_pass.color_mip_level = pass->color_mip_level;
            active_pass.depth_mip_level = pass->depth_mip_level;
            active_pass.stencil_mip_level = pass->stencil_mip_level;
            backend_default_raster_state(color_target, pass->color_mip_level,
                                         &raster_state);
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES) {
            result = backend_draw_vertices_valid(
                context, &command->value.draw_vertices, &active_pass);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES_V2) {
            result = backend_draw_vertices_valid_v2(
                context, &command->value.draw_vertices_v2, &active_pass);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED) {
            result = backend_draw_indexed_valid(
                context, &command->value.draw_indexed, &active_pass,
                &indexed_values[indexed_value_index++]);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED_V2) {
            result = backend_draw_indexed_valid_v2(
                context, &command->value.draw_indexed_v2, &active_pass,
                &indexed_values[indexed_value_index++]);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_END_RENDER_PASS) {
            if (active_pass.color_target_cookie == 0u) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            memset(&active_pass, 0, sizeof(active_pass));
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_SET_RASTER_STATE) {
            if (active_pass.color_target_cookie == 0u) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            result = backend_raster_state_from_command(
                &command->value.raster_state, &raster_state);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_PRESENT) {
            if (active_pass.color_target_cookie != 0u ||
                !image_matches(context, command->value.present.image_cookie,
                                RIN_GPU_FORMAT_BGRA8_UNORM)) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
        } else {
            result = RIN_GPU_ERROR_UNSUPPORTED;
            goto cleanup;
        }
    }

    memset(&raster_state, 0, sizeof(raster_state));
    memset(&active_pass, 0, sizeof(active_pass));
    indexed_value_index = 0u;
    for (index = 0u; index < command_count; ++index) {
        const RinGpuBackendCommandV1* command = &commands[index];
        if (command->type == RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS) {
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    command->value.render_pass_begin.color_target_cookie;
            backend_default_raster_state(
                color_target, command->value.render_pass_begin.mip_level,
                &raster_state);
            active_pass.color_target_cookie =
                command->value.render_pass_begin.color_target_cookie;
            active_pass.color_mip_level =
                command->value.render_pass_begin.mip_level;
            if (command->value.render_pass_begin.load_op ==
                RIN_GPU_RENDER_CLEAR) {
                if (clear_color_target(
                        context, command->value.render_pass_begin.clear_red,
                        command->value.render_pass_begin.clear_green,
                        command->value.render_pass_begin.clear_blue,
                        command->value.render_pass_begin.clear_alpha,
                        command->value.render_pass_begin.color_write_mask,
                        &command->value.render_pass_begin.clear_region,
                        command->value.render_pass_begin.color_target_cookie,
                        active_pass.color_mip_level) !=
                    RIN_GPU_OK) {
                    result = RIN_GPU_ERROR_BACKEND;
                    goto cleanup;
                }
            }
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS_DEPTH) {
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    command->value.render_pass_depth_begin.color_target_cookie;
            backend_default_raster_state(
                color_target,
                command->value.render_pass_depth_begin.color_mip_level,
                &raster_state);
            active_pass.color_target_cookie =
                command->value.render_pass_depth_begin.color_target_cookie;
            active_pass.depth_target_cookie =
                command->value.render_pass_depth_begin.depth_target_cookie;
            active_pass.stencil_target_cookie =
                command->value.render_pass_depth_begin.depth_target_cookie;
            active_pass.color_mip_level =
                command->value.render_pass_depth_begin.color_mip_level;
            active_pass.depth_mip_level =
                command->value.render_pass_depth_begin.depth_mip_level;
            active_pass.stencil_mip_level = active_pass.depth_mip_level;
            if (command->value.render_pass_depth_begin.color_load_op ==
                RIN_GPU_RENDER_CLEAR) {
                if (clear_color_target(
                        context,
                        command->value.render_pass_depth_begin.clear_red,
                        command->value.render_pass_depth_begin.clear_green,
                        command->value.render_pass_depth_begin.clear_blue,
                        command->value.render_pass_depth_begin.clear_alpha,
                        command->value.render_pass_depth_begin.color_write_mask,
                        &command->value.render_pass_depth_begin.clear_region,
                        command->value.render_pass_depth_begin
                            .color_target_cookie,
                        active_pass.color_mip_level) != RIN_GPU_OK) {
                    result = RIN_GPU_ERROR_BACKEND;
                    goto cleanup;
                }
            }
            if (command->value.render_pass_depth_begin.depth_load_op ==
                RIN_GPU_RENDER_CLEAR) {
                if (clear_depth_target(
                        context,
                        command->value.render_pass_depth_begin.clear_depth,
                        &command->value.render_pass_depth_begin.clear_region,
                        command->value.render_pass_depth_begin
                            .depth_target_cookie,
                        active_pass.depth_mip_level) != RIN_GPU_OK) {
                    result = RIN_GPU_ERROR_BACKEND;
                    goto cleanup;
                }
            }
            if (command->value.render_pass_depth_begin.stencil_load_op ==
                RIN_GPU_RENDER_CLEAR) {
                if (clear_stencil_target(
                        context,
                        command->value.render_pass_depth_begin.clear_stencil,
                        command->value.render_pass_depth_begin
                            .stencil_write_mask,
                        &command->value.render_pass_depth_begin.clear_region,
                        command->value.render_pass_depth_begin
                            .depth_target_cookie,
                        active_pass.stencil_mip_level) != RIN_GPU_OK) {
                    result = RIN_GPU_ERROR_BACKEND;
                    goto cleanup;
                }
            }
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_BEGIN_RENDER_PASS_DEPTH_STENCIL) {
            const RinGpuBackendRenderPassDepthStencilBeginV1* pass =
                &command->value.render_pass_depth_stencil_begin;
            const RinGLAquamarineSurfaceImage* color_target =
                (const RinGLAquamarineSurfaceImage*)(uintptr_t)
                    pass->color_target_cookie;
            backend_default_raster_state(color_target, pass->color_mip_level,
                                         &raster_state);
            active_pass.color_target_cookie = pass->color_target_cookie;
            active_pass.depth_target_cookie = pass->depth_target_cookie;
            active_pass.stencil_target_cookie = pass->stencil_target_cookie;
            active_pass.color_mip_level = pass->color_mip_level;
            active_pass.depth_mip_level = pass->depth_mip_level;
            active_pass.stencil_mip_level = pass->stencil_mip_level;
            if (pass->color_load_op == RIN_GPU_RENDER_CLEAR &&
                clear_color_target(context, pass->clear_red, pass->clear_green,
                                   pass->clear_blue, pass->clear_alpha,
                                   pass->color_write_mask, &pass->clear_region,
                                   pass->color_target_cookie,
                                   active_pass.color_mip_level) != RIN_GPU_OK) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            if (pass->depth_load_op == RIN_GPU_RENDER_CLEAR &&
                clear_depth_target(context, pass->clear_depth,
                                   &pass->clear_region,
                                   pass->depth_target_cookie,
                                   active_pass.depth_mip_level) != RIN_GPU_OK) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
            if (pass->stencil_load_op == RIN_GPU_RENDER_CLEAR &&
                clear_stencil_target(context, pass->clear_stencil,
                                     pass->stencil_write_mask,
                                     &pass->clear_region,
                                     pass->stencil_target_cookie,
                                     active_pass.stencil_mip_level) !=
                    RIN_GPU_OK) {
                result = RIN_GPU_ERROR_BACKEND;
                goto cleanup;
            }
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_SET_RASTER_STATE) {
            result = backend_raster_state_from_command(
                &command->value.raster_state, &raster_state);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES) {
            result = backend_draw_vertices(
                context, &command->value.draw_vertices, &raster_state,
                &active_pass);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_DRAW_VERTICES_V2) {
            result = backend_draw_vertices_v2(
                context, &command->value.draw_vertices_v2, &raster_state,
                &active_pass);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED) {
            result = backend_draw_indexed(
                context, &command->value.draw_indexed, &raster_state,
                &active_pass,
                indexed_values[indexed_value_index++]);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type ==
                   RIN_GPU_BACKEND_COMMAND_DRAW_INDEXED_V2) {
            result = backend_draw_indexed_v2(
                context, &command->value.draw_indexed_v2, &raster_state,
                &active_pass,
                indexed_values[indexed_value_index++]);
            if (result != RIN_GPU_OK) goto cleanup;
        } else if (command->type == RIN_GPU_BACKEND_COMMAND_END_RENDER_PASS) {
            memset(&active_pass, 0, sizeof(active_pass));
        }
    }
cleanup:
    for (index = 0u; index < indexed_draw_count; ++index)
        free(indexed_values ? indexed_values[index] : NULL);
    free(indexed_values);
    return result;
}

static int initialize_context(RinGLAquamarineSurfaceContext* context,
                              const RinGLAquamarineSurfaceTargetV1* target)
{
    RinGpuCoreConfigV1 config;
    RinGpuImageDescV1 image;
    RinGpuQueueDescV1 queue;
    RinGpuCommandListDescV1 command_list;
    uint64_t color_bytes;
    uint64_t depth_bytes;
    int result;

    if (!context || !target_valid(target))
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;

    memset(context, 0, sizeof(*context));
    memset(&context->target, 0, sizeof(context->target));
    memcpy(&context->target, target, target->struct_size);
    initialize_aquamarine_surface(context);
    context->display.abi_version = RIN_GPU_ABI_VERSION;
    context->display.struct_size = sizeof(context->display);
    context->display.display_id = RIN_GPU_PRIMARY_DISPLAY;
    context->display.flags = RIN_GPU_DISPLAY_CONNECTED | RIN_GPU_DISPLAY_PRIMARY;
    context->display.width = target->width;
    context->display.height = target->height;
    context->display.refresh_millihertz = 60000u;
    context->display.format = RIN_GPU_FORMAT_BGRA8_UNORM;
    context->display.physical_width_mm = 1u;
    context->display.physical_height_mm = 1u;
    context->display.scale_milli = 1000u;
    memcpy(context->display.name, "Ladybird RinGPU surface", 23u);

    color_bytes = (uint64_t)target->pitch_bytes * target->height;
    depth_bytes = target->depth
        ? (uint64_t)target->depth_pitch_floats * target->height * sizeof(float)
        : 0u;
    if (color_bytes > RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES ||
        depth_bytes > RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }

    memset(&config, 0, sizeof(config));
    config.abi_version = RIN_GPU_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.handle_secret = UINT64_C(0x52494e574542474c);
    config.max_buffer_size = RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES;
    /* A complete 2D mip chain is less than twice its base-level storage. */
    config.max_image_size =
        RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES * UINT64_C(2);
    /* One context may own a presentation image, an optional depth image, a
     * custom color target, and one resource of equal bounded size. */
    config.max_total_allocation_size =
        RINGL_AQUAMARINE_SURFACE_MAX_TOTAL_ALLOCATION_BYTES;
    config.max_image_dimension = RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION;
    config.max_image_layers = 1u;
    config.max_image_mip_levels =
        RINGL_AQUAMARINE_SURFACE_MAX_IMAGE_MIP_LEVELS;
    config.max_image_sample_count = 1u;
    config.adapter.abi_version = RIN_GPU_ABI_VERSION;
    config.adapter.struct_size = sizeof(config.adapter);
    config.adapter.queue_capabilities = RIN_GPU_QUEUE_GRAPHICS;
    memcpy(config.adapter.name, "Ladybird RinGPU adapter", 23u);
    config.backend.abi_version = RIN_GPU_ABI_VERSION;
    config.backend.struct_size = sizeof(config.backend);
    config.backend.create_buffer = backend_create_buffer;
    config.backend.destroy_buffer = backend_destroy_buffer;
    config.backend.upload_buffer = backend_upload_buffer;
    config.backend.create_image = backend_create_image;
    config.backend.destroy_image = backend_destroy_image;
    config.backend.upload_image = backend_upload_image;
    config.backend.create_sampler = backend_create_sampler;
    config.backend.destroy_sampler = backend_destroy_sampler;
    config.backend.create_shader_module = backend_create_shader;
    config.backend.destroy_shader_module = backend_destroy_shader;
    config.backend.create_compute_pipeline = backend_create_compute_pipeline;
    config.backend.destroy_compute_pipeline = backend_destroy_placeholder;
    config.backend.create_graphics_pipeline = backend_create_graphics_pipeline;
    config.backend.destroy_graphics_pipeline = backend_destroy_graphics_pipeline;
    config.backend.create_compute_bind_group = backend_create_compute_bind_group;
    config.backend.destroy_compute_bind_group = backend_destroy_placeholder;
    config.backend.create_graphics_bind_group = backend_create_graphics_bind_group;
    config.backend.destroy_graphics_bind_group = backend_destroy_graphics_bind_group;
    config.backend.submit_commands = backend_submit;
    config.backend.wait_for_completion = backend_wait_for_completion;
    config.backend.readback_image = backend_readback_image;
    config.backend_context = context;
    config.displays = &context->display;
    config.display_count = 1u;
    result = ringpu_core_init(&context->core, &config);
    if (result != RIN_GPU_OK) goto fail;

    memset(&queue, 0, sizeof(queue));
    queue.abi_version = RIN_GPU_ABI_VERSION;
    queue.struct_size = sizeof(queue);
    queue.capabilities = RIN_GPU_QUEUE_GRAPHICS;
    result = ringpu_create_queue(&context->core, &queue, &context->queue);
    if (result != RIN_GPU_OK) goto fail;

    memset(&command_list, 0, sizeof(command_list));
    command_list.abi_version = RIN_GPU_ABI_VERSION;
    command_list.struct_size = sizeof(command_list);
    command_list.capabilities = RIN_GPU_QUEUE_GRAPHICS;
    result = ringpu_create_command_list(&context->core, &command_list,
                                        &context->command_list);
    if (result != RIN_GPU_OK) goto fail;

    memset(&image, 0, sizeof(image));
    image.abi_version = RIN_GPU_ABI_VERSION;
    image.struct_size = sizeof(image);
    image.dimension = RIN_GPU_IMAGE_DIMENSION_2D;
    image.format = RIN_GPU_FORMAT_BGRA8_UNORM;
    image.width = target->width;
    image.height = target->height;
    image.depth = 1u;
    image.array_layers = 1u;
    image.mip_levels = 1u;
    image.sample_count = 1u;
    image.usage = RIN_GPU_IMAGE_COPY_DESTINATION |
                  RIN_GPU_IMAGE_COPY_SOURCE |
                  RIN_GPU_IMAGE_COLOR_TARGET | RIN_GPU_IMAGE_PRESENT;
    image.flags = RIN_GPU_IMAGE_CPU_VISIBLE | RIN_GPU_IMAGE_CPU_READABLE;
    result = ringpu_create_image(&context->core, &image, &context->color_image);
    if (result != RIN_GPU_OK) goto fail;

    if (context->target.depth) {
        image.format = context->target.stencil != NULL
            ? RIN_GPU_FORMAT_D32_FLOAT_S8_UINT : RIN_GPU_FORMAT_D32_FLOAT;
        image.usage = RIN_GPU_IMAGE_COPY_DESTINATION |
                      RIN_GPU_IMAGE_DEPTH_STENCIL;
        image.flags = RIN_GPU_IMAGE_CPU_VISIBLE;
        result = ringpu_create_image(&context->core, &image,
                                     &context->depth_image);
        if (result != RIN_GPU_OK) goto fail;
    }
    context->initialized = RINGL_AQUAMARINE_SURFACE_VERSION;
    return RINGL_AQUAMARINE_SURFACE_OK;

fail:
    ringpu_core_shutdown(&context->core);
    memset(context, 0, sizeof(*context));
    return result == RIN_GPU_ERROR_NO_MEMORY
        ? RINGL_AQUAMARINE_SURFACE_NO_MEMORY
        : RINGL_AQUAMARINE_SURFACE_BACKEND;
}

static int context_ready(const RinGLAquamarineSurfaceContext* context)
{
    return context && context->initialized == RINGL_AQUAMARINE_SURFACE_VERSION;
}

static void transition_for(RinGpuImageTransitionV1* transition,
                           uint32_t before, uint32_t after)
{
    memset(transition, 0, sizeof(*transition));
    transition->abi_version = RIN_GPU_ABI_VERSION;
    transition->struct_size = sizeof(*transition);
    transition->mip_level_count = 1u;
    transition->array_layer_count = 1u;
    transition->before_state = before;
    transition->after_state = after;
}

static int submit(RinGLAquamarineSurfaceContext* context)
{
    RinGpuSubmitInfoV1 submit;
    int result;

    result = ringpu_command_list_close(&context->core, context->command_list);
    if (result != RIN_GPU_OK) return RINGL_AQUAMARINE_SURFACE_BACKEND;
    memset(&submit, 0, sizeof(submit));
    submit.abi_version = RIN_GPU_ABI_VERSION;
    submit.struct_size = sizeof(submit);
    submit.command_list = context->command_list;
    result = ringpu_queue_submit(&context->core, context->queue, &submit);
    return result == RIN_GPU_OK ? RINGL_AQUAMARINE_SURFACE_OK
                                : RINGL_AQUAMARINE_SURFACE_BACKEND;
}

int ringl_aquamarine_surface_begin_content_update(
    RinGLAquamarineSurfaceContext* context)
{
    RinGpuImageTransitionV1 transition;
    int result;

    if (!context_ready(context)) return RINGL_AQUAMARINE_SURFACE_STATE;
    if (context->color_state == RIN_GPU_IMAGE_STATE_COLOR_TARGET)
        return RINGL_AQUAMARINE_SURFACE_OK;

    result = ringpu_command_list_reset(&context->core, context->command_list);
    if (result != RIN_GPU_OK) return RINGL_AQUAMARINE_SURFACE_BACKEND;
    transition_for(&transition, context->color_state,
                   RIN_GPU_IMAGE_STATE_COLOR_TARGET);
    result = ringpu_command_transition_image(
        &context->core, context->command_list, context->color_image,
        &transition);
    if (result != RIN_GPU_OK) return RINGL_AQUAMARINE_SURFACE_BACKEND;
    result = submit(context);
    if (result != RINGL_AQUAMARINE_SURFACE_OK) return result;
    context->color_state = RIN_GPU_IMAGE_STATE_COLOR_TARGET;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_create(
    const RinGLAquamarineSurfaceTargetV1* target,
    RinGLAquamarineSurfaceContext** context_out)
{
    RinGLAquamarineSurfaceContext* context;
    int result;

    if (!context_out) return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    *context_out = NULL;
    context = calloc(1u, sizeof(*context));
    if (!context) return RINGL_AQUAMARINE_SURFACE_NO_MEMORY;
    result = initialize_context(context, target);
    if (result != RINGL_AQUAMARINE_SURFACE_OK) {
        free(context);
        return result;
    }
    *context_out = context;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

void ringl_aquamarine_surface_destroy(RinGLAquamarineSurfaceContext* context)
{
    if (!context) return;
    ringpu_core_shutdown(&context->core);
    memset(context, 0, sizeof(*context));
    free(context);
}

int ringl_aquamarine_surface_sync_external_color_state(
    RinGLAquamarineSurfaceContext* context, uint32_t state)
{
    if (!context_ready(context)) return RINGL_AQUAMARINE_SURFACE_STATE;
    if (state != RIN_GPU_IMAGE_STATE_COLOR_TARGET &&
        state != RIN_GPU_IMAGE_STATE_PRESENT) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    context->color_state = state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_sync_external_framebuffer_states(
    RinGLAquamarineSurfaceContext* context, uint32_t color_state,
    uint32_t depth_state)
{
    if (!context_ready(context)) return RINGL_AQUAMARINE_SURFACE_STATE;
    if (color_state != RIN_GPU_IMAGE_STATE_COLOR_TARGET &&
        color_state != RIN_GPU_IMAGE_STATE_PRESENT) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    if ((context->depth_image == 0u &&
         depth_state != RIN_GPU_IMAGE_STATE_UNDEFINED) ||
        (context->depth_image != 0u &&
         depth_state != RIN_GPU_IMAGE_STATE_UNDEFINED &&
         depth_state != RIN_GPU_IMAGE_STATE_DEPTH_TARGET)) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    context->color_state = color_state;
    if (context->depth_image != 0u)
        context->depth_state = depth_state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_get_native(
    RinGLAquamarineSurfaceContext* context,
    RinGLAquamarineSurfaceNativeV1* native_out)
{
    if (!context_ready(context) || native_out == NULL ||
        native_out->struct_size < sizeof(*native_out) ||
        native_out->version != RINGL_AQUAMARINE_SURFACE_NATIVE_VERSION ||
        native_out->reserved0 != 0u) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }

    native_out->ringpu_core = &context->core;
    native_out->graphics_queue = context->queue;
    native_out->color_image = context->color_image;
    native_out->queue_capabilities = RIN_GPU_QUEUE_GRAPHICS;
    native_out->color_format = context->display.format;
    native_out->width = context->target.width;
    native_out->height = context->target.height;
    native_out->display_id = context->display.display_id;
    native_out->color_state = context->color_state;
    native_out->depth_image = context->depth_image;
    native_out->depth_format = context->depth_image != 0u
        ? (context->target.stencil != NULL
               ? RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
               : RIN_GPU_FORMAT_D32_FLOAT)
        : 0u;
    native_out->depth_state = context->depth_state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}
