/* SPDX-License-Identifier: MIT */
#ifndef RINGL_INTERNAL_H
#define RINGL_INTERNAL_H

#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>

#include "objects/object_table.h"

#define RINGL_CONTEXT_MAGIC 0x52474c43u /* RGLC */
#define RINGL_NATIVE_VERTEX_UINT32 1u
#define RINGL_NATIVE_VERTEX_SINT32 2u
#define RINGL_NATIVE_VERTEX_FLOAT32 3u
#define RINGL_NATIVE_VERTEX_UINT8 4u
#define RINGL_NATIVE_VERTEX_SINT8 5u
#define RINGL_NATIVE_VERTEX_UNORM8 6u
#define RINGL_NATIVE_VERTEX_SNORM8 7u
#define RINGL_NATIVE_VERTEX_UINT16 8u
#define RINGL_NATIVE_VERTEX_SINT16 9u
#define RINGL_NATIVE_VERTEX_UNORM16 10u
#define RINGL_NATIVE_VERTEX_SNORM16 11u
#define RINGL_SHADER_LOG_MAX 160u
#define RINGL_PROGRAM_LOG_MAX 160u
#define RINGL_MAX_SAMPLER_UNIFORMS 8u
#define RINGL_MAX_FLOAT_UNIFORMS 8u
#define RINGL_MAX_INT_UNIFORMS 8u
#define RINGL_MAX_VEC2_UNIFORMS 8u
#define RINGL_MAX_VEC3_UNIFORMS 8u
#define RINGL_MAX_VEC4_UNIFORMS 8u
#define RINGL_MAX_IVEC2_UNIFORMS 8u
#define RINGL_MAX_IVEC3_UNIFORMS 8u
#define RINGL_MAX_IVEC4_UNIFORMS 8u
#define RINGL_MAX_MAT4_UNIFORMS 4u
#define RINGL_MAX_VARYINGS 8u
#define RINGL_UNIFORM_NAME_MAX RINGL_ACTIVE_INFO_NAME_MAX
/* RINGL_MAX_TEXTURE_SIZE is 4096, so a 2D texture has at most levels 0..12.
 * Keep the storage bound explicit rather than deriving an unchecked array
 * index from application-controlled level values. */
#define RINGL_MAX_TEXTURE_MIP_LEVELS 13u
/* RSH1 and the public RinGPU adapter both admit 32 scalar vertex inputs. The
 * public GL limit remains 16 generic vertex-array indices. */
#define RINGL_MAX_VERTEX_INPUT_COMPONENTS 32u

typedef struct RinGLBufferObject {
    uint64_t ringpu_handle;
    uint64_t size_bytes;
    uint8_t* shadow_bytes;
    uint32_t usage;
    uint32_t reserved0;
} RinGLBufferObject;

typedef struct RinGLTextureMipStorage {
    uint8_t* shadow_bytes;
    uint64_t shadow_size;
    uint32_t width;
    uint32_t height;
    uint32_t defined;
    uint32_t generated;
} RinGLTextureMipStorage;

typedef struct RinGLTextureObject {
    uint64_t ringpu_image;
    uint64_t ringpu_sampler;
    uint8_t* shadow_bytes;
    uint64_t shadow_size;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    /* Zero for depth/stencil storage; otherwise the accepted color upload
     * component type. FLOAT selects native RGBA32F shadow storage while the
     * public WebGL internal-format token remains unchanged. */
    uint32_t color_component_type;
    uint32_t defined;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t wrap_s;
    uint32_t wrap_t;
    /* RinGPU tracks image state per subresource. Keeping the same ownership
     * here prevents a render to mip 1 from publishing COLOR_TARGET for mip 0. */
    uint32_t ringpu_image_state[RINGL_MAX_TEXTURE_MIP_LEVELS];
    uint32_t requires_color_target;
    /* Level zero remains in the fields above for ABI-local compatibility with
     * the existing framebuffer and query paths.  Higher levels are separate,
     * individually-owned CPU snapshots. */
    RinGLTextureMipStorage
        mip_storage[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u];
} RinGLTextureObject;

