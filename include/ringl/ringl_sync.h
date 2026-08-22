/* SPDX-License-Identifier: MIT */
#ifndef RINGL_RINGL_SYNC_H
#define RINGL_RINGL_SYNC_H

#include <ringl/ringl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RINGL_SYNC_API_VERSION 1u
#define RINGL_TIMEOUT_INFINITE UINT64_MAX
#define RINGL_RIN_GPU_IMAGE_COPY_SOURCE 1u

typedef struct RinGLRinGpuImageReadback2DV1 {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint64_t destination_row_pitch_bytes;
} RinGLRinGpuImageReadback2DV1;

typedef int (*RinGLRinGpuCreateFenceFn)(void* session,
                                        uint64_t initial_value,
                                        uint64_t* fence_out);
typedef int (*RinGLRinGpuQueueSubmitFencedFn)(void* session,
                                              uint64_t queue,
                                              uint64_t command_list,
                                              uint64_t signal_fence,
                                              uint64_t signal_value);
typedef int (*RinGLRinGpuWaitFenceFn)(void* session,
                                      uint64_t fence,
                                      uint64_t value,
                                      uint64_t timeout_ns);
typedef int (*RinGLRinGpuReadbackImage2DFn)(
    void* session, uint64_t image,
    const RinGLRinGpuImageReadback2DV1* readback,
    void* destination, uint64_t destination_size);

typedef struct RinGLRinGpuSyncOpsV1 {
    uint32_t struct_size;
    uint32_t api_version;
    RinGLRinGpuCreateFenceFn create_fence;
    RinGLRinGpuQueueSubmitFencedFn queue_submit_fenced;
    RinGLRinGpuWaitFenceFn wait_fence;
    RinGLRinGpuReadbackImage2DFn readback_image_2d;
} RinGLRinGpuSyncOpsV1;

/* Registers optional synchronization/readback operations for a context.
 * The operations use the same opaque session supplied by RinGLRinGpuBindingV1.
 * Passing NULL removes the extension. */
int ringl_context_set_sync_ops(RinGLContext* context,
                               const RinGLRinGpuSyncOpsV1* ops);

/* Current RinGL draw/clear operations submit immediately, so flush is an
 * ordering boundary without an additional native submission. */
void ringl_flush(void);

/* Blocks until all RinGPU graphics-queue work submitted before this call has
 * completed. */
void ringl_finish(void);

/* Initial GLES2 readback slice: the current complete color framebuffer,
 * RGBA/UNSIGNED_BYTE only. Destination rows are tightly packed. */
void ringl_read_pixels(int32_t x, int32_t y,
                       int32_t width, int32_t height,
                       uint32_t format, uint32_t type,
                       void* pixels);
/* Bounded readback for untrusted destination spans. The supported
 * RGBA/UNSIGNED_BYTE result is tightly packed, so pixels_size must cover
 * width * height * 4 bytes. A short destination records INVALID_OPERATION
 * before command submission or destination writes. The raw-pointer entry
 * point above remains a trusted native compatibility API. */
void ringl_read_pixels_to_bytes(int32_t x, int32_t y,
                                int32_t width, int32_t height,
                                uint32_t format, uint32_t type,
                                void* pixels, uint64_t pixels_size);

#ifdef __cplusplus
}
#endif

#endif /* RINGL_RINGL_SYNC_H */
