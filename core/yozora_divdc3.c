/* the quotient of two complex doubles, the way compiler-rt does it (lib/builtins/divdc3.c of llvm 23.1.1, Apache License v2.0 with LLVM
   Exceptions, https://llvm.org/LICENSE.txt). libSystem exports ___divdc3 for armv7 only, so an arm64 binary that divides complex numbers
   (the frequency response of the web audio filters in WebCore) has no definition of it and the link fails. the toolchain carries no
   libclang_rt for ios. the macros of int_math.h are the builtins here, nothing else changed */

#if defined(__aarch64__) || defined(YOZORA_DIVDC3_TEST)

#ifndef YOZORA_DIVDC3_NAME
#define YOZORA_DIVDC3_NAME __divdc3
#endif

double _Complex YOZORA_DIVDC3_NAME(double a, double b, double c, double d);

double _Complex YOZORA_DIVDC3_NAME(double a, double b, double c, double d)
{
    int ilogbw = 0;
    double logbw = __builtin_logb(__builtin_fmax(__builtin_fabs(c), __builtin_fabs(d)));
    if (__builtin_isfinite(logbw)) {
        ilogbw = (int)logbw;
        c = __builtin_scalbn(c, -ilogbw);
        d = __builtin_scalbn(d, -ilogbw);
    }
    double denom = c * c + d * d;
    double real = __builtin_scalbn((a * c + b * d) / denom, -ilogbw);
    double imag = __builtin_scalbn((b * c - a * d) / denom, -ilogbw);
    if (__builtin_isnan(real) && __builtin_isnan(imag)) {
        if ((denom == 0.0) && (!__builtin_isnan(a) || !__builtin_isnan(b))) {
            real = __builtin_copysign(__builtin_inf(), c) * a;
            imag = __builtin_copysign(__builtin_inf(), c) * b;
        } else if ((__builtin_isinf(a) || __builtin_isinf(b)) && __builtin_isfinite(c) && __builtin_isfinite(d)) {
            a = __builtin_copysign(__builtin_isinf(a) ? 1.0 : 0.0, a);
            b = __builtin_copysign(__builtin_isinf(b) ? 1.0 : 0.0, b);
            real = __builtin_inf() * (a * c + b * d);
            imag = __builtin_inf() * (b * c - a * d);
        } else if (__builtin_isinf(logbw) && logbw > 0.0 && __builtin_isfinite(a) && __builtin_isfinite(b)) {
            c = __builtin_copysign(__builtin_isinf(c) ? 1.0 : 0.0, c);
            d = __builtin_copysign(__builtin_isinf(d) ? 1.0 : 0.0, d);
            real = 0.0 * (a * c + b * d);
            imag = 0.0 * (b * c - a * d);
        }
    }
    double _Complex z;
    __real__ z = real;
    __imag__ z = imag;
    return z;
}

#else

/* an empty file is not valid c99 */
typedef int yozora_divdc3_unused;

#endif