typedef struct RinGLFramebufferObject {
    uint32_t color_attachment_kind;
    uint32_t color_attachment_object;
    int32_t color_attachment_level;
    uint32_t depth_attachment_kind;
    uint32_t depth_attachment_object;
    int32_t depth_attachment_level;
    uint32_t depth_attachment_has_depth;
    uint32_t depth_attachment_has_stencil;
    uint32_t stencil_attachment_kind;
    uint32_t stencil_attachment_object;
    int32_t stencil_attachment_level;
} RinGLFramebufferObject;

typedef struct RinGLRenderbufferObject {
    uint64_t ringpu_image;
    uint32_t internal_format;
    uint32_t width;
    uint32_t height;
    uint32_t defined;
    uint32_t ringpu_image_state;
} RinGLRenderbufferObject;

typedef struct RinGLShaderObject {
    char* source;
    uint8_t* rsh1;
    uint64_t source_length;
    uint64_t ringpu_module;
    uint32_t rsh1_size;
    uint32_t shader_type;
    uint32_t compile_status;
    uint32_t declaration_count;
    uint32_t statement_count;
    uint32_t attribute_count;
    uint32_t sampler_uniform_count;
    uint32_t float_uniform_count;
    uint32_t int_uniform_count;
    uint32_t vec2_uniform_count;
    uint32_t vec3_uniform_count;
    uint32_t vec4_uniform_count;
    uint32_t ivec2_uniform_count;
    uint32_t ivec3_uniform_count;
    uint32_t ivec4_uniform_count;
    uint32_t mat4_uniform_count;
    uint32_t rsh1_sampler_binding_count;
    uint32_t varying_count;
    uint32_t delete_pending;
    char sampler_uniform_names[RINGL_MAX_SAMPLER_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char float_uniform_names[RINGL_MAX_FLOAT_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char int_uniform_names[RINGL_MAX_INT_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char vec2_uniform_names[RINGL_MAX_VEC2_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char vec3_uniform_names[RINGL_MAX_VEC3_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char vec4_uniform_names[RINGL_MAX_VEC4_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char ivec2_uniform_names[RINGL_MAX_IVEC2_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char ivec3_uniform_names[RINGL_MAX_IVEC3_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char ivec4_uniform_names[RINGL_MAX_IVEC4_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    char mat4_uniform_names[RINGL_MAX_MAT4_UNIFORMS][RINGL_UNIFORM_NAME_MAX];
    uint32_t rsh1_sampler_binding_indices[RINGL_MAX_SAMPLER_UNIFORMS];
    char varying_names[RINGL_MAX_VARYINGS][RINGL_UNIFORM_NAME_MAX];
    uint32_t varying_widths[RINGL_MAX_VARYINGS];
    char info_log[RINGL_SHADER_LOG_MAX];
} RinGLShaderObject;

typedef struct RinGLProgramSamplerUniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    int32_t texture_unit;
} RinGLProgramSamplerUniform;

typedef struct RinGLProgramFloatUniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    float value;
} RinGLProgramFloatUniform;

typedef struct RinGLProgramIntUniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    int32_t value;
} RinGLProgramIntUniform;

typedef struct RinGLProgramVec2Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    float values[2];
} RinGLProgramVec2Uniform;

typedef struct RinGLProgramVec3Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    float values[3];
} RinGLProgramVec3Uniform;

/* Scalar RSH1 has no mutable uniform resource yet. Vec4 values therefore
 * live on the linked program and are lowered to program-owned CONST_F32
 * instructions.  Program ownership is required: one compiled shader may be
 * attached to multiple programs with different WebGL uniform state. */
typedef struct RinGLProgramVec4Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    float values[4];
} RinGLProgramVec4Uniform;

typedef struct RinGLProgramIVec2Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    int32_t values[2];
} RinGLProgramIVec2Uniform;

typedef struct RinGLProgramIVec3Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    int32_t values[3];
} RinGLProgramIVec3Uniform;

typedef struct RinGLProgramIVec4Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    int32_t values[4];
} RinGLProgramIVec4Uniform;

