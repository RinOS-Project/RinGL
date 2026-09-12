/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local RinGLContext* ringl_current_context;

#define RINGL_TRACE_MAGIC UINT64_C(0x52494e4c54524331)

typedef struct RinGLTraceState {
    uint64_t magic;
    uint64_t next_sequence;
    uint64_t dropped_count;
    uint32_t event_count;
    uint32_t reserved0;
    uint64_t event_counts[RINGL_TRACE_EVENT_TYPE_COUNT];
    RinGLTraceEventV1 events[RINGL_TRACE_MAX_EVENTS];
} RinGLTraceState;

static RinGLTraceState* ringl_trace_state(RinGLTraceRuntimeV1* runtime)
{
    return runtime != NULL ? (RinGLTraceState*)runtime->opaque : NULL;
}

static const RinGLTraceState* ringl_trace_state_const(
    const RinGLTraceRuntimeV1* runtime)
{
    return runtime != NULL ? (const RinGLTraceState*)runtime->opaque : NULL;
}

static int ringl_trace_overlaps_runtime(const RinGLTraceRuntimeV1* runtime,
                                        const void* pointer, size_t size)
{
    uintptr_t first;
    uintptr_t candidate;

    if (runtime == NULL || pointer == NULL || size == 0u)
        return 0;
    first = (uintptr_t)runtime;
    candidate = (uintptr_t)pointer;
    if (size > UINTPTR_MAX - first || size > UINTPTR_MAX - candidate)
        return 1;
    return candidate < first + sizeof(*runtime) &&
           first < candidate + size;
}

int ringl_trace_runtime_init(RinGLTraceRuntimeV1* runtime)
{
    RinGLTraceState* state;

    if (runtime == NULL)
        return -1;
    memset(runtime, 0, sizeof(*runtime));
    state = ringl_trace_state(runtime);
    state->magic = RINGL_TRACE_MAGIC;
    state->next_sequence = 1u;
    return 0;
}

int ringl_trace_runtime_is_initialized(const RinGLTraceRuntimeV1* runtime)
{
    const RinGLTraceState* state = ringl_trace_state_const(runtime);
    return state != NULL && state->magic == RINGL_TRACE_MAGIC &&
           state->next_sequence != 0u;
}

int ringl_trace_runtime_record(RinGLTraceRuntimeV1* runtime, uint32_t type,
                               int32_t status, uint64_t value0,
                               uint64_t value1)
{
    RinGLTraceState* state = ringl_trace_state(runtime);
    RinGLTraceEventV1* event;
    uint32_t index;

    if (!ringl_trace_runtime_is_initialized(runtime) ||
        type < RINGL_TRACE_CALL_SUMMARY ||
        type > RINGL_TRACE_CONTEXT_LOSS)
        return -1;
    if (state->next_sequence == UINT64_MAX)
        return -2;

    index = (uint32_t)((state->next_sequence - 1u) %
                       RINGL_TRACE_MAX_EVENTS);
    if (state->event_count == RINGL_TRACE_MAX_EVENTS)
        ++state->dropped_count;
    else
        ++state->event_count;

    event = &state->events[index];
    memset(event, 0, sizeof(*event));
    event->struct_size = sizeof(*event);
    event->version = RINGL_TRACE_VERSION;
    event->type = type;
    event->status = status;
    event->sequence = state->next_sequence++;
    event->value0 = value0;
    event->value1 = value1;
    ++state->event_counts[type - 1u];
    return 0;
}

