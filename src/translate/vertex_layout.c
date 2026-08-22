/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <string.h>

static uint32_t ringl_native_vertex_format(uint32_t type, uint32_t normalized,
                                           uint32_t* component_bytes)
{
    if (component_bytes == NULL)
        return 0u;
    if (type == RINGL_FLOAT) {
        *component_bytes = 4u;
        return RINGL_NATIVE_VERTEX_FLOAT32;
    }
    if (type == RINGL_BYTE) {
        *component_bytes = 1u;
        return normalized == RINGL_TRUE ? RINGL_NATIVE_VERTEX_SNORM8
                                        : RINGL_NATIVE_VERTEX_SINT8;
    }
    if (type == RINGL_UNSIGNED_BYTE) {
        *component_bytes = 1u;
        return normalized == RINGL_TRUE ? RINGL_NATIVE_VERTEX_UNORM8
                                        : RINGL_NATIVE_VERTEX_UINT8;
    }
    if (type == RINGL_SHORT) {
        *component_bytes = 2u;
        return normalized == RINGL_TRUE ? RINGL_NATIVE_VERTEX_SNORM16
                                        : RINGL_NATIVE_VERTEX_SINT16;
    }
    if (type == RINGL_UNSIGNED_SHORT) {
        *component_bytes = 2u;
        return normalized == RINGL_TRUE ? RINGL_NATIVE_VERTEX_UNORM16
                                        : RINGL_NATIVE_VERTEX_UINT16;
    }
    return 0u;
}

static uint32_t ringl_native_vertex_format_bytes(uint32_t format)
{
    if (format == RINGL_NATIVE_VERTEX_UINT8 ||
        format == RINGL_NATIVE_VERTEX_SINT8 ||
        format == RINGL_NATIVE_VERTEX_UNORM8 ||
        format == RINGL_NATIVE_VERTEX_SNORM8)
        return 1u;
    if (format == RINGL_NATIVE_VERTEX_UINT16 ||
        format == RINGL_NATIVE_VERTEX_SINT16 ||
        format == RINGL_NATIVE_VERTEX_UNORM16 ||
        format == RINGL_NATIVE_VERTEX_SNORM16)
        return 2u;
    if (format == RINGL_NATIVE_VERTEX_UINT32 ||
        format == RINGL_NATIVE_VERTEX_SINT32 ||
        format == RINGL_NATIVE_VERTEX_FLOAT32)
        return 4u;
    return 0u;
}

