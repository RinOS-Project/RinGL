/* SPDX-License-Identifier: MIT */
#ifndef RINGL_RINGL_H
#define RINGL_RINGL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RINGL_API_VERSION 1u

/* OpenGL-compatible error values used by the initial RinGL context core. */
#define RINGL_NO_ERROR          0x0000u
#define RINGL_INVALID_ENUM      0x0500u
#define RINGL_INVALID_VALUE     0x0501u
#define RINGL_INVALID_OPERATION 0x0502u
#define RINGL_OUT_OF_MEMORY     0x0505u

/* Initial OpenGL ES buffer targets and usage hints. */
#define RINGL_ARRAY_BUFFER          0x8892u
#define RINGL_ELEMENT_ARRAY_BUFFER  0x8893u
#define RINGL_STREAM_DRAW           0x88e0u
#define RINGL_STATIC_DRAW           0x88e4u
#define RINGL_DYNAMIC_DRAW          0x88e8u

/* Context-side derived state. These bits are internal policy made observable
 * only for diagnostics/tests; ordinary GL state changes should set them rather
 * than immediately emitting RinGPU commands. */
#define RINGL_DIRTY_PIPELINE    0x00000001u
#define RINGL_DIRTY_BINDINGS    0x00000002u
#define RINGL_DIRTY_FRAMEBUFFER 0x00000004u
#define RINGL_DIRTY_VIEWPORT    0x00000008u
#define RINGL_DIRTY_ALL         0x0000000fu

typedef struct RinGLContext RinGLContext;

/*
 * Thin embedding callbacks for operations that require host integration around
 * RinGPU. The OS-specific adapter may implement uploads with a staging buffer,
 * command list, copy command, queue submit, and fence as appropriate.
 */
typedef int (*RinGLRinGpuCreateBufferFn)(void* session,
                                         uint64_t size_bytes,
                                         uint64_t* buffer_out);
typedef int (*RinGLRinGpuUploadBufferFn)(void* session,
                                         uint64_t buffer,
                                         uint64_t offset,
                                         const void* data,
                                         uint64_t size_bytes);
typedef int (*RinGLRinGpuDestroyObjectFn)(void* session, uint64_t object);

typedef struct RinGLRinGpuOpsV1 {
    uint32_t struct_size;
    uint32_t api_version;
    RinGLRinGpuCreateBufferFn create_buffer;
    RinGLRinGpuUploadBufferFn upload_buffer;
    RinGLRinGpuDestroyObjectFn destroy_object;
} RinGLRinGpuOpsV1;

/*
 * Embedding boundary for RinGPU integration.
 *
 * RinGL deliberately does not own a global GPU device. The embedding runtime
 * supplies an opaque session, operation table, and a selected graphics queue.
 * The context copies the v1 operation table during creation.
 */
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

int ringl_context_create(const RinGLContextDescV1* desc,
                         RinGLContext** context_out);
void ringl_context_destroy(RinGLContext* context);

/* One current RinGL context per thread. Passing NULL clears the binding. */
int ringl_make_current(RinGLContext* context);
RinGLContext* ringl_get_current_context(void);

/* GL-style sticky error state: the first pending error wins until consumed. */
uint32_t ringl_get_error(void);

uint32_t ringl_context_dirty_bits(const RinGLContext* context);

/* Initial buffer-object namespace. Generated names are reserved until first
 * bind, matching GL's distinction between a generated name and a live object. */
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

#ifdef __cplusplus
}
#endif

#endif /* RINGL_RINGL_H */