int ringl_trace_runtime_read(RinGLTraceRuntimeV1* runtime, uint64_t cursor,
                             uint32_t capacity, RinGLTraceEventV1* events_out,
                             uint32_t* event_count_out,
                             uint64_t* next_cursor_out)
{
    RinGLTraceState* state = ringl_trace_state(runtime);
    uint64_t oldest;
    uint64_t available;
    uint32_t count;

    if (event_count_out != NULL)
        *event_count_out = 0u;
    if (next_cursor_out != NULL)
        *next_cursor_out = cursor;
    if (!ringl_trace_runtime_is_initialized(runtime) ||
        event_count_out == NULL || next_cursor_out == NULL ||
        (capacity != 0u && events_out == NULL) ||
#if UINTPTR_MAX <= UINT32_MAX
        capacity > SIZE_MAX / sizeof(*events_out) ||
#endif
        ringl_trace_overlaps_runtime(runtime, events_out,
                                     sizeof(*events_out) * (size_t)capacity))
        return -1;

    oldest = state->next_sequence - state->event_count;
    if (cursor == 0u)
        cursor = oldest;
    if (cursor < oldest || cursor > state->next_sequence) {
        *next_cursor_out = oldest;
        return -2;
    }

    available = state->next_sequence - cursor;
    if (available == 0u) {
        *next_cursor_out = cursor;
        return 0;
    }
    if (capacity == 0u)
        return -4;

    count = available > capacity ? capacity : (uint32_t)available;
    for (uint32_t index = 0u; index < count; ++index) {
        uint64_t sequence = cursor + index;
        events_out[index] = state->events[(sequence - 1u) %
                                          RINGL_TRACE_MAX_EVENTS];
    }
    *event_count_out = count;
    *next_cursor_out = cursor + count;
    return available > capacity ? -3 : 0;
}

void ringl_copy_c_string(char* destination, size_t capacity,
                         const char* source)
{
    size_t length = 0u;

    if (destination == NULL || capacity == 0u)
        return;
    if (source != NULL) {
        while (length + 1u < capacity && source[length] != '\0')
            ++length;
        if (length != 0u)
            memmove(destination, source, length);
    }
    destination[length] = '\0';
}

static int ringl_context_is_valid(const RinGLContext* context)
{
    return context != NULL && context->magic == RINGL_CONTEXT_MAGIC;
}

static int ringl_validate_ringpu_ops(const RinGLRinGpuOpsV1* ops)
{
    const size_t minimum_size = offsetof(RinGLRinGpuOpsV1,
                                         create_shader_module);

    if (ops == NULL)
        return 1;
    if (ops->struct_size < minimum_size ||
        ops->api_version != RINGL_API_VERSION ||
        ops->create_buffer == NULL ||
        ops->upload_buffer == NULL ||
        ops->destroy_object == NULL) {
        return 0;
    }
    return 1;
}

static int ringl_validate_ringpu_binding(const RinGLRinGpuBindingV1* binding)
{
    if (binding == NULL)
        return 1;

    if (binding->struct_size < sizeof(*binding))
        return 0;
    if (binding->api_version != RINGL_API_VERSION)
        return 0;
    if (binding->reserved0 != 0u)
        return 0;
    if (!ringl_validate_ringpu_ops(binding->ops))
        return 0;

    return 1;
}

