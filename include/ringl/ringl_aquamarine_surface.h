/* SPDX-License-Identifier: MIT */
#ifndef RINGL_AQUAMARINE_SURFACE_H
#define RINGL_AQUAMARINE_SURFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RinGL's Aquamarine embedding surface. It owns no window or global device:
 * the embedder supplies the caller-owned BGRA drawing-buffer storage. RinGL
 * is the sole producer of graphics commands for a live surface; Aquamarine
 * is only its presentation-memory sink.
 */
#define RINGL_AQUAMARINE_SURFACE_VERSION 1u
#define RINGL_AQUAMARINE_SURFACE_NATIVE_VERSION 1u
#define RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION 4096u
#define RINGL_AQUAMARINE_SURFACE_MAX_PITCH_BYTES \
    (RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION * 4u)

typedef enum RinGLAquamarineSurfaceResult {
    RINGL_AQUAMARINE_SURFACE_OK = 0,
    RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT = -1,
    RINGL_AQUAMARINE_SURFACE_NO_MEMORY = -2,
    RINGL_AQUAMARINE_SURFACE_STATE = -3,
    RINGL_AQUAMARINE_SURFACE_BACKEND = -4,
    /* A borrowed RinGPU device stopped accepting commands. The caller must
     * discard this bridge/surface pairing rather than retrying a state error. */
    RINGL_AQUAMARINE_SURFACE_DEVICE_LOST = -5,
} RinGLAquamarineSurfaceResult;

typedef struct RinGLAquamarineSurfaceTargetV1 {
    uint32_t struct_size;
    uint32_t version;
    uint8_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes;
    float* depth;
    uint32_t depth_pitch_floats;
    uint32_t reserved0;
    /* Optional caller-owned S8 plane. It requires a matching D32 plane and
     * uses a byte pitch because each stencil sample is one byte. */
    uint8_t* stencil;
    uint32_t stencil_pitch_bytes;
    uint32_t reserved1;
} RinGLAquamarineSurfaceTargetV1;

typedef struct RinGLAquamarineSurfaceNativeV1 {
    uint32_t struct_size;
    uint32_t version;
    void* ringpu_core;
    uint64_t graphics_queue;
    uint64_t color_image;
    uint32_t queue_capabilities;
    uint32_t color_format;
    uint32_t width;
    uint32_t height;
    uint32_t display_id;
    uint32_t color_state;
    uint32_t reserved0;
    uint64_t depth_image;
    uint32_t depth_format;
    uint32_t depth_state;
} RinGLAquamarineSurfaceNativeV1;

typedef struct RinGLAquamarineSurfaceContext RinGLAquamarineSurfaceContext;

int ringl_aquamarine_surface_create(
    const RinGLAquamarineSurfaceTargetV1* target,
    RinGLAquamarineSurfaceContext** context_out);
void ringl_aquamarine_surface_destroy(RinGLAquamarineSurfaceContext* context);

/* Prepares a freshly allocated target for RinGL's first submission. This is
 * an image-state setup operation, not a clear, draw, or presentation API. */
int ringl_aquamarine_surface_begin_content_update(
    RinGLAquamarineSurfaceContext* context);

/* Records framebuffer state after a successful RinGL submission. This private
 * embedding boundary never issues a draw or presentation command. */
int ringl_aquamarine_surface_sync_external_color_state(
    RinGLAquamarineSurfaceContext* context, uint32_t state);
int ringl_aquamarine_surface_sync_external_framebuffer_states(
    RinGLAquamarineSurfaceContext* context, uint32_t color_state,
    uint32_t depth_state);

/* Private RinGL embedding view. The returned core/image/queue remain owned by
 * the surface and are valid only while the surface lives. */
int ringl_aquamarine_surface_get_native(
    RinGLAquamarineSurfaceContext* context,
    RinGLAquamarineSurfaceNativeV1* native_out);

#ifdef __cplusplus
}
#endif

#endif /* RINGL_AQUAMARINE_SURFACE_H */
