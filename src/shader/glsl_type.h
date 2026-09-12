/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_TYPE_H
#define RINGL_GLSL_TYPE_H

#include <stdint.h>

/* The frontend keeps one canonical shape description for every value that can
 * cross a GLSL expression boundary.  `width` is the scalar component count of
 * a scalar/vector; matrices use columns/rows and samplers use a target. */
typedef enum RinGLGlslTypeKindV1 {
    RINGL_GLSL_TYPE_INVALID = 0,
    RINGL_GLSL_TYPE_SCALAR = 1,
    RINGL_GLSL_TYPE_VECTOR = 2,
    RINGL_GLSL_TYPE_MATRIX = 3,
    RINGL_GLSL_TYPE_SAMPLER = 4,
} RinGLGlslTypeKindV1;

typedef enum RinGLGlslBaseTypeV1 {
    RINGL_GLSL_BASE_NONE = 0,
    RINGL_GLSL_BASE_F32 = 1,
    RINGL_GLSL_BASE_I32 = 2,
    RINGL_GLSL_BASE_BOOL = 3,
} RinGLGlslBaseTypeV1;

typedef enum RinGLGlslSamplerTargetV1 {
    RINGL_GLSL_SAMPLER_NONE = 0,
    RINGL_GLSL_SAMPLER_2D = 1,
    RINGL_GLSL_SAMPLER_CUBE = 2,
} RinGLGlslSamplerTargetV1;

typedef struct RinGLGlslTypeV1 {
    uint8_t kind;
    uint8_t base;
    uint8_t width;
    uint8_t columns;
    uint8_t rows;
    uint8_t sampler_target;
    uint16_t reserved;
} RinGLGlslTypeV1;

static inline RinGLGlslTypeV1 ringl_glsl_invalid_type(void)
{
    RinGLGlslTypeV1 type = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    return type;
}

static inline RinGLGlslTypeV1 ringl_glsl_scalar_type(uint8_t base)
{
    RinGLGlslTypeV1 type = { RINGL_GLSL_TYPE_SCALAR, base, 1u,
                             0u, 0u, 0u, 0u };
    return type;
}

static inline RinGLGlslTypeV1 ringl_glsl_vector_type(uint8_t base,
                                                      uint8_t width)
{
    RinGLGlslTypeV1 type = { RINGL_GLSL_TYPE_VECTOR, base, width,
                             0u, 0u, 0u, 0u };
    return type;
}

static inline RinGLGlslTypeV1 ringl_glsl_matrix_type(uint8_t dimension)
{
    RinGLGlslTypeV1 type = { RINGL_GLSL_TYPE_MATRIX, RINGL_GLSL_BASE_F32, 0u,
                             dimension, dimension, 0u, 0u };
    return type;
}

static inline RinGLGlslTypeV1 ringl_glsl_sampler_type(uint8_t target)
{
    RinGLGlslTypeV1 type = { RINGL_GLSL_TYPE_SAMPLER, RINGL_GLSL_BASE_NONE,
                             0u, 0u, 0u, target, 0u };
    return type;
}

static inline int ringl_glsl_type_valid(RinGLGlslTypeV1 type)
{
    if (type.kind == RINGL_GLSL_TYPE_SCALAR)
        return type.width == 1u &&
               (type.base == RINGL_GLSL_BASE_F32 ||
                type.base == RINGL_GLSL_BASE_I32 ||
                type.base == RINGL_GLSL_BASE_BOOL);
    if (type.kind == RINGL_GLSL_TYPE_VECTOR)
        return type.width >= 2u && type.width <= 4u &&
               (type.base == RINGL_GLSL_BASE_F32 ||
                type.base == RINGL_GLSL_BASE_I32 ||
                type.base == RINGL_GLSL_BASE_BOOL);
    if (type.kind == RINGL_GLSL_TYPE_MATRIX)
        return type.base == RINGL_GLSL_BASE_F32 && type.columns >= 2u &&
               type.columns <= 4u && type.rows == type.columns &&
               type.width == 0u;
    return type.kind == RINGL_GLSL_TYPE_SAMPLER &&
           type.sampler_target >= RINGL_GLSL_SAMPLER_2D &&
           type.sampler_target <= RINGL_GLSL_SAMPLER_CUBE;
}

