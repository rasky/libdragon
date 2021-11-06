/**
 * @file fmath.c
 * @brief Fast math routines, optimized for 3D graphics calculations
 * @ingroup fastmath
 */
#include "fmath.h"
#include <string.h>
#include <stdint.h>

float fm_sinf(float x) {
    // Approximation of sine to 5 ULP with Chebyshev polynomials
    // http://mooooo.ooo/chebyshev-sine-approximation/
    const float pi_hi            = 3.14159274e+00f; // 0x1.921fb6p+01
    const float pi_lo            =-8.74227766e-08f; // -0x1.777a5cp-24
    float p, s;

    // This function has been designed to operate in the [-π, +π] range, so
    // bring the argument there. This reduction using dragon_fmodf is not
    // very accurate for large numbers, so it will introduce more error compared
    // to the 5 ULP figure.
    x = fm_fmodf(x+pi_hi, 2*pi_hi) - pi_hi;
    s = x * x;
    p =         1.32729383e-10f;
    p = p * s - 2.33177868e-8f;
    p = p * s + 2.52223435e-6f;
    p = p * s - 1.73503853e-4f;
    p = p * s + 6.62087463e-3f;
    p = p * s - 1.01321176e-1f;
    return x * ((x - pi_hi) - pi_lo) * ((x + pi_hi) + pi_lo) * p;
}

float fm_cosf(float x) {
    const float half_pi = 1.57079637e+0f; //  0x1.921fb6p+0
    return fm_sinf(half_pi - x);
}

/** @brief Calculate `z / sqrt(x)`. 
 * 
 * This primitive can be used for both `1/sqrt(x)` (z=1), and `sqrt(x)` (z=x).
 *
 * We use the algorithm presented in this 2018 paper:
 * Improving the Accuracy of the Fast Inverse Square Root by Modifying
 * Newton–Raphson Corrections (Walczyk, Moroz, Cieslinski).
 * 
 * It computes an initial estimation (similar to the famous Quake's version)
 * and two subsequent debiased Newton-Raphson iterations. This version does
 * not handle denormals as they are not supported by MIPS VR4300 FPU anyway.
 * 
 * The average numerical error is ~= 0.35 * 10e-6.
 **/
__attribute__((noinline))
static float __inv_sqrtf(float x, float z) {
    int32_t i = BITCAST_F2I(x);
    i = 0x5F200000 - (i>>1);
    float y = BITCAST_I2F(i);
    y *= 1.68191391f - 0.703952009f * x * y * y;
    y *= 1.50000036f - 0.500000053f * x * y * y;
    return y * z;
}

float fm_inv_sqrtf(float x) { return __inv_sqrtf(x, 1.0f); }
float fm_sqrtf(float x)     { return __inv_sqrtf(x, x);    }

