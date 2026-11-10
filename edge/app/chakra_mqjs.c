/* chakra_mqjs.c — behavioural Chakra variant over mquickjs (HOST EXPERIMENTS).
 *
 * NOT the default build: the device profile has no FPU and cannot run a
 * double-based JS engine, so the shipped chakra.dll is the pure API-shape
 * stub (chakra_stub.c, generated). This file keeps the experiment wiring
 * the Chakra public API onto Fabrice Bellard's MicroQuickJS and builds as
 * chakra-full.dll when the engine sources are present (see Makefile).
 *
 * The real chakra.dll is ~5.8MB x86 (see edge/analysis/pe_scan); mquickjs
 * is ~100 KB ROM, runs in as little as 10 KB RAM, tracing GC, no CPU-stack
 * VM. The engine context lives in a fixed static arena, matching the
 * NTOS "no surprise heap growth" model.
 *
 * v1 scope: runtime/context create+dispose, current-context switching,
 * RunScript, exception query, value->string conversion, string out-param.
 * Values are mquickjs JSValues (single context = single runtime in v1).
 * Full language stdlib (Object/Function/Array/String/Number/Math/JSON/Date/
 * RegExp/typed arrays) comes from the generated mqjs_edge_stdlib.h tables;
 * the 7 host functions route to NTOS (console.log -> coredll_puts, gc ->
 * JS_GC; load/setTimeout throw "unsupported" in v1).
 */
#include "nt_app.h"
#include "mquickjs.h"
/* host stdlib functions (defined below) referenced by the generated tables */
JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv);
#include "mqjs_edge_stdlib.h" /* generated: js_stdlib def + tables */

/* JSValue pointer encoding (mirrors mquickjs_priv.h one-liners) */
#define CK_VALUE_FROM_PTR(ptr) ((JSWord)((uintptr_t)(ptr) + 1))
#define CK_VALUE_TO_PTR(v) ((void *)((uintptr_t)(v) - 1))

static int xstrlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        ++n;
    return n;
}

/* ---- imports ---- */
NT_IMPORT(void, coredll_puts, (const char *))
NT_DEFINE(coredll_puts)

/* ---- JS arena: fixed memory, zero heap ---- */
#define JS_ARENA_SIZE (192 * 1024)
static unsigned char js_arena[JS_ARENA_SIZE] __attribute__((aligned(8)));
static JSContext *g_ctx;

/* Chakra handles: opaque pointers; v1 singletons */
typedef void JsRuntimeHandle;
typedef void JsContextRef;
typedef void JsValueRef;
static char runtime_obj, context_obj; /* handle identities */

/* Chakra error codes (subset) */
#define JS_NO_ERROR 0
#define JS_ERROR_INVALID_ARGUMENT ((int)0x80070057)
#define JS_ERROR_FATAL ((int)0x8000FFFF)
#define JS_ERROR_SCRIPTException ((int)0x80264001)
#define JS_ERROR_OUT_OF_MEMORY 0x8007000E

/* JsValueType enum (real Chakra values) */
#define JsUndefined 0
#define JsNull 1
#define JsNumber 2
#define JsString 3
#define JsBoolean 4
#define JsObject 5

