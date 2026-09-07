#ifndef _GL4ES_LOGS_H_
#define _GL4ES_LOGS_H_
//----------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include "init.h"
#include "attributes.h"
//----------------------------------------------------------------------------
void LogPrintf_NoPrefix(const char *fmt,...);
void LogFPrintf(FILE *fp,const char *fmt,...);
EXPORT void LogPrintf(const char *fmt,...);
void write_log(const char* format, ...);
//----------------------------------------------------------------------------
// ZOMDROID DIAG (Mali ES3-mine hunt): flight recorder + probes.
void zomdroid_gltrace(const char* fmt, ...);
void zomdroid_exit_probe_register(void);
// MEMDIAG: buffer shadow-copy accounting (+RSS/Swap ground truth via /proc/self/status)
void zomdroid_memstat_tag(const char* tag);
void zomdroid_shadow_add(long n);
void zomdroid_shadow_sub(long n);
// GPU-ALLOC DIAG (GL mtrack explosion hunt): ledger of driver-side storage the app
// requests through our entry points, keyed by app-visible object id.
// cat: 0=texture 1=buffer 2=renderbuffer 3=stream-scratch (cumulative only)
void zomdroid_glalloc_set(int cat, unsigned id, long bytes);
void zomdroid_glalloc_del(int cat, unsigned id);
void zomdroid_glalloc_cum(int cat, long bytes);
long zomdroid_glalloc_live_tex(void);

// Opt-in native frame-path tracer. ZOMDROID_NG_TRACE=1 enables it; disabled is a
// single predictable branch at each instrumented entry point and performs no clock
// reads, allocation, locking or I/O. Enabled data is grouped into 16.7 ms buckets
// and only slow buckets are written, keeping the launcher's rotating native.log
// useful during a several-minute driving reproduction.
typedef enum {
    ZNG_TRACE_DRAW_TOTAL = 0,
    ZNG_TRACE_DRAW_DRIVER,
    ZNG_TRACE_BUFFER,
    ZNG_TRACE_TEXTURE,
    ZNG_TRACE_PROGRAM,
    ZNG_TRACE_FBO,
    ZNG_TRACE_SYNC,
    ZNG_TRACE_CATEGORY_COUNT
} zomdroid_ngtrace_category_t;

typedef struct {
    uint64_t start_ns;
    uint64_t units;
    uint64_t bytes;
    unsigned char category;
    unsigned char active;
    unsigned char record;
} zomdroid_ngtrace_scope_t;

extern int zomdroid_ngtrace_state;
int zomdroid_ngtrace_init(void);
zomdroid_ngtrace_scope_t zomdroid_ngtrace_begin_enabled(
        zomdroid_ngtrace_category_t category, uint64_t units, uint64_t bytes);
void zomdroid_ngtrace_end_enabled(zomdroid_ngtrace_scope_t* scope);

static inline int zomdroid_ngtrace_active(void) {
    int state = __atomic_load_n(&zomdroid_ngtrace_state, __ATOMIC_RELAXED);
    return (state >= 0) ? state : zomdroid_ngtrace_init();
}

static inline zomdroid_ngtrace_scope_t zomdroid_ngtrace_begin(
        zomdroid_ngtrace_category_t category, uint64_t units, uint64_t bytes) {
    zomdroid_ngtrace_scope_t scope = {0};
    if (__builtin_expect(zomdroid_ngtrace_active(), 0))
        scope = zomdroid_ngtrace_begin_enabled(category, units, bytes);
    return scope;
}

static inline void zomdroid_ngtrace_scope_cleanup(zomdroid_ngtrace_scope_t* scope) {
    if (__builtin_expect(scope->active, 0))
        zomdroid_ngtrace_end_enabled(scope);
}

#define ZNG_TRACE_JOIN2(A, B) A##B
#define ZNG_TRACE_JOIN(A, B) ZNG_TRACE_JOIN2(A, B)
#define ZOMDROID_NGTRACE_FUNCTION(CATEGORY, UNITS, BYTES)                                      \
    zomdroid_ngtrace_scope_t ZNG_TRACE_JOIN(zng_scope_, __LINE__)                              \
        __attribute__((cleanup(zomdroid_ngtrace_scope_cleanup))) =                             \
            zomdroid_ngtrace_begin((CATEGORY), (uint64_t)(UNITS), (uint64_t)(BYTES))
#define ZOMDROID_NGTRACE_CALL(CATEGORY, UNITS, BYTES, CALL)                                    \
    do {                                                                                       \
        zomdroid_ngtrace_scope_t zng_call_scope =                                              \
            zomdroid_ngtrace_begin((CATEGORY), (uint64_t)(UNITS), (uint64_t)(BYTES));          \
        CALL;                                                                                  \
        zomdroid_ngtrace_scope_cleanup(&zng_call_scope);                                       \
    } while (0)
// One-shot usage probe for ES3-only entry points: logs the FIRST 3 calls per function
// (then goes silent — zero flood). The last "GL3 use#" crumb before a silent death
// names the poison candidate.
// Verbose per-compile/link dumps: compiled OUT for play builds (they hammered
// logcat + the log file on every shader). Re-enable by mapping to SHUT_LOGD.
#define ZOMDROID_VDBG(...) do { } while (0)
#define ZOMDROID_GL3PROBE() \
    do { \
        static int zp_c = 0; \
        if (zp_c < 3) { \
            zp_c++; \
            zomdroid_gltrace("GL3 use#%d %s", zp_c, __func__); \
        } \
    } while (0)
//----------------------------------------------------------------------------
#ifdef GL4ES_SILENCE_MESSAGES
	#define SHUT_LOGD(...)
	#define SHUT_LOGD_NOPREFIX(...)
	#define SHUT_LOGE(...)
#else
	#define SHUT_LOGD(...) {printf(__VA_ARGS__);printf("\n");write_log(__VA_ARGS__);}
	#define SHUT_LOGD_NOPREFIX(...) {if(!globals4es.nobanner) LogPrintf_NoPrefix(__VA_ARGS__);}
	#define SHUT_LOGE(...) {printf(__VA_ARGS__);printf("\n");write_log(__VA_ARGS__);}
#endif
//----------------------------------------------------------------------------
#define LOGD(...) SHUT_LOGD(__VA_ARGS__);
#define LOGE(...) SHUT_LOGD(__VA_ARGS__);
//----------------------------------------------------------------------------
#endif // _GL4ES_LOGS_H_