typedef struct RinGLProgramMat4Uniform {
    char name[RINGL_UNIFORM_NAME_MAX];
    float values[16];
} RinGLProgramMat4Uniform;

typedef struct RinGLProgramAttribute {
    char name[RINGL_UNIFORM_NAME_MAX];
    uint32_t width;
    uint32_t location;
} RinGLProgramAttribute;

typedef struct RinGLProgramAttributeBinding {
    char name[RINGL_UNIFORM_NAME_MAX];
    uint32_t location;
    uint32_t active;
} RinGLProgramAttributeBinding;

typedef struct RinGLProgramVarying {
    char name[RINGL_UNIFORM_NAME_MAX];
    uint32_t width;
    uint32_t vertex_output_location;
    uint32_t fragment_input_location;
} RinGLProgramVarying;

typedef struct RinGLProgramObject {
    uint32_t vertex_shader;
    uint32_t fragment_shader;
    uint32_t linked_vertex_shader;
    uint32_t linked_fragment_shader;
    uint32_t link_status;
    uint32_t validate_status;
    uint32_t attribute_count;
    uint32_t sampler_uniform_count;
    uint32_t float_uniform_count;
    uint32_t int_uniform_count;
    uint32_t vec2_uniform_count;
    uint32_t vec3_uniform_count;
    uint32_t vec4_uniform_count;
    uint32_t ivec2_uniform_count;
    uint32_t ivec3_uniform_count;
    uint32_t ivec4_uniform_count;
    uint32_t mat4_uniform_count;
    uint32_t varying_count;
    RinGLProgramAttribute attributes[RINGL_MAX_VERTEX_ATTRIBS];
    /* Pending bindAttribLocation requests deliberately live outside the
     * linked reflection fields: they survive link attempts and only affect a
     * later successful executable. */
    RinGLProgramAttributeBinding
        attribute_bindings[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLProgramSamplerUniform sampler_uniforms[RINGL_MAX_SAMPLER_UNIFORMS];
    RinGLProgramFloatUniform float_uniforms[RINGL_MAX_FLOAT_UNIFORMS];
    RinGLProgramIntUniform int_uniforms[RINGL_MAX_INT_UNIFORMS];
    RinGLProgramVec2Uniform vec2_uniforms[RINGL_MAX_VEC2_UNIFORMS];
    RinGLProgramVec3Uniform vec3_uniforms[RINGL_MAX_VEC3_UNIFORMS];
    RinGLProgramVec4Uniform vec4_uniforms[RINGL_MAX_VEC4_UNIFORMS];
    RinGLProgramIVec2Uniform ivec2_uniforms[RINGL_MAX_IVEC2_UNIFORMS];
    RinGLProgramIVec3Uniform ivec3_uniforms[RINGL_MAX_IVEC3_UNIFORMS];
    RinGLProgramIVec4Uniform ivec4_uniforms[RINGL_MAX_IVEC4_UNIFORMS];
    RinGLProgramMat4Uniform mat4_uniforms[RINGL_MAX_MAT4_UNIFORMS];
    uint8_t* vertex_uniform_rsh1;
    uint8_t* fragment_uniform_rsh1;
    uint64_t vertex_uniform_module;
    uint64_t fragment_uniform_module;
    uint32_t vertex_uniform_rsh1_size;
    uint32_t fragment_uniform_rsh1_size;
    RinGLProgramVarying varyings[RINGL_MAX_VARYINGS];
    char info_log[RINGL_PROGRAM_LOG_MAX];
} RinGLProgramObject;

typedef struct RinGLVertexAttribState {
    uint32_t enabled;
    uint32_t size;
    uint32_t type;
    uint32_t normalized;
    uint32_t stride;
    /* Zero is per-vertex; a non-zero value is the WebGL instance divisor. */
    uint32_t divisor;
    uint32_t buffer;
    uint64_t offset;
    float current_value[4];
} RinGLVertexAttribState;

/* A vertex-array object owns the array-descriptor fields and its element
 * buffer binding. Current generic attribute values deliberately remain on
 * the context: WebGL inherits the OpenGL ES rule that those values are not
 * captured by a VAO. */
typedef struct RinGLVertexArrayState {
    uint32_t element_array_buffer;
    RinGLVertexAttribState vertex_attribs[RINGL_MAX_VERTEX_ATTRIBS];
} RinGLVertexArrayState;

typedef struct RinGLResolvedVertexAttribute {
    uint32_t location;
    uint32_t format;
    uint32_t offset;
    uint32_t flags;
    uint32_t binding;
} RinGLResolvedVertexAttribute;

typedef struct RinGLResolvedVertexBinding {
    uint32_t buffer;
    uint32_t stride;
    uint32_t divisor;
} RinGLResolvedVertexBinding;

typedef struct RinGLResolvedVertexLayout {
    /* The legacy fields are populated only for one streamed binding, so
     * callers still using V1 can retain their previous fast path. */
    uint32_t buffer;
    uint32_t stride;
    uint32_t binding_count;
    uint32_t attribute_count;
    uint32_t has_constant_attributes;
    RinGLResolvedVertexBinding bindings[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLResolvedVertexAttribute
        attributes[RINGL_MAX_VERTEX_INPUT_COMPONENTS];
} RinGLResolvedVertexLayout;

typedef struct RinGLColorTarget {
    uint64_t image;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t mip_level;
    uint32_t* state;
} RinGLColorTarget;

typedef struct RinGLDepthTarget {
    uint64_t image;
    uint32_t format;
    uint32_t has_depth;
    uint32_t has_stencil;
    uint32_t mip_level;
    uint32_t* state;
} RinGLDepthTarget;

typedef struct RinGLDepthStencilTargets {
    RinGLDepthTarget depth;
    RinGLDepthTarget stencil;
    uint32_t combined;
} RinGLDepthStencilTargets;

struct RinGLContext {
    uint32_t magic;
    uint32_t pending_error;
    uint32_t lost;
    uint32_t loss_reported;
    uint32_t dirty_bits;
    RinGLRinGpuBindingV1 ringpu;
    RinGLRinGpuOpsV1 ringpu_ops;
    RinGLRinGpuSyncOpsV1 sync_ops;
    int has_ringpu;
    int has_ringpu_ops;
    int has_sync_ops;
    int reserved_sync0;
    void* pipeline_cache;
    RinGLDefaultFramebufferV1 default_framebuffer;
    uint32_t has_default_framebuffer;
    uint32_t default_framebuffer_state;
    uint32_t default_depth_framebuffer_state;
    uint32_t webgl_float_texture_linear_enabled;
    uint32_t webgl_float_color_buffer_enabled;
    uint64_t graphics_command_list;
    uint64_t graphics_bind_group;
    uint64_t finish_fence;
    uint64_t finish_value;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
    float clear_depth;
    uint32_t clear_stencil;
    float depth_range_near;
    float depth_range_far;

    int32_t viewport_x;
    int32_t viewport_y;
    uint32_t viewport_width;
    uint32_t viewport_height;
    uint32_t viewport_initialized;
    int32_t scissor_x;
    int32_t scissor_y;
    uint32_t scissor_width;
    uint32_t scissor_height;
    uint32_t scissor_enabled;
    uint32_t cull_face_enabled;
    uint32_t depth_test_enabled;
    uint32_t polygon_offset_fill_enabled;
    uint32_t sample_coverage_enabled;
    uint32_t stencil_test_enabled;
    uint32_t blend_enabled;
    uint32_t cull_face_mode;
    uint32_t front_face;
    uint32_t depth_func;
    uint32_t depth_write_mask;
    float polygon_offset_factor;
    float polygon_offset_units;
    float line_width;
    float sample_coverage_value;
    uint32_t sample_coverage_invert;
    uint32_t stencil_func;
    uint32_t stencil_reference;
    uint32_t stencil_value_mask;
    uint32_t stencil_write_mask;
    uint32_t stencil_fail_operation;
    uint32_t stencil_depth_fail_operation;
    uint32_t stencil_pass_operation;
    uint32_t back_stencil_func;
    uint32_t back_stencil_reference;
    uint32_t back_stencil_value_mask;
    uint32_t back_stencil_write_mask;
    uint32_t back_stencil_fail_operation;
    uint32_t back_stencil_depth_fail_operation;
    uint32_t back_stencil_pass_operation;
    uint32_t blend_source_rgb;
    uint32_t blend_destination_rgb;
    uint32_t blend_source_alpha;
    uint32_t blend_destination_alpha;
    uint32_t blend_equation_rgb;
    uint32_t blend_equation_alpha;
    float blend_constant_red;
    float blend_constant_green;
    float blend_constant_blue;
    float blend_constant_alpha;
    uint32_t color_write_mask;
    uint32_t unpack_alignment;

    RinGLObjectSlot objects[RINGL_OBJECT_SLOT_COUNT];
    RinGLBufferObject buffers[RINGL_OBJECT_SLOT_COUNT];
    RinGLTextureObject textures[RINGL_OBJECT_SLOT_COUNT];
    RinGLFramebufferObject framebuffers[RINGL_OBJECT_SLOT_COUNT];
    RinGLRenderbufferObject renderbuffers[RINGL_OBJECT_SLOT_COUNT];
    RinGLShaderObject shaders[RINGL_OBJECT_SLOT_COUNT];
    RinGLProgramObject programs[RINGL_OBJECT_SLOT_COUNT];
    RinGLVertexArrayState vertex_arrays[RINGL_OBJECT_SLOT_COUNT];
    RinGLVertexArrayState default_vertex_array;
    uint32_t array_buffer;
    uint32_t element_array_buffer;
    uint32_t vertex_array_binding;
    uint32_t active_texture_unit;
    uint32_t bound_texture_2d[RINGL_MAX_TEXTURE_UNITS];
    uint32_t framebuffer_binding;
    uint32_t renderbuffer_binding;
    uint32_t current_program;
    RinGLVertexAttribState vertex_attribs[RINGL_MAX_VERTEX_ATTRIBS];
};

void ringl_context_record_error(RinGLContext* context, uint32_t error);
void ringl_context_mark_lost(RinGLContext* context);
void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits);
void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits);
void ringl_copy_c_string(char* destination, size_t capacity,
                         const char* source);