int ringl_context_create(const RinGLContextDescV1* desc,
                         RinGLContext** context_out)
{
    RinGLContext* context;

    if (context_out == NULL)
        return -1;
    *context_out = NULL;

    if (desc != NULL) {
        if (desc->struct_size < offsetof(RinGLContextDescV1, device_generation) ||
            desc->api_version != RINGL_API_VERSION ||
            desc->reserved0 != 0u ||
            desc->flags != 0u ||
            !ringl_validate_ringpu_binding(desc->ringpu) ||
            (desc->trace != NULL &&
             !ringl_trace_runtime_is_initialized(desc->trace))) {
            return -1;
        }
    }

    context = calloc(1, sizeof(*context));
    if (context == NULL)
        return -2;

    context->magic = RINGL_CONTEXT_MAGIC;
    context->pending_error = RINGL_NO_ERROR;
    context->device_generation =
        desc != NULL && desc->struct_size >= sizeof(*desc)
            ? desc->device_generation
            : 0u;
    context->dirty_bits = RINGL_DIRTY_ALL;
    context->trace = desc != NULL ? desc->trace : NULL;
    context->cull_face_mode = RINGL_BACK;
    /* OpenGL ES 2.0 enables DITHER at context creation. */
    context->dither_enabled = RINGL_TRUE;
    context->front_face = RINGL_CCW;
    context->depth_func = RINGL_LESS;
    context->depth_write_mask = RINGL_TRUE;
    context->depth_range_near = 0.0f;
    context->depth_range_far = 1.0f;
    context->line_width = 1.0f;
    context->sample_coverage_value = 1.0f;
    context->fragment_shader_derivative_hint = RINGL_DONT_CARE;
    context->default_draw_buffer = RINGL_BACK;
    context->clear_depth = 1.0f;
    context->stencil_func = RINGL_ALWAYS;
    context->stencil_value_mask = 0xffu;
    context->stencil_write_mask = 0xffu;
    context->stencil_fail_operation = RINGL_KEEP;
    context->stencil_depth_fail_operation = RINGL_KEEP;
    context->stencil_pass_operation = RINGL_KEEP;
    context->back_stencil_func = RINGL_ALWAYS;
    context->back_stencil_value_mask = 0xffu;
    context->back_stencil_write_mask = 0xffu;
    context->back_stencil_fail_operation = RINGL_KEEP;
    context->back_stencil_depth_fail_operation = RINGL_KEEP;
    context->back_stencil_pass_operation = RINGL_KEEP;
    context->blend_source_rgb = RINGL_ONE;
    context->blend_destination_rgb = RINGL_ZERO;
    context->blend_source_alpha = RINGL_ONE;
    context->blend_destination_alpha = RINGL_ZERO;
    context->blend_equation_rgb = RINGL_FUNC_ADD;
    context->blend_equation_alpha = RINGL_FUNC_ADD;
    context->color_write_mask = 0x0fu;
    context->unpack_alignment = 4u;
    context->pack_alignment = 4u;
    for (uint32_t index = 0u; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
        context->vertex_attribs[index].size = 4u;
        context->vertex_attribs[index].type = RINGL_FLOAT;
        context->vertex_attribs[index].current_value[3] = 1.0f;
        context->default_vertex_array.vertex_attribs[index].size = 4u;
        context->default_vertex_array.vertex_attribs[index].type = RINGL_FLOAT;
    }

    if (desc != NULL) {
        if (desc->ringpu != NULL) {
            memcpy(&context->ringpu, desc->ringpu, sizeof(context->ringpu));
            context->has_ringpu = 1;
            if (desc->ringpu->ops != NULL) {
                size_t copy_size = desc->ringpu->ops->struct_size;
                if (copy_size > sizeof(context->ringpu_ops))
                    copy_size = sizeof(context->ringpu_ops);
                memset(&context->ringpu_ops, 0, sizeof(context->ringpu_ops));
                memcpy(&context->ringpu_ops, desc->ringpu->ops, copy_size);
                context->ringpu.ops = &context->ringpu_ops;
                context->has_ringpu_ops = 1;
            }
        }
    }

    *context_out = context;
    return 0;
}

void ringl_context_destroy(RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return;

    if (ringl_current_context == context)
        ringl_current_context = NULL;

    if (context->graphics_command_list != 0u) {
        ringl_backend_destroy_object(context, context->graphics_command_list);
        context->graphics_command_list = 0u;
    }
    if (context->graphics_bind_group != 0u) {
        ringl_backend_destroy_object(context, context->graphics_bind_group);
        context->graphics_bind_group = 0u;
    }
    if (context->finish_fence != 0u) {
        ringl_backend_destroy_object(context, context->finish_fence);
        context->finish_fence = 0u;
    }
    ringl_pipeline_cache_destroy(context);
    ringl_program_objects_destroy_all(context);
    ringl_shader_objects_destroy_all(context);
    ringl_framebuffer_objects_destroy_all(context);
    ringl_texture_objects_destroy_all(context);
    ringl_buffer_objects_destroy_all(context);
    for (uint32_t index = 0u; index < RINGL_STAGING_CACHE_BLOCK_COUNT;
         ++index)
        free(context->staging_blocks[index].memory);
    context->magic = 0u;
    memset(&context->sync_ops, 0, sizeof(context->sync_ops));
    memset(&context->ringpu_ops, 0, sizeof(context->ringpu_ops));
    memset(&context->ringpu, 0, sizeof(context->ringpu));
    free(context);
}

int ringl_make_current(RinGLContext* context)
{
    if (context != NULL && !ringl_context_is_valid(context))
        return -1;

    ringl_current_context = context;
    return context != NULL && context->lost ? -1 : 0;
}