static inline int ringl_glsl_type_equal(RinGLGlslTypeV1 left,
                                         RinGLGlslTypeV1 right)
{
    return ringl_glsl_type_valid(left) && ringl_glsl_type_valid(right) &&
           left.kind == right.kind && left.base == right.base &&
           left.width == right.width && left.columns == right.columns &&
           left.rows == right.rows && left.sampler_target == right.sampler_target;
}

static inline int ringl_glsl_type_is_numeric(RinGLGlslTypeV1 type)
{
    return ringl_glsl_type_valid(type) &&
           (type.base == RINGL_GLSL_BASE_F32 ||
            type.base == RINGL_GLSL_BASE_I32) &&
           (type.kind == RINGL_GLSL_TYPE_SCALAR ||
            type.kind == RINGL_GLSL_TYPE_VECTOR);
}

static inline int ringl_glsl_type_is_float_vector_or_scalar(
    RinGLGlslTypeV1 type)
{
    return ringl_glsl_type_is_numeric(type) &&
           type.base == RINGL_GLSL_BASE_F32;
}

typedef enum RinGLGlslBinaryOpV1 {
    RINGL_GLSL_BINARY_ADD = 1,
    RINGL_GLSL_BINARY_SUB = 2,
    RINGL_GLSL_BINARY_MUL = 3,
    RINGL_GLSL_BINARY_DIV = 4,
} RinGLGlslBinaryOpV1;

/* Return the inferred type for scalar/vector component-wise arithmetic. A
 * scalar may broadcast to a vector, but integer and float values never mix
 * implicitly and matrices are handled by the matrix-specific rule. */
static inline int ringl_glsl_infer_componentwise(
    RinGLGlslTypeV1 left, RinGLGlslTypeV1 right,
    RinGLGlslBinaryOpV1 operation, int allow_scalar_broadcast,
    RinGLGlslTypeV1* result_out)
{
    uint8_t width;

    (void)operation;
    if (result_out == NULL || !ringl_glsl_type_is_numeric(left) ||
        !ringl_glsl_type_is_numeric(right) || left.base != right.base ||
        left.base == RINGL_GLSL_BASE_BOOL)
        return 0;
    if (left.width == right.width) {
        width = left.width;
    } else if (allow_scalar_broadcast && left.width == 1u) {
        width = right.width;
    } else if (allow_scalar_broadcast && right.width == 1u) {
        width = left.width;
    } else {
        return 0;
    }
    *result_out = width == 1u ? ringl_glsl_scalar_type(left.base)
                              : ringl_glsl_vector_type(left.base, width);
    return 1;
}

static inline int ringl_glsl_infer_matrix_vector(
    RinGLGlslTypeV1 matrix, RinGLGlslTypeV1 vector,
    RinGLGlslTypeV1* result_out)
{
    if (result_out == NULL || !ringl_glsl_type_valid(matrix) ||
        matrix.kind != RINGL_GLSL_TYPE_MATRIX ||
        !ringl_glsl_type_is_float_vector_or_scalar(vector) ||
        vector.width != matrix.columns)
        return 0;
    *result_out = vector.width == 1u
        ? ringl_glsl_scalar_type(RINGL_GLSL_BASE_F32)
        : ringl_glsl_vector_type(RINGL_GLSL_BASE_F32, vector.width);
    return 1;
}

static inline int ringl_glsl_constructor_accepts(
    RinGLGlslTypeV1 target, RinGLGlslTypeV1 argument,
    int allow_numeric_conversion)
{
    if (!ringl_glsl_type_valid(target) ||
        (target.kind != RINGL_GLSL_TYPE_VECTOR &&
         target.kind != RINGL_GLSL_TYPE_SCALAR) ||
        !ringl_glsl_type_valid(argument) ||
        (argument.kind != RINGL_GLSL_TYPE_VECTOR &&
         argument.kind != RINGL_GLSL_TYPE_SCALAR))
        return 0;
    if (target.base == RINGL_GLSL_BASE_BOOL ||
        argument.base == RINGL_GLSL_BASE_BOOL)
        return target.base == argument.base;
    return target.base == argument.base ||
           (allow_numeric_conversion && ringl_glsl_type_is_numeric(target) &&
            ringl_glsl_type_is_numeric(argument));
}

#endif