int ringl_resolve_color_target(RinGLContext* context,
                               RinGLColorTarget* target);
int ringl_resolve_depth_target(RinGLContext* context,
                               RinGLDepthTarget* target);
int ringl_resolve_depth_stencil_targets(RinGLContext* context,
                                        RinGLDepthStencilTargets* targets);
int ringl_read_color_target_rgba(RinGLContext* context, int32_t x, int32_t y,
                                 int32_t width, int32_t height, void* pixels);

int ringl_backend_create_buffer(RinGLContext* context,
                                uint64_t size_bytes,
                                uint64_t* buffer_out);
int ringl_backend_upload_buffer(RinGLContext* context,
                                uint64_t buffer,
                                uint64_t offset,
                                const void* data,
                                uint64_t size_bytes);
int ringl_backend_create_sampled_image_2d(
    RinGLContext* context, const RinGLRinGpuSampledImage2DV1* desc,
    uint64_t* image_out);
int ringl_backend_create_image_2d(
    RinGLContext* context, const RinGLRinGpuImage2DV1* desc,
    uint64_t* image_out);
int ringl_backend_create_image_2d_mip_v2(
    RinGLContext* context, const RinGLRinGpuImage2DMipV2* desc,
    uint64_t* image_out);
int ringl_backend_upload_image_2d(
    RinGLContext* context, uint64_t image,
    const RinGLRinGpuImageUpload2DV1* upload,
    const void* data, uint64_t size_bytes);