RinGLContext* ringl_get_current_context(void)
{
    if (!ringl_context_is_valid(ringl_current_context) ||
        ringl_current_context->lost)
        return NULL;
    return ringl_current_context;
}

uint32_t ringl_get_error(void)
{
    RinGLContext* context = ringl_current_context;
    uint32_t error;

    if (!ringl_context_is_valid(context))
        return RINGL_NO_ERROR;

    if (context->lost) {
        if (context->loss_reported)
            return RINGL_NO_ERROR;
        context->loss_reported = 1u;
        return RINGL_CONTEXT_LOST_WEBGL;
    }

    error = context->pending_error;
    context->pending_error = RINGL_NO_ERROR;
    return error;
}

uint32_t ringl_context_is_lost(const RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return RINGL_FALSE;
    return context->lost ? RINGL_TRUE : RINGL_FALSE;
}

uint32_t ringl_context_dirty_bits(const RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return 0u;
    return context->dirty_bits;
}

int ringl_context_observe_device_generation(RinGLContext* context,
                                            uint64_t device_generation)
{
    if (!ringl_context_is_valid(context) || device_generation == 0u)
        return -1;
    if (context->lost)
        return -2;
    if (context->device_generation == 0u) {
        context->device_generation = device_generation;
        return 0;
    }
    if (context->device_generation == device_generation)
        return 0;

    ringl_context_mark_lost(context);
    return -2;
}

static void ringl_context_trim_staging_cache(RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return;
    for (uint32_t index = 0u; index < RINGL_STAGING_CACHE_BLOCK_COUNT;
         ++index) {
        RinGLStagingBlock* block = &context->staging_blocks[index];
        if (block->memory == NULL || block->in_use)
            continue;
        free(block->memory);
        if (block->capacity >= context->staging_cached_bytes)
            context->staging_cached_bytes = 0u;
        else
            context->staging_cached_bytes -= block->capacity;
        memset(block, 0, sizeof(*block));
    }
}

void ringl_context_record_error(RinGLContext* context, uint32_t error)
{
    if (!ringl_context_is_valid(context) || context->lost ||
        error == RINGL_NO_ERROR)
        return;

    if (context->pending_error == RINGL_NO_ERROR)
        context->pending_error = error;
}

void ringl_context_trace(RinGLContext* context, uint32_t type,
                         uint64_t value0, uint64_t value1, int32_t status)
{
    if (!ringl_context_is_valid(context) || context->trace == NULL)
        return;
    (void)ringl_trace_runtime_record(context->trace, type, status, value0,
                                      value1);
}

void ringl_context_mark_lost(RinGLContext* context)
{
    if (!ringl_context_is_valid(context) || context->lost)
        return;

    ringl_context_trace(context, RINGL_TRACE_CONTEXT_LOSS, 0u, 0u,
                        RINGL_CONTEXT_LOST_WEBGL);
    context->lost = 1u;
    context->loss_reported = 0u;
    context->pending_error = RINGL_NO_ERROR;
}

void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits)
{
    if (!ringl_context_is_valid(context))
        return;
    context->dirty_bits |= bits & RINGL_DIRTY_ALL;
}

void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits)
{
    if (!ringl_context_is_valid(context))
        return;
    context->dirty_bits &= ~(bits & RINGL_DIRTY_ALL);
}

int ringl_context_reserve_shadow_bytes(RinGLContext* context,
                                       uint64_t bytes)
{
    if (!ringl_context_is_valid(context) ||
        bytes > RINGL_MAX_CPU_SHADOW_BYTES)
        return 0;
    if (context->staging_cached_bytes <= RINGL_MAX_CPU_SHADOW_BYTES -
                                            context->cpu_shadow_bytes &&
        bytes <= RINGL_MAX_CPU_SHADOW_BYTES - context->cpu_shadow_bytes -
                     context->staging_cached_bytes) {
        context->cpu_shadow_bytes += bytes;
        return 1;
    }
    /* Cached readback memory is reclaimable. Release it before rejecting a
     * real persistent allocation so the budget remains a hard total bound. */
    ringl_context_trim_staging_cache(context);
    if (context->cpu_shadow_bytes > RINGL_MAX_CPU_SHADOW_BYTES - bytes)
        return 0;
    context->cpu_shadow_bytes += bytes;
    return 1;
}