__attribute__((dllexport)) int JsCreateRuntime(unsigned attributes, void *thread_svc,
                                               JsRuntimeHandle **runtime)
{
    (void)attributes;
    (void)thread_svc;
    if (!runtime)
        return JS_ERROR_INVALID_ARGUMENT;
    *runtime = (JsRuntimeHandle *)&runtime_obj;
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsDisposeRuntime(JsRuntimeHandle *runtime)
{
    if (runtime != (JsRuntimeHandle *)&runtime_obj)
        return JS_ERROR_INVALID_ARGUMENT;
    if (g_ctx) {
        JS_FreeContext(g_ctx);
        g_ctx = 0;
    }
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsCreateContext(JsRuntimeHandle *runtime, JsContextRef **context)
{
    if (runtime != (JsRuntimeHandle *)&runtime_obj || !context)
        return JS_ERROR_INVALID_ARGUMENT;
    if (!g_ctx) {
        g_ctx = JS_NewContext(js_arena, sizeof(js_arena), &js_stdlib);
        if (!g_ctx)
            return JS_ERROR_OUT_OF_MEMORY;
    }
    *context = (JsContextRef *)g_ctx;
    return JS_NO_ERROR;
}

/* mquickjs has a single current context per process; accepted and ignored. */
__attribute__((dllexport)) int JsSetCurrentContext(JsContextRef *context)
{
    if (context && context != (JsContextRef *)g_ctx)
        return JS_ERROR_INVALID_ARGUMENT;
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsRunScript(const W16 *script, unsigned source_context,
                                           const W16 *source_url, JsValueRef **result)
{
    (void)source_context;
    (void)source_url;
    if (!script || !result || !g_ctx)
        return JS_ERROR_INVALID_ARGUMENT;
    /* UTF-16 -> UTF-8 (BMP only, matches device constraints) */
    static char utf8[8192];
    int n = 0;
    for (int i = 0; script[i] && n < (int)sizeof(utf8) - 4; ++i) {
        unsigned c = script[i];
        if (c < 0x80)
            utf8[n++] = (char)c;
        else {
            utf8[n++] = (char)(0xC0 | (c >> 6));
            utf8[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    utf8[n] = 0;
    JSValue v = JS_Eval(g_ctx, utf8, (unsigned)n, "edge", 0);
    if (JS_IsException(v)) {
        JSValue e = JS_GetException(g_ctx);
        static char msg[512];
        JSCStringBuf ebuf;
        const char *m = JS_ToCString(g_ctx, e, &ebuf);
        if (m) {
            int i = 0;
            for (; m[i] && i < 511; ++i)
                msg[i] = m[i];
            msg[i] = 0;
        } else
            msg[0] = 0;
        NT_CALL(coredll_puts, ("chakra_stub: script exception: "));
        NT_CALL(coredll_puts, (msg));
        NT_CALL(coredll_puts, ("\n"));
        return JS_ERROR_SCRIPTException;
    }
    *result = (JsValueRef *)CK_VALUE_TO_PTR(v); /* stash raw value ptr */
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsConvertValueToString(JsValueRef *value, JsValueRef **str)
{
    if (!value || !str || !g_ctx)
        return JS_ERROR_INVALID_ARGUMENT;
    JSValue v = (JSValue)CK_VALUE_FROM_PTR(value);
    /* JS_ToCString result lives in the caller-provided string buffer; keep
     * it in a static one so the handle stays valid until the next call */
    static JSCStringBuf sbuf;
    static char buf[4096];
    const char *s = JS_ToCString(g_ctx, v, &sbuf);
    if (!s)
        return JS_ERROR_SCRIPTException;
    int i = 0;
    for (; s[i] && i < 4095; ++i)
        buf[i] = s[i];
    buf[i] = 0;
    *str = (JsValueRef *)buf;
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsStringToPointer(JsValueRef *value, const char **str,
                                                 unsigned *len)
{
    if (!value || !str)
        return JS_ERROR_INVALID_ARGUMENT;
    *str = (const char *)value;
    if (len)
        *len = (unsigned)xstrlen(*str);
    return JS_NO_ERROR;
}

__attribute__((dllexport)) int JsGetValueType(JsValueRef *value, int *type)
{
    if (!value || !type || !g_ctx)
        return JS_ERROR_INVALID_ARGUMENT;
    JSValue v = (JSValue)CK_VALUE_FROM_PTR(value);
    *type = JS_IsString(g_ctx, v) ? JsString
          : JS_IsBool(v) ? JsBoolean
          : JS_IsNumber(g_ctx, v) ? JsNumber
          : JS_IsNull(v) ? JsNull
          : JS_IsUndefined(v) ? JsUndefined
          : JsObject;
    return JS_NO_ERROR;
}

/* ---- NTOS-backed stdlib host functions (referenced by mqjs_edge_stdlib.h) ----
 * The real chakra.dll pulls these from the OS; on NTOS they route to the
 * loader-provided coredll. js_print = console.log/print. */
static void ck_out(const char *s)
{
    if (s)
        NT_CALL(coredll_puts, (s));
}

JSValue js_print(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        JSCStringBuf buf;
        const char *str;
        if (JS_IsString(ctx, argv[i])) {
            size_t len;
            str = JS_ToCStringLen(ctx, &len, argv[i], &buf);
            if (!str)
                return JS_EXCEPTION;
            ck_out(str);
        } else {
            str = JS_ToCString(ctx, argv[i], &buf);
            if (!str)
                return JS_EXCEPTION;
            ck_out(str);
        }
    }
    ck_out("\n");
    return JS_UNDEFINED;
}

JSValue js_gc(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    JS_GC(ctx);
    return JS_UNDEFINED;
}

JSValue js_load(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "load unsupported on NTOS");
}

JSValue js_setTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "setTimeout unsupported in v1");
}

JSValue js_clearTimeout(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_UNDEFINED;
}

JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewFloat64(ctx, 0.0);
}

JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewFloat64(ctx, 0.0);
}

JSValue js_date_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    /* no RTC on the device in v1: Date() returns epoch 0 */
    return JS_NewFloat64(ctx, 0.0);
}

/* ---- self test used by the NTOS_SH4_linux e2e run ---- */
static const W16 script[] = {
    'v', 'a', 'r', ' ', 's', '=', '0', ';', ' ', 'f', 'o', 'r', '(', 'v', 'a', 'r', ' ', 'i', '=', '0', ';', 'i', '<', '1', '0', '0', '0', ';', 'i', '+', '+', ')', ' ', 's', '+', '=', 'i', ';', ' ', 'c', 'o', 'n', 's', 'o', 'l', 'e', '.', 'l', 'o', 'g', '(', '\'', 'm', 'q', 'u', 'i', 'c', 'k', 'j', 's', ':', ' ', '\'', '+', 's', ')', ';', ' ', 'c', 'o', 'n', 's', 'o', 'l', 'e', '.', 'l', 'o', 'g', '(', '\'', 'c', 'h', 'a', 'k', 'r', 'a', '-', 's', 'h', 'a', 'p', 'e', ' ', 'o', 'k', ':', ' ', '\'', '+', '(', 's', '=', '=', '=', '4', '9', '9', '5', '0', '0', ')', ')', 0};

__attribute__((dllexport)) int ChakraSmokeTest(void)
{
    JsRuntimeHandle *rt;
    JsContextRef *cx;
    JsValueRef *res, *str;
    const char *out;
    int rc = JsCreateRuntime(0, 0, &rt);
    if (rc)
        return rc;
    rc = JsCreateContext(rt, &cx);
    if (rc)
        return rc;
    rc = JsSetCurrentContext(cx);
    if (rc)
        return rc;
    rc = JsRunScript(script, 0, 0, &res);
    if (rc)
        return rc;
    rc = JsConvertValueToString(res, &str);
    if (rc)
        return rc;
    rc = JsStringToPointer(str, &out, 0);
    if (rc)
        return rc;
    NT_CALL(coredll_puts, ("chakra_stub: "));
    NT_CALL(coredll_puts, (out));
    NT_CALL(coredll_puts, ("\n"));
    NT_CALL(coredll_puts, ("chakra_stub: OK\n"));
    return 0;
}

int DllMainCRTStartup_impl(void *h, U32 r, void *r2);
__asm__(".globl _DllMainCRTStartup\n_DllMainCRTStartup = DllMainCRTStartup_impl\n");
int DllMainCRTStartup_impl(void *h, U32 r, void *r2)
{
    (void)h;
    (void)r;
    (void)r2;
    return 1;
}