int ringl_backend_upload_image_2d_mip_v2(
    RinGLContext* context, uint64_t image,
    const RinGLRinGpuImageUpload2DMipV2* upload,
    const void* data, uint64_t size_bytes);
int ringl_backend_create_sampler(RinGLContext* context,
                                 const RinGLRinGpuSamplerV1* desc,
                                 uint64_t* sampler_out);
int ringl_backend_create_shader_module(RinGLContext* context,
                                       const void* rsh1,
                                       uint64_t size_bytes,
                                       uint64_t* shader_module_out);
int ringl_backend_create_graphics_pipeline(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    uint64_t* pipeline_out);
int ringl_backend_create_graphics_pipeline_native(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
int ringl_backend_create_graphics_pipeline_native_v2(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
int ringl_backend_create_graphics_pipeline_vertex_bindings(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    uint64_t* pipeline_out);
int ringl_backend_create_graphics_pipeline_native_vertex_bindings(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
int ringl_backend_create_graphics_pipeline_native_vertex_bindings_v2(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out);
int ringl_backend_create_command_list(RinGLContext* context,
                                      uint32_t capabilities,
                                      uint64_t* command_list_out);
int ringl_backend_reset_command_list(RinGLContext* context,
                                     uint64_t command_list);
int ringl_backend_transition_image(RinGLContext* context,
                                   uint64_t command_list,
                                   uint64_t image,
                                    uint32_t old_state,
                                    uint32_t new_state);
int ringl_backend_transition_image_2d_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuImageTransition2DMipV2* transition);
int ringl_backend_begin_render_pass(RinGLContext* context,
                                    uint64_t command_list,
                                     const RinGLRinGpuRenderPassV1* render_pass);
int ringl_backend_begin_render_pass_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassMipV2* render_pass);
int ringl_backend_begin_render_pass_depth(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthV1* render_pass);
int ringl_backend_begin_render_pass_depth_stencil(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthStencilV1* render_pass);
int ringl_backend_begin_render_pass_depth_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthMipV2* render_pass);
int ringl_backend_begin_render_pass_depth_stencil_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthStencilMipV2* render_pass);
int ringl_backend_set_raster_state(RinGLContext* context,
                                   uint64_t command_list,
                                   const RinGLRinGpuRasterStateV1* state);
