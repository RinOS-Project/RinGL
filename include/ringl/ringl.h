/* SPDX-License-Identifier: MIT */
#ifndef RINGL_RINGL_H
#define RINGL_RINGL_H

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

#define RINGL_FALSE 0u
#define RINGL_TRUE  1u
#define RINGL_UNSIGNED_BYTE  0x1401u
#define RINGL_UNSIGNED_SHORT 0x1403u
#define RINGL_UNSIGNED_INT   0x1405u
#define RINGL_FLOAT          0x1406u

#define RINGL_TRIANGLES        0x0004u
#define RINGL_COLOR_BUFFER_BIT 0x00004000u

#define RINGL_ARRAY_BUFFER          0x8892u
#define RINGL_ELEMENT_ARRAY_BUFFER  0x8893u
#define RINGL_STREAM_DRAW           0x88e0u
#define RINGL_STATIC_DRAW           0x88e4u
#define RINGL_DYNAMIC_DRAW          0x88e8u

#define RINGL_TEXTURE_2D 0x0de1u
#define RINGL_TEXTURE0   0x84c0u
#define RINGL_RGBA       0x1908u
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

#define RINGL_VERTEX_SHADER   0x8b31u
#define RINGL_FRAGMENT_SHADER 0x8b30u

#define RINGL_MAX_VERTEX_ATTRIBS 16u

#define RINGL_DIRTY_PIPELINE    0x00000001u
#define RINGL_DIRTY_BINDINGS    0x00000002u
#define RINGL_DIRTY_FRAMEBUFFER 0x00000004u
#define RINGL_DIRTY_VIEWPORT    0x00000008u
#define RINGL_DIRTY_ALL         0x0000000fu

#define RINGL_RIN_GPU_QUEUE_GRAPHICS       0x00000004u
#define RINGL_RIN_GPU_IMAGE_UNDEFINED      0u
#define RINGL_RIN_GPU_IMAGE_COLOR_TARGET   3u
#define RINGL_RIN_GPU_IMAGE_PRESENT        4u
#define RINGL_RIN_GPU_IMAGE_SHADER_READ    6u
#define RINGL_RIN_GPU_RENDER_LOAD          1u
#define RINGL_RIN_GPU_RENDER_CLEAR         2u
#define RINGL_RIN_GPU_RENDER_STORE         1u
#define RINGL_RIN_GPU_INDEX_UINT16         1u
#define RINGL_RIN_GPU_INDEX_UINT32         2u
#define RINGL_RIN_GPU_FORMAT_RGBA8_UNORM   2u
#define RINGL_RIN_GPU_SAMPLER_NEAREST      1u
#define RINGL_RIN_GPU_SAMPLER_LINEAR       2u
#define RINGL_RIN_GPU_ADDRESS_CLAMP        1u
#define RINGL_RIN_GPU_ADDRESS_REPEAT       2u
#define RINGL_RIN_GPU_ADDRESS_MIRRORED     3u

typedef struct RinGLContext RinGLContext;

typedef struct RinGLRinGpuVertexAttributeV1 {
    uint32_t location;
    uint32_t format;
    uint32_t offset;
    uint32_t reserved0;
} RinGLRinGpuVertexAttributeV1;

typedef struct RinGLRinGpuGraphicsPipelineV1 {
    uint64_t vertex_shader;
    uint64_t fragment_shader;
    uint32_t color_format;
    uint32_t primitive_topology;
    uint32_t vertex_stride;
    uint32_t attribute_count;
} RinGLRinGpuGraphicsPipelineV1;

typedef struct RinGLRinGpuRenderPassV1 {
    uint64_t color_target;
    uint32_t load_op;
    uint32_t store_op;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;
} RinGLRinGpuRenderPassV1;

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

typedef struct RinGLRinGpuSampledImage2DV1 {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t reserved0;
} RinGLRinGpuSampledImage2DV1;

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
typedef int (*RinGLRinGpuDrawVerticesFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawVerticesV1* draw);
typedef int (*RinGLRinGpuDrawIndexedFn)(
    void* session, uint64_t command_list,
    const RinGLRinGpuDrawIndexedV1* draw);
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
} RinGLRinGpuOpsV1;

typedef struct RinGLRinGpuBindingV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* session;
    const RinGLRinGpuOpsV1* ops;
    uint64_t graphics_queue;
    uint32_t queue_capabilities;
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
} RinGLDefaultFramebufferV1;

int ringl_context_create(const RinGLContextDescV1* desc,
                         RinGLContext** context_out);
void ringl_context_destroy(RinGLContext* context);
int ringl_make_current(RinGLContext* context);
RinGLContext* ringl_get_current_context(void);
uint32_t ringl_get_error(void);
uint32_t ringl_context_dirty_bits(const RinGLContext* context);

int ringl_set_default_framebuffer(const RinGLDefaultFramebufferV1* framebuffer);
int ringl_set_default_framebuffer_state(uint32_t state);
int ringl_get_default_framebuffer(RinGLDefaultFramebufferV1* framebuffer);
void ringl_clear_color(float red, float green, float blue, float alpha);
void ringl_clear(uint32_t mask);
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
void ringl_tex_sub_image_2d(uint32_t target, int32_t level,
                            int32_t xoffset, int32_t yoffset,
                            int32_t width, int32_t height,
                            uint32_t format, uint32_t type,
                            const void* pixels);

void ringl_enable_vertex_attrib_array(uint32_t index);
void ringl_disable_vertex_attrib_array(uint32_t index);
void ringl_vertex_attrib_pointer(uint32_t index,
                                 int32_t size,
                                 uint32_t type,
                                 uint32_t normalized,
                                 int32_t stride,
                                 uint64_t offset);
int ringl_get_vertex_attrib(uint32_t index, RinGLVertexAttribInfoV1* info);

uint32_t ringl_create_shader(uint32_t shader_type);
void ringl_delete_shader(uint32_t shader);
int ringl_is_shader(uint32_t shader);
void ringl_shader_source(uint32_t shader, const char* source, int64_t length);
void ringl_compile_shader(uint32_t shader);
uint32_t ringl_get_shader_compile_status(uint32_t shader);
uint32_t ringl_get_shader_type(uint32_t shader);
uint64_t ringl_get_shader_source_length(uint32_t shader);
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
void ringl_link_program(uint32_t program);
uint32_t ringl_get_program_link_status(uint32_t program);
uint64_t ringl_get_program_info_log(uint32_t program,
                                    char* buffer,
                                    uint64_t buffer_size);
void ringl_use_program(uint32_t program);
uint32_t ringl_get_current_program(void);
int32_t ringl_get_uniform_location(uint32_t program, const char* name);
void ringl_uniform_1i(int32_t location, int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* RINGL_RINGL_H */