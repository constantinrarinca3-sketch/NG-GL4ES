#ifndef _GL4ES_LOGS_H_
#define _GL4ES_LOGS_H_
//----------------------------------------------------------------------------
#include <stdio.h>
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