int ringl_backend_create_graphics_bind_group(
    RinGLContext* context, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV1* bindings,
    uint32_t binding_count, uint64_t* bind_group_out);
int ringl_backend_create_graphics_bind_group_v2(
    RinGLContext* context, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV2* bindings,
    uint32_t binding_count, uint64_t* bind_group_out);
int ringl_backend_bind_graphics_resources(RinGLContext* context,
                                          uint64_t command_list,
                                          uint64_t bind_group);
int ringl_backend_draw_vertices(RinGLContext* context,
                                uint64_t command_list,
                                const RinGLRinGpuDrawVerticesV1* draw);
int ringl_backend_draw_indexed(RinGLContext* context,
                               uint64_t command_list,
                               const RinGLRinGpuDrawIndexedV1* draw);
int ringl_backend_draw_vertices_v2(RinGLContext* context,
                                   uint64_t command_list,
                                   const RinGLRinGpuDrawVerticesV2* draw);
int ringl_backend_draw_indexed_v2(RinGLContext* context,
                                  uint64_t command_list,
                                   const RinGLRinGpuDrawIndexedV2* draw);
int ringl_backend_draw_vertices_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawVerticesMipV3* draw);
int ringl_backend_draw_vertices_bindings_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawVerticesBindingsMipV3* draw);
int ringl_backend_draw_indexed_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawIndexedMipV3* draw);
int ringl_backend_draw_indexed_bindings_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawIndexedBindingsMipV3* draw);
int ringl_backend_end_render_pass(RinGLContext* context,
                                  uint64_t command_list);
