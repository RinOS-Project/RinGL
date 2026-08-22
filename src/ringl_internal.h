/* SPDX-License-Identifier: MIT */
#ifndef RINGL_INTERNAL_H
#define RINGL_INTERNAL_H

#include <ringl/ringl.h>

#include "objects/object_table.h"

#define RINGL_CONTEXT_MAGIC 0x52474c43u /* RGLC */
#define RINGL_NATIVE_VERTEX_FLOAT32 3u
#define RINGL_SHADER_LOG_MAX 160u
#define RINGL_PROGRAM_LOG_MAX 160u

typedef struct RinGLBufferObject {
    uint64_t ringpu_handle;
    uint64_t size_bytes;
    uint8_t* shadow_bytes;
    uint32_t usage;
    uint32_t reserved0;
} RinGLBufferObject;

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
    uint32_t reserved0;
    char info_log[RINGL_SHADER_LOG_MAX];
} RinGLShaderObject;

typedef struct RinGLProgramObject {
    uint32_t vertex_shader;
    uint32_t fragment_shader;
    uint32_t link_status;
    uint32_t reserved0;
    char info_log[RINGL_PROGRAM_LOG_MAX];
} RinGLProgramObject;

typedef struct RinGLVertexAttribState {
    uint32_t enabled;
    uint32_t size;
    uint32_t type;
    uint32_t normalized;
    uint32_t stride;
    uint32_t buffer;
    uint64_t offset;
} RinGLVertexAttribState;

typedef struct RinGLResolvedVertexAttribute {
    uint32_t location;
    uint32_t format;
    uint32_t offset;
} RinGLResolvedVertexAttribute;

typedef struct RinGLResolvedVertexLayout {
    uint32_t buffer;
    uint32_t stride;
    uint32_t attribute_count;
    RinGLResolvedVertexAttribute attributes[RINGL_MAX_VERTEX_ATTRIBS];
} RinGLResolvedVertexLayout;

struct RinGLContext {
    uint32_t magic;
    uint32_t pending_error;
    uint32_t dirty_bits;
    uint32_t flags;
    RinGLRinGpuBindingV1 ringpu;
    RinGLRinGpuOpsV1 ringpu_ops;
    int has_ringpu;
    int has_ringpu_ops;
    void* pipeline_cache;
    RinGLDefaultFramebufferV1 default_framebuffer;
    uint32_t has_default_framebuffer;
    uint32_t default_framebuffer_state;
    uint64_t graphics_command_list;
    float clear_red;
    float clear_green;
    float clear_blue;
    float clear_alpha;

    RinGLObjectSlot objects[RINGL_OBJECT_SLOT_COUNT];
    RinGLBufferObject buffers[RINGL_OBJECT_SLOT_COUNT];
    RinGLShaderObject shaders[RINGL_OBJECT_SLOT_COUNT];
    RinGLProgramObject programs[RINGL_OBJECT_SLOT_COUNT];
    uint32_t array_buffer;
    uint32_t element_array_buffer;
    uint32_t current_program;
    RinGLVertexAttribState vertex_attribs[RINGL_MAX_VERTEX_ATTRIBS];
};

void ringl_context_record_error(RinGLContext* context, uint32_t error);
void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits);
void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits);

int ringl_backend_create_buffer(RinGLContext* context,
                                uint64_t size_bytes,
                                uint64_t* buffer_out);
int ringl_backend_upload_buffer(RinGLContext* context,
                                uint64_t buffer,
                                uint64_t offset,
                                const void* data,
                                uint64_t size_bytes);
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
int ringl_backend_begin_render_pass(RinGLContext* context,
                                    uint64_t command_list,
                                    const RinGLRinGpuRenderPassV1* render_pass);
int ringl_backend_draw_vertices(RinGLContext* context,
                                uint64_t command_list,
                                const RinGLRinGpuDrawVerticesV1* draw);
int ringl_backend_draw_indexed(RinGLContext* context,
                               uint64_t command_list,
                               const RinGLRinGpuDrawIndexedV1* draw);
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
void ringl_shader_objects_destroy_all(RinGLContext* context);
void ringl_program_objects_destroy_all(RinGLContext* context);
void ringl_pipeline_cache_destroy(RinGLContext* context);
void ringl_invalidate_graphics_artifacts(RinGLContext* context);
void ringl_vertex_attrib_detach_buffer(RinGLContext* context, uint32_t buffer);
int ringl_resolve_vertex_layout(const RinGLContext* context,
                                RinGLResolvedVertexLayout* layout);
int ringl_validate_vertex_fetch(const RinGLContext* context,
                                uint32_t first_vertex,
                                uint32_t vertex_count,
                                RinGLResolvedVertexLayout* layout);
int ringl_validate_index_fetch(const RinGLContext* context,
                               uint32_t index_type,
                               uint64_t offset,
                               uint32_t count,
                               uint32_t* max_index_out);

#endif /* RINGL_INTERNAL_H */