static int ringl_append_vertex_attrib(
    const RinGLContext* context, const RinGLVertexAttribState* attrib,
    uint32_t component_count, uint32_t* common_buffer,
    uint32_t* common_stride, uint32_t* scalar_location,
    RinGLResolvedVertexLayout* layout)
{
    uint32_t effective_stride;
    uint32_t component_bytes;
    uint32_t format;
    uint32_t component;

    if (context == NULL || attrib == NULL || component_count == 0u ||
        common_buffer == NULL || common_stride == NULL ||
        scalar_location == NULL || layout == NULL ||
        layout->attribute_count + component_count >
            RINGL_MAX_VERTEX_INPUT_COMPONENTS) {
        return -1;
    }
    if (!attrib->enabled) {
        /* Disabled generic arrays source their current floating-point values.
         * RSH1 is scalar, so materialize the requested vector components as
         * independent constant inputs in the RinGPU layout. */
        for (component = 0u; component < component_count; ++component) {
            RinGLResolvedVertexAttribute* resolved =
                &layout->attributes[layout->attribute_count++];
            resolved->location = (*scalar_location)++;
            resolved->format = RINGL_NATIVE_VERTEX_FLOAT32;
            memcpy(&resolved->offset, &attrib->current_value[component],
                   sizeof(resolved->offset));
            resolved->flags =
                RINGL_RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32;
        }
        layout->has_constant_attributes = RINGL_TRUE;
        return 0;
    }
    if (component_count > attrib->size || attrib->buffer == 0u ||
        (attrib->size != 1u && attrib->size != 2u && attrib->size != 3u &&
         attrib->size != 4u) ||
        (attrib->normalized != RINGL_FALSE &&
         attrib->normalized != RINGL_TRUE)) {
        return -1;
    }
    format = ringl_native_vertex_format(attrib->type, attrib->normalized,
                                        &component_bytes);
    if (format == 0u ||
        ringl_object_lookup_const(context, attrib->buffer,
                                  RINGL_OBJECT_BUFFER) == NULL ||
        attrib->offset > UINT32_MAX ||
        (uint64_t)attrib->offset +
                (uint64_t)component_count * component_bytes > UINT32_MAX) {
        return -1;
    }

    effective_stride = attrib->stride == 0u
        ? attrib->size * component_bytes : attrib->stride;
    if (effective_stride > 2048u ||
        effective_stride < attrib->size * component_bytes) {
        return -1;
    }
    if (*common_buffer == 0u) {
        *common_buffer = attrib->buffer;
        *common_stride = effective_stride;
    } else if (*common_buffer != attrib->buffer ||
               *common_stride != effective_stride) {
        return -1;
    }

    for (component = 0u; component < component_count; ++component) {
        RinGLResolvedVertexAttribute* resolved =
            &layout->attributes[layout->attribute_count++];
        resolved->location = (*scalar_location)++;
        resolved->format = format;
        resolved->offset = (uint32_t)attrib->offset +
            component * component_bytes;
        resolved->flags = 0u;
    }
    return 0;
}

static const RinGLProgramObject* ringl_layout_current_program(
    const RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->current_program == 0u ||
        ringl_object_lookup_const(context, context->current_program,
                                  RINGL_OBJECT_PROGRAM) == NULL) {
        return NULL;
    }
    index = ringl_object_slot_index(context->current_program);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->programs[index];
}

int ringl_resolve_vertex_layout(const RinGLContext* context,
                                RinGLResolvedVertexLayout* layout)
{
    const RinGLProgramObject* program;
    uint32_t index;
    uint32_t common_buffer = 0u;
    uint32_t common_stride = 0u;
    uint32_t scalar_location = 0u;

    if (context == NULL || layout == NULL)
        return -1;
    memset(layout, 0, sizeof(*layout));

    program = ringl_layout_current_program(context);
    if (context->current_program != 0u &&
        (program == NULL || !program->link_status)) {
        return -1;
    }
    if (program != NULL) {
        /* RSH1 uses dense scalar input locations. A linked generic GL index
         * selects the source pointer; it is not leaked into the RinGPU ABI. */
        for (index = 0u; index < program->attribute_count; ++index) {
            const RinGLProgramAttribute* attribute = &program->attributes[index];
            if (attribute->location >= RINGL_MAX_VERTEX_ATTRIBS ||
                ringl_append_vertex_attrib(
                    context, &context->vertex_attribs[attribute->location],
                    attribute->width, &common_buffer, &common_stride,
                    &scalar_location, layout) != 0) {
                return -1;
            }
        }
    } else {
        /* Keep the pre-link diagnostic helper useful to callers and tests.
         * Actual draws always use the linked-program branch above. */
        for (index = 0u; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
            const RinGLVertexAttribState* attrib =
                &context->vertex_attribs[index];
            if (!attrib->enabled)
                continue;
            if (ringl_append_vertex_attrib(context, attrib, attrib->size,
                                           &common_buffer, &common_stride,
                                           &scalar_location, layout) != 0) {
                return -1;
            }
        }
    }

    layout->buffer = common_buffer;
    layout->stride = common_stride;
    return 0;
}