int ringl_backend_present(RinGLContext* context,
                          uint64_t command_list,
                          uint64_t image,
                          uint32_t display_id);
int ringl_backend_close_command_list(RinGLContext* context,
                                     uint64_t command_list);
int ringl_backend_queue_submit(RinGLContext* context,
                               uint64_t queue,
                               uint64_t command_list);
void ringl_backend_destroy_object(RinGLContext* context, uint64_t object);
void ringl_buffer_objects_destroy_all(RinGLContext* context);
void ringl_texture_objects_destroy_all(RinGLContext* context);
void ringl_framebuffer_detach_texture(RinGLContext* context, uint32_t texture);
void ringl_framebuffer_detach_renderbuffer(RinGLContext* context,
                                           uint32_t renderbuffer);
void ringl_framebuffer_objects_destroy_all(RinGLContext* context);
uint32_t ringl_framebuffer_operation_error(const RinGLContext* context);
int ringl_renderbuffer_realize_color_target(RinGLContext* context,
                                            uint32_t renderbuffer,
                                            uint64_t* image_out,
                                            uint32_t** image_state_out,
                                            uint32_t* width_out,
                                            uint32_t* height_out);
int ringl_renderbuffer_realize_depth_target(RinGLContext* context,
                                            uint32_t renderbuffer,
                                            uint64_t* image_out,
                                            uint32_t** image_state_out,
                                            uint32_t* width_out,
                                            uint32_t* height_out);
int ringl_texture_require_color_target(RinGLContext* context,
                                       uint32_t texture);
int ringl_texture_realize_color_target(RinGLContext* context,
                                        uint32_t texture,
                                        uint32_t mip_level,
                                        uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out);
int ringl_texture_realize_depth_target(RinGLContext* context,
                                       uint32_t texture,
                                       uint32_t mip_level,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out);
int ringl_texture_realize_unit(RinGLContext* context, uint32_t unit,
                               uint64_t* image_out, uint64_t* sampler_out);
uint32_t ringl_texture_sampled_mip_count(const RinGLTextureObject* texture);
void ringl_shader_objects_destroy_all(RinGLContext* context);
int ringl_shader_is_delete_pending(RinGLContext* context, uint32_t shader);
void ringl_shader_release_if_delete_pending(RinGLContext* context,
                                            uint32_t shader);
void ringl_program_objects_destroy_all(RinGLContext* context);
void ringl_pipeline_cache_destroy(RinGLContext* context);
void ringl_invalidate_graphics_artifacts(RinGLContext* context);
void ringl_vertex_array_detach_buffer(RinGLContext* context, uint32_t buffer);
int ringl_resolve_vertex_layout(const RinGLContext* context,
                                RinGLResolvedVertexLayout* layout);
int ringl_validate_vertex_fetch(const RinGLContext* context,
                                uint32_t first_vertex,
                                uint32_t vertex_count,
                                RinGLResolvedVertexLayout* layout);
int ringl_validate_vertex_fetch_instanced(const RinGLContext* context,
                                          uint32_t first_vertex,
                                          uint32_t vertex_count,
                                          uint32_t first_instance,
                                          uint32_t instance_count,
                                          RinGLResolvedVertexLayout* layout);
int ringl_validate_index_fetch(const RinGLContext* context,
                               uint32_t index_type,
                               uint64_t offset,
                               uint32_t count,
                               uint32_t* max_index_out);

#endif /* RINGL_INTERNAL_H */
