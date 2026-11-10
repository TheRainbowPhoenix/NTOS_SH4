#ifndef SHIM_ASSERT_H
#define SHIM_ASSERT_H
/* NDEBUG is set for release PE builds; assert compiles to nothing */
#define assert(x) ((void)0)
#endif
