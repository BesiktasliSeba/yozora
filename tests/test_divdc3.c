#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* the copy under test is built under another name, the compiler's own __divdc3 answers a / b below */
double _Complex yozora_test_divdc3(double a, double b, double c, double d);

static int failures;

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "test_divdc3: %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

/* the same class (nan, infinity of a sign, zero of a sign) and a relative error under 1e-7 for finite results: the compiler's own division and this
   copy may fuse a multiply and an add differently, and where the two terms of a sum nearly cancel that moves the last digits. a wrong sign or a
   swapped part is off by far more */
static int close_to(double got, double want)
{
    if (isnan(want))
        return isnan(got);
    if (isinf(want))
        return isinf(got) && signbit(got) == signbit(want);
    if (want == 0.0)
        return got == 0.0 && signbit(got) == signbit(want);
    return fabs(got - want) <= 1e-7 * fabs(want);
}

static void compare(double a, double b, double c, double d)
{
    double _Complex got = yozora_test_divdc3(a, b, c, d);
    volatile double _Complex x = a + b * I, y = c + d * I;
    double _Complex want = x / y;
    if (!close_to(creal(got), creal(want)) || !close_to(cimag(got), cimag(want))) {
        fprintf(stderr, "test_divdc3: (%g,%g) / (%g,%g): got (%g,%g), want (%g,%g)\n", a, b, c, d, creal(got), cimag(got), creal(want), cimag(want));
        ++failures;
    }
}

/* what compiler-rt answers for the cases the recovery code is for, taken from a run of its own divdc3.c (this copy and that file gave the same bits
   on 528561 inputs, all of the nan, infinity and zero combinations among them). the host's libgcc answers some of them differently */
static void expect(double a, double b, double c, double d, double real, double imag)
{
    double _Complex got = yozora_test_divdc3(a, b, c, d);
    int real_ok = isnan(real) ? isnan(creal(got)) : (creal(got) == real && signbit(creal(got)) == signbit(real));
    int imag_ok = isnan(imag) ? isnan(cimag(got)) : (cimag(got) == imag && signbit(cimag(got)) == signbit(imag));
    if (!real_ok || !imag_ok) {
        fprintf(stderr, "test_divdc3: (%g,%g) / (%g,%g): got (%g,%g), want (%g,%g)\n", a, b, c, d, creal(got), cimag(got), real, imag);
        ++failures;
    }
}

int main(void)
{
    /* plain values, a unit, a division by a real, by an imaginary */
    CHECK(creal(yozora_test_divdc3(1, 0, 1, 0)) == 1 && cimag(yozora_test_divdc3(1, 0, 1, 0)) == 0);
    CHECK(creal(yozora_test_divdc3(6, 4, 2, 0)) == 3 && cimag(yozora_test_divdc3(6, 4, 2, 0)) == 2);
    CHECK(creal(yozora_test_divdc3(0, 1, 0, 1)) == 1 && cimag(yozora_test_divdc3(0, 1, 0, 1)) == 0);
    CHECK(close_to(creal(yozora_test_divdc3(1, 2, 3, 4)), 0.44) && close_to(cimag(yozora_test_divdc3(1, 2, 3, 4)), 0.08));

    /* division by zero, infinite and nan operands, a denominator that overflows when squared or underflows, a quotient past the range */
    expect(1, 0, 0, 0, INFINITY, NAN);
    expect(0, 0, 0, 0, NAN, NAN);
    expect(-1, 2, 0, 0, -INFINITY, INFINITY);
    expect(INFINITY, 0, 1, 0, INFINITY, NAN);
    expect(1, 0, INFINITY, 0, 0, 0);
    expect(INFINITY, INFINITY, INFINITY, INFINITY, NAN, NAN);
    expect(NAN, 0, 1, 0, NAN, NAN);
    expect(1, 1, 0, 1, 1, -1);
    expect(1e308, 1e308, 1e-308, 1e-308, INFINITY, 0);
    expect(1, 1, 1e308, 1e308, 9.9999999999999991e-309, 0);

    /* finite values of every size against the compiler's own division */
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (int n = 0; n < 200000; ++n) {
        double v[4];
        for (int m = 0; m < 4; ++m) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            int exponent = (int)((state >> 33) % 400) - 200;
            double mantissa = (double)((state >> 11) & 0xfffffffffffffull) / (double)0x10000000000000ull + 0.5;
            v[m] = ldexp(((state >> 63) ? -mantissa : mantissa), exponent);
        }
        compare(v[0], v[1], v[2], v[3]);
    }

    if (failures) {
        fprintf(stderr, "test_divdc3: %d failures\n", failures);
        return 1;
    }
    printf("test_divdc3: ok\n");
    return 0;
}