int ringl_validate_vertex_fetch(const RinGLContext* context,
                                uint32_t first_vertex,
                                uint32_t vertex_count,
                                RinGLResolvedVertexLayout* layout)
{
    uint32_t slot_index;
    uint32_t index;
    uint64_t last_vertex;
    const RinGLBufferObject* buffer;

    if (ringl_resolve_vertex_layout(context, layout) != 0)
        return -1;
    if (vertex_count == 0u || layout->attribute_count == 0u)
        return 0;
    if (layout->buffer == 0u) {
        return layout->has_constant_attributes != 0u && layout->stride == 0u
            ? 0 : -1;
    }
    if (layout->stride == 0u)
        return -1;

    last_vertex = (uint64_t)first_vertex + (uint64_t)vertex_count - 1u;
    if (last_vertex > UINT32_MAX)
        return -1;

    slot_index = ringl_object_slot_index(layout->buffer);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    buffer = &context->buffers[slot_index];
    if (buffer->size_bytes == 0u)
        return -1;

    for (index = 0; index < layout->attribute_count; ++index) {
        const RinGLResolvedVertexAttribute* attrib = &layout->attributes[index];
        uint32_t component_bytes;
        uint64_t stride_bytes;
        uint64_t end;

        if (attrib->flags ==
            RINGL_RIN_GPU_VERTEX_ATTRIBUTE_CONSTANT_FLOAT32) {
            if (attrib->format != RINGL_NATIVE_VERTEX_FLOAT32)
                return -1;
            continue;
        }
        if (attrib->flags != 0u)
            return -1;
        component_bytes = ringl_native_vertex_format_bytes(attrib->format);
        if (component_bytes == 0u)
            return -1;
        if (last_vertex != 0u &&
            (uint64_t)layout->stride >
                (UINT64_MAX - (uint64_t)attrib->offset - component_bytes) /
                    last_vertex) {
            return -1;
        }
        stride_bytes = last_vertex * (uint64_t)layout->stride;
        end = (uint64_t)attrib->offset + stride_bytes + component_bytes;
        if (end > buffer->size_bytes)
            return -1;
    }
    return 0;
}

int ringl_validate_index_fetch(const RinGLContext* context,
                               uint32_t index_type,
                               uint64_t offset,
                               uint32_t count,
                               uint32_t* max_index_out)
{
    const RinGLBufferObject* buffer;
    uint32_t slot_index;
    uint32_t index_size;
    uint64_t bytes;
    uint32_t max_index = 0u;
    uint32_t i;

    if (context == NULL || max_index_out == NULL ||
        context->element_array_buffer == 0u)
        return -1;
    if (ringl_object_lookup_const(context, context->element_array_buffer,
                                  RINGL_OBJECT_BUFFER) == NULL)
        return -1;

    if (index_type == RINGL_UNSIGNED_BYTE)
        index_size = 1u;
    else if (index_type == RINGL_UNSIGNED_SHORT)
        index_size = 2u;
    else if (index_type == RINGL_UNSIGNED_INT)
        index_size = 4u;
    else
        return -1;

    if ((offset & (uint64_t)(index_size - 1u)) != 0u)
        return -1;
    if ((uint64_t)count > UINT64_MAX / index_size)
        return -1;
    bytes = (uint64_t)count * index_size;
    if (offset > UINT64_MAX - bytes)
        return -1;

    slot_index = ringl_object_slot_index(context->element_array_buffer);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    buffer = &context->buffers[slot_index];
    if (offset + bytes > buffer->size_bytes ||
        (bytes != 0u && buffer->shadow_bytes == NULL))
        return -1;

    for (i = 0u; i < count; ++i) {
        uint32_t value;
        const uint8_t* source = buffer->shadow_bytes + offset +
            (uint64_t)i * index_size;
        if (index_size == 1u) {
            value = source[0];
        } else if (index_size == 2u) {
            uint16_t value16;
            memcpy(&value16, source, sizeof(value16));
            value = value16;
        } else {
            memcpy(&value, source, sizeof(value));
        }
        if (value > max_index)
            max_index = value;
    }

    *max_index_out = max_index;
    return 0;
}
