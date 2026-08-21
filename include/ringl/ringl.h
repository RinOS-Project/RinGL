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
 * Embedding boundary for RinGPU integration.
 *
 * RinGL deliberately does not own a global GPU device. The embedding runtime
 * supplies an opaque session plus a RinGPU graphics queue handle/capabilities.
 * The opaque session is never interpreted by the context core; backend code
 * will use it when the command translation layer is added.
 */
typedef struct RinGLRinGpuBindingV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* session;
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

#ifdef __cplusplus
}
#endif

#endif /* RINGL_RINGL_H */
