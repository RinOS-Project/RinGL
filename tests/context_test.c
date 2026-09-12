/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include "ringl_internal.h"

#include <assert.h>

int main(void)
{
    static RinGLTraceRuntimeV1 trace;
    static RinGLTraceRuntimeV1 overflow_trace;
    RinGLTraceEventV1 trace_events[8];
    RinGLTraceEventV1 overflow_events[RINGL_TRACE_MAX_EVENTS];
    RinGLContext* first = NULL;
    RinGLContext* second = NULL;
    uint32_t texture;
    uint32_t shader;
    uint32_t program;
    uint8_t pixel[4] = {0xffu, 0x00u, 0x00u, 0xffu};
    uint32_t trace_count;
    uint64_t next_cursor;
    uint32_t seen[RINGL_TRACE_EVENT_TYPE_COUNT] = {0};
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .trace = &trace,
    };

    assert(ringl_trace_runtime_init(&trace) == 0);
    assert(ringl_trace_runtime_is_initialized(&trace) != 0);
    assert(ringl_trace_runtime_init(&overflow_trace) == 0);
    assert(ringl_context_create(&desc, &first) == 0);
    assert(ringl_context_create(&desc, &second) == 0);
    assert(first != NULL);
    assert(second != NULL);
    assert(first != second);
    assert(ringl_context_dirty_bits(first) == RINGL_DIRTY_ALL);

    assert(ringl_make_current(first) == 0);
    assert(ringl_get_current_context() == first);

    ringl_context_record_error(first, RINGL_INVALID_ENUM);
    ringl_context_record_error(first, RINGL_OUT_OF_MEMORY);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_clear_dirty(first, RINGL_DIRTY_PIPELINE);
    assert((ringl_context_dirty_bits(first) & RINGL_DIRTY_PIPELINE) == 0u);
    ringl_context_mark_dirty(first, RINGL_DIRTY_PIPELINE);
    assert((ringl_context_dirty_bits(first) & RINGL_DIRTY_PIPELINE) != 0u);

    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);
    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixel);
    shader = ringl_create_shader(RINGL_VERTEX_SHADER);
    ringl_compile_shader(shader);
    program = ringl_create_program();
    ringl_link_program(program);
    ringl_draw_arrays(RINGL_TRIANGLES, 0, 0);
    assert(ringl_trace_runtime_record(&trace, RINGL_TRACE_CALL_SUMMARY,
                                      -7, 42u, 84u) == 0);
    ringl_context_mark_lost(first);

    assert(ringl_trace_runtime_read(&trace, 0u, 8u, trace_events,
                                    &trace_count, &next_cursor) == 0);
    assert(trace_count == 7u);
    for (uint32_t index = 0u; index < trace_count; ++index) {
        assert(trace_events[index].struct_size == sizeof(RinGLTraceEventV1));
        assert(trace_events[index].version == RINGL_TRACE_VERSION);
        assert(trace_events[index].type >= RINGL_TRACE_CALL_SUMMARY);
        assert(trace_events[index].type <= RINGL_TRACE_CONTEXT_LOSS);
        seen[trace_events[index].type - 1u] = 1u;
    }
    for (uint32_t index = 0u; index < RINGL_TRACE_EVENT_TYPE_COUNT; ++index)
        assert(seen[index] != 0u);
    assert(next_cursor == 8u);
    assert(trace_events[trace_count - 1u].type == RINGL_TRACE_CONTEXT_LOSS);
    assert(trace_events[trace_count - 1u].status == RINGL_CONTEXT_LOST_WEBGL);

    for (uint32_t index = 0u; index < RINGL_TRACE_MAX_EVENTS + 3u; ++index)
        assert(ringl_trace_runtime_record(&overflow_trace,
                                          RINGL_TRACE_DRAW, (int32_t)index,
                                          index, index + 1u) == 0);
    assert(ringl_trace_runtime_read(&overflow_trace, 1u,
                                    RINGL_TRACE_MAX_EVENTS, overflow_events,
                                    &trace_count, &next_cursor) == -2);
    assert(next_cursor == 4u);
    assert(ringl_trace_runtime_read(&overflow_trace, 0u,
                                    RINGL_TRACE_MAX_EVENTS, overflow_events,
                                    &trace_count, &next_cursor) == 0);
    assert(trace_count == RINGL_TRACE_MAX_EVENTS);
    assert(overflow_events[0].sequence == 4u);
    assert(next_cursor == RINGL_TRACE_MAX_EVENTS + 4u);

    assert(ringl_make_current(second) == 0);
    assert(ringl_get_current_context() == second);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_destroy(second);
    assert(ringl_get_current_context() == NULL);

    ringl_context_destroy(first);
    return 0;
}
