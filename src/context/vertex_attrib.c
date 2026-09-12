/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static RinGLVertexAttribState* ringl_vertex_attrib(RinGLContext* context,
                                                   uint32_t index)
{
    if (context == NULL || index >= RINGL_MAX_VERTEX_ATTRIBS)
        return NULL;
    return &context->vertex_attribs[index];
}

static uint32_t ringl_vertex_component_bytes(uint32_t type)
{
    if (type == RINGL_BYTE || type == RINGL_UNSIGNED_BYTE)
        return 1u;
    if (type == RINGL_SHORT || type == RINGL_UNSIGNED_SHORT)
        return 2u;
    if (type == RINGL_FLOAT)
        return 4u;
    return 0u;
}

static void ringl_vertex_attrib_set_current(RinGLContext* context,
                                            uint32_t index, float x, float y,
                                            float z, float w)
{
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    attrib->current_value[0] = x;
    attrib->current_value[1] = y;
    attrib->current_value[2] = z;
    attrib->current_value[3] = w;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

void ringl_enable_vertex_attrib_array(uint32_t index)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (!attrib->enabled) {
        attrib->enabled = RINGL_TRUE;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

void ringl_disable_vertex_attrib_array(uint32_t index)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (attrib->enabled) {
        attrib->enabled = RINGL_FALSE;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

void ringl_vertex_attrib_pointer(uint32_t index,
                                 int32_t size,
                                 uint32_t type,
                                 uint32_t normalized,
                                 int32_t stride,
                                 uint64_t offset)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (ringl_vertex_component_bytes(type) == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if ((size != 1 && size != 2 && size != 3 && size != 4) ||
        (normalized != RINGL_FALSE && normalized != RINGL_TRUE) ||
        stride < 0 || stride > 2048) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->array_buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((offset % ringl_vertex_component_bytes(type)) != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    attrib->size = (uint32_t)size;
    attrib->type = type;
    attrib->normalized = normalized;
    attrib->stride = (uint32_t)stride;
    attrib->buffer = context->array_buffer;
    attrib->offset = offset;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}

void ringl_vertex_attrib_divisor(uint32_t index, uint32_t divisor)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return;
    if (context->webgl_instanced_arrays_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (attrib->divisor != divisor) {
        attrib->divisor = divisor;
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
    }
}

uint32_t ringl_get_vertex_attrib_divisor(uint32_t index)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL)
        return 0u;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return 0u;
    }
    return attrib->divisor;
}

void ringl_vertex_attrib1f(uint32_t index, float x)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x,
                                    0.0f, 0.0f, 1.0f);
}

void ringl_vertex_attrib2f(uint32_t index, float x, float y)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    0.0f, 1.0f);
}

void ringl_vertex_attrib3f(uint32_t index, float x, float y, float z)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    z, 1.0f);
}

void ringl_vertex_attrib4f(uint32_t index, float x, float y, float z,
                           float w)
{
    ringl_vertex_attrib_set_current(ringl_get_current_context(), index, x, y,
                                    z, w);
}

int ringl_get_vertex_attrib(uint32_t index, RinGLVertexAttribInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL || info == NULL)
        return -1;
    if (info->struct_size < sizeof(*info) || info->api_version != RINGL_API_VERSION)
        return -1;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }

    info->enabled = attrib->enabled;
    info->size = attrib->size;
    info->type = attrib->type;
    info->normalized = attrib->normalized;
    info->stride = attrib->stride;
    info->buffer = attrib->buffer;
    info->offset = attrib->offset;
    return 0;
}

int ringl_get_vertex_attrib_current(uint32_t index, float values[4])
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;

    if (context == NULL || values == NULL)
        return -1;
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    memcpy(values, attrib->current_value, sizeof(attrib->current_value));
    return 0;
}

static size_t ringl_vertex_attrib_query_count(uint32_t pname)
{
    return pname == RINGL_CURRENT_VERTEX_ATTRIB ? 4u :
        (pname == RINGL_VERTEX_ATTRIB_ARRAY_ENABLED ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_SIZE ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_STRIDE ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_TYPE ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_NORMALIZED ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING ||
         pname == RINGL_VERTEX_ATTRIB_ARRAY_DIVISOR ? 1u : 0u);
}

static int ringl_vertex_attrib_query_validate(
    RinGLContext* context, uint32_t index, uint32_t pname,
    size_t value_count, RinGLVertexAttribState** attrib_out,
    size_t* required_out)
{
    size_t required;
    RinGLVertexAttribState* attrib;

    if (context == NULL || attrib_out == NULL || required_out == NULL)
        return -1;
    required = ringl_vertex_attrib_query_count(pname);
    if (required == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    if (value_count < required) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    attrib = ringl_vertex_attrib(context, index);
    if (attrib == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    *attrib_out = attrib;
    *required_out = required;
    return 0;
}

int ringl_get_vertex_attribiv_bounded(uint32_t index, uint32_t pname,
                                      int32_t* values, size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;
    int32_t converted[4] = {0, 0, 0, 0};
    size_t required;

    if (context == NULL)
        return -1;
    if (values == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (ringl_vertex_attrib_query_validate(context, index, pname,
                                           value_count, &attrib,
                                           &required) != 0)
        return -1;
    if (pname == RINGL_CURRENT_VERTEX_ATTRIB) {
        for (size_t component = 0u; component < required; ++component) {
            float current = attrib->current_value[component];

            if (!isfinite(current) || current < (float)INT32_MIN ||
                current > (float)INT32_MAX) {
                ringl_context_record_error(context, RINGL_INVALID_OPERATION);
                return -1;
            }
            converted[component] = (int32_t)current;
        }
    } else {
        switch (pname) {
        case RINGL_VERTEX_ATTRIB_ARRAY_ENABLED:
            converted[0] = (int32_t)attrib->enabled;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_SIZE:
            converted[0] = (int32_t)attrib->size;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_STRIDE:
            converted[0] = (int32_t)attrib->stride;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_TYPE:
            converted[0] = (int32_t)attrib->type;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_NORMALIZED:
            converted[0] = (int32_t)attrib->normalized;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:
            converted[0] = (int32_t)attrib->buffer;
            break;
        case RINGL_VERTEX_ATTRIB_ARRAY_DIVISOR:
            converted[0] = (int32_t)attrib->divisor;
            break;
        default:
            /* The validator rejects this path before the switch. */
            return -1;
        }
    }
    memcpy(values, converted, required * sizeof(*values));
    return 0;
}

int ringl_get_vertex_attribfv_bounded(uint32_t index, uint32_t pname,
                                      float* values, size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLVertexAttribState* attrib;
    float converted[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    size_t required;

    if (context == NULL)
        return -1;
    if (values == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (ringl_vertex_attrib_query_validate(context, index, pname,
                                           value_count, &attrib,
                                           &required) != 0)
        return -1;
    if (pname == RINGL_CURRENT_VERTEX_ATTRIB) {
        memcpy(converted, attrib->current_value, required * sizeof(*converted));
    } else {
        int32_t integer_value[1] = {0};

        if (ringl_get_vertex_attribiv_bounded(index, pname, integer_value,
                                              1u) != 0)
            return -1;
        converted[0] = (float)integer_value[0];
    }
    memcpy(values, converted, required * sizeof(*values));
    return 0;
}