void ringl_context_release_shadow_bytes(RinGLContext* context,
                                         uint64_t bytes)
{
    if (!ringl_context_is_valid(context))
        return;
    /* Never wrap the accounting counter on an internal ownership mismatch. */
    if (bytes >= context->cpu_shadow_bytes)
        context->cpu_shadow_bytes = 0u;
    else
        context->cpu_shadow_bytes -= bytes;
}

void* ringl_context_alloc_temporary(RinGLContext* context, uint64_t bytes)
{
    void* memory;

    if (bytes == 0u || bytes > SIZE_MAX ||
        !ringl_context_reserve_shadow_bytes(context, bytes))
        return NULL;
    memory = malloc((size_t)bytes);
    if (memory == NULL)
        ringl_context_release_shadow_bytes(context, bytes);
    return memory;
}

void ringl_context_free_temporary(RinGLContext* context, void* memory,
                                  uint64_t bytes)
{
    if (memory == NULL)
        return;
    ringl_context_release_shadow_bytes(context, bytes);
    free(memory);
}

void* ringl_context_alloc_staging(RinGLContext* context, uint64_t bytes)
{
    RinGLStagingBlock* block = NULL;
    void* memory;

    if (!ringl_context_is_valid(context) || bytes == 0u || bytes > SIZE_MAX)
        return NULL;
    for (uint32_t index = 0u; index < RINGL_STAGING_CACHE_BLOCK_COUNT;
         ++index) {
        RinGLStagingBlock* candidate = &context->staging_blocks[index];
        if (candidate->memory != NULL && !candidate->in_use &&
            candidate->capacity >= bytes) {
            block = candidate;
            break;
        }
    }
    if (block != NULL) {
        const uint64_t capacity = block->capacity;
        context->staging_cached_bytes -= capacity;
        block->in_use = 1u;
        if (!ringl_context_reserve_shadow_bytes(context, capacity)) {
            block->in_use = 0u;
            context->staging_cached_bytes += capacity;
            return NULL;
        }
        return block->memory;
    }

    if (!ringl_context_reserve_shadow_bytes(context, bytes))
        return NULL;
    memory = malloc((size_t)bytes);
    if (memory == NULL) {
        ringl_context_release_shadow_bytes(context, bytes);
        return NULL;
    }
    if (bytes <= RINGL_STAGING_CACHE_MAX_BLOCK_BYTES) {
        for (uint32_t index = 0u; index < RINGL_STAGING_CACHE_BLOCK_COUNT;
             ++index) {
            RinGLStagingBlock* candidate = &context->staging_blocks[index];
            if (candidate->memory == NULL) {
                candidate->memory = memory;
                candidate->capacity = bytes;
                candidate->in_use = 1u;
                return memory;
            }
        }
    }
    return memory;
}

void ringl_context_free_staging(RinGLContext* context, void* memory,
                                uint64_t bytes)
{
    if (memory == NULL)
        return;
    if (ringl_context_is_valid(context)) {
        for (uint32_t index = 0u; index < RINGL_STAGING_CACHE_BLOCK_COUNT;
             ++index) {
            RinGLStagingBlock* block = &context->staging_blocks[index];
            if (block->memory != memory)
                continue;
            if (!block->in_use)
                return;
            if (block->capacity <= RINGL_STAGING_CACHE_MAX_BLOCK_BYTES) {
                if (context->staging_cached_bytes + block->capacity >
                    RINGL_STAGING_CACHE_MAX_BYTES)
                    ringl_context_trim_staging_cache(context);
                if (context->staging_cached_bytes + block->capacity <=
                    RINGL_STAGING_CACHE_MAX_BYTES) {
                    ringl_context_release_shadow_bytes(context,
                                                       block->capacity);
                    context->staging_cached_bytes += block->capacity;
                    block->in_use = 0u;
                    return;
                }
            }
            ringl_context_release_shadow_bytes(context, block->capacity);
            free(block->memory);
            memset(block, 0, sizeof(*block));
            return;
        }
        ringl_context_release_shadow_bytes(context, bytes);
    }
    free(memory);
}
