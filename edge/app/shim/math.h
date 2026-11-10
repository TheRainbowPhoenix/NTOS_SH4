#ifndef SHIM_MATH_H
#define SHIM_MATH_H
/* isnan/isfinite are compiler builtins; the transcendental functions come
 * from mquickjs's own libm.c compiled into the module */
#define isnan(x) __builtin_isnan(x)
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define signbit(x) __builtin_signbit(x)
double sqrt(double);
double floor(double);
double ceil(double);
double fabs(double);
double pow(double, double);
double log(double);
double log2(double);
double log10(double);
double exp(double);
double sin(double);
double cos(double);
double tan(double);
double atan2(double, double);
double fmod(double, double);
double ldexp(double, int);
double trunc(double);
double copysign(double, double);
double round(double);
#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define M_PI 3.14159265358979323846
#endif
