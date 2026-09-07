#include "logs.h"
#include "init.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#if defined(ANDROID) && defined(USE_ANDROID_LOG)
#include <android/log.h>
#endif
//----------------------------------------------------------------------------
static const char * const log_prefix="LIBGL: ";
//----------------------------------------------------------------------------
void LogPrintf_NoPrefix(const char *fmt,...)
{
	va_list args;
	va_start(args,fmt);
	#if defined(ANDROID) && defined(USE_ANDROID_LOG)
	__android_log_vprint(ANDROID_LOG_INFO, "LIBGL", fmt, args);
	#else
	vprintf(fmt,args);
	#endif
	va_end(args);
}
//----------------------------------------------------------------------------
void LogFPrintf(FILE *fp,const char *fmt,...)
{
	#ifndef ANDROID
	fprintf(fp,log_prefix);
	#endif
	va_list args;
	va_start(args,fmt);
	#if defined(ANDROID) && defined(USE_ANDROID_LOG)
	// also on logcat
	__android_log_vprint(ANDROID_LOG_INFO, "LIBGL", fmt, args);
	#endif
	vfprintf(fp,fmt,args);
	va_end(args);
}
//----------------------------------------------------------------------------
void LogPrintf(const char *fmt,...)
{
	#ifndef ANDROID
	printf(log_prefix);
	#endif
	va_list args;
	va_start(args,fmt);
	#if defined(ANDROID) && defined(USE_ANDROID_LOG)
	__android_log_vprint(ANDROID_LOG_INFO, "LIBGL", fmt, args);
	#else
	vprintf(fmt,args);
	#endif
	va_end(args);
}
//----------------------------------------------------------------------------

// ZOMDROID GL TRACE: log native rejections in the uniform/program path to a flushed file.
#include <stdio.h>
void zomdroid_gltrace(const char* fmt, ...) {
    static FILE* f = NULL;
    static int init = 0;
    static int count = 0;
    if (count > 12000) return;
    if (!init) {
        init = 1;
        f = fopen("/data/data/com.zomdroid/files/gl_trace.txt", "w");
        // ZOMDROID (Codex minimap audit): a flush per line turned load-time CONVERT
        // bursts into syscall storms. Buffer the file; flush only the lines whose
        // survival matters at crash time — everything else also mirrors to stderr,
        // which the launcher captures independently.
        if (f) setvbuf(f, NULL, _IOFBF, 16384);
    }
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        if (!strncmp(fmt, "INIT", 4) || !strncmp(fmt, "MEMSTAT", 7) || !strncmp(fmt, "EXIT", 4) ||
            !strncmp(fmt, "GLALLOC exit", 12) || !strncmp(fmt, "CCACHE", 6) || !strncmp(fmt, "FBO", 3) ||
            !strncmp(fmt, "DRAWMIX", 7) || !strncmp(fmt, "EBOBATCH", 8))
            fflush(f);
    }
    // ZOMDROID DIAG: mirror to stderr — it lands in the console log that testers can
    // export from Zomdroid WITHOUT adb (and it works even if the file open failed).
    {
        va_list args2;
        va_start(args2, fmt);
        fprintf(stderr, "[NGG] ");
        vfprintf(stderr, fmt, args2);
        va_end(args2);
        fputc('\n', stderr);
        fflush(stderr);
    }
    count++;
}

// ZOMDROID NG FRAME-PATH TRACE ---------------------------------------------------------------
//
// The Java all-frame tracer identifies slow RENDER/RTHREAD windows, but cannot say whether the
// time is GL4ES preparation, a GLES driver call, streaming uploads, FBO work or an explicit GPU
// wait. This opt-in layer supplies that missing split without changing the normal release path.
// It uses thread-local fixed storage (no allocator and no locks) and emits at most one compact
// line per active 16.7 ms bucket. Fast buckets are discarded.
#define ZNG_BUCKET_NS 16666667ull
#define ZNG_DEFAULT_THRESHOLD_US 8000ull

int zomdroid_ngtrace_state = -1; // -1 unknown, -2 initializing, 0 off, 1 on
static uint64_t zng_trace_threshold_ns = ZNG_DEFAULT_THRESHOLD_US * 1000ull;

typedef struct {
    uint64_t bucket_id;
    uint64_t total_ns[ZNG_TRACE_CATEGORY_COUNT];
    uint64_t max_ns[ZNG_TRACE_CATEGORY_COUNT];
    uint64_t calls[ZNG_TRACE_CATEGORY_COUNT];
    uint64_t units[ZNG_TRACE_CATEGORY_COUNT];
    uint64_t bytes[ZNG_TRACE_CATEGORY_COUNT];
    uint64_t draw_bins[4];
    uint64_t batch_followers;
    uint64_t batch_runs;
    uint64_t batch_draws;
    uint64_t batch_max_run;
    uint64_t batch_state_breaks;
    uint64_t batch_signature_breaks;
} zng_trace_bucket_t;

static __thread zng_trace_bucket_t zng_bucket;
static __thread unsigned short zng_depth[ZNG_TRACE_CATEGORY_COUNT];
static __thread uint64_t zng_state_epoch = 1;
static __thread struct {
    uint64_t epoch;
    uint64_t program;
    uintptr_t vao;
    uint32_t mode;
    uint32_t type;
    uint32_t element_buffer;
    uint32_t framebuffer;
    uint64_t run_length;
    unsigned char valid;
} zng_previous_draw;

static uint64_t zng_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static long zng_thread_id(void) {
#if defined(__linux__) && defined(SYS_gettid)
    return (long)syscall(SYS_gettid);
#else
    return (long)getpid();
#endif
}

static void zng_write_line(const char* text, size_t length) {
    while (length) {
        ssize_t written = write(STDERR_FILENO, text, length);
        if (written <= 0) return;
        text += written;
        length -= (size_t)written;
    }
}

static void zng_emit_bucket(void) {
    if (!zng_bucket.bucket_id) return;

    // DRAW_DRIVER is nested in DRAW_TOTAL, so exclude it from the threshold sum. It remains in
    // the output: draw-total minus gles-draw is the conversion/setup share we need to distinguish.
    uint64_t covered_ns = 0;
    for (int i = 0; i < ZNG_TRACE_CATEGORY_COUNT; ++i)
        if (i != ZNG_TRACE_DRAW_DRIVER && i != ZNG_TRACE_DRAW_REALIZE)
            covered_ns += zng_bucket.total_ns[i];
    if (covered_ns < zng_trace_threshold_ns) return;

    uint64_t upload_bytes = zng_bucket.bytes[ZNG_TRACE_BUFFER]
            + zng_bucket.bytes[ZNG_TRACE_TEXTURE];
    char line[1024];
    int length = snprintf(line, sizeof(line),
            "[NGTRACE] bucket_ms=%llu tid=%ld covered_us=%llu "
            "draw=%llu/%llu/%llu glesdraw=%llu/%llu/%llu realize=%llu/%llu/%llu "
            "buffer=%llu/%llu/%llu texture=%llu/%llu/%llu "
            "program=%llu/%llu/%llu fbo=%llu/%llu/%llu sync=%llu/%llu/%llu "
            "draw_units=%llu upload_bytes=%llu draw_bins=%llu/%llu/%llu/%llu "
            "batch=%llu/%llu/%llu/%llu batch_breaks=%llu/%llu\n",
            (unsigned long long)((zng_bucket.bucket_id * ZNG_BUCKET_NS) / 1000000ull),
            zng_thread_id(), (unsigned long long)(covered_ns / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_DRAW_TOTAL] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_DRAW_TOTAL],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_DRAW_TOTAL] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_DRAW_DRIVER] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_DRAW_DRIVER],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_DRAW_DRIVER] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_DRAW_REALIZE] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_DRAW_REALIZE],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_DRAW_REALIZE] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_BUFFER] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_BUFFER],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_BUFFER] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_TEXTURE] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_TEXTURE],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_TEXTURE] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_PROGRAM] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_PROGRAM],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_PROGRAM] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_FBO] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_FBO],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_FBO] / 1000ull),
            (unsigned long long)(zng_bucket.total_ns[ZNG_TRACE_SYNC] / 1000ull),
            (unsigned long long)zng_bucket.calls[ZNG_TRACE_SYNC],
            (unsigned long long)(zng_bucket.max_ns[ZNG_TRACE_SYNC] / 1000ull),
            (unsigned long long)zng_bucket.units[ZNG_TRACE_DRAW_TOTAL],
            (unsigned long long)upload_bytes,
            (unsigned long long)zng_bucket.draw_bins[0],
            (unsigned long long)zng_bucket.draw_bins[1],
            (unsigned long long)zng_bucket.draw_bins[2],
            (unsigned long long)zng_bucket.draw_bins[3],
            (unsigned long long)zng_bucket.batch_followers,
            (unsigned long long)zng_bucket.batch_runs,
            (unsigned long long)zng_bucket.batch_draws,
            (unsigned long long)zng_bucket.batch_max_run,
            (unsigned long long)zng_bucket.batch_state_breaks,
            (unsigned long long)zng_bucket.batch_signature_breaks);
    if (length > 0) {
        size_t safe_length = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        zng_write_line(line, safe_length);
    }
}

int zomdroid_ngtrace_init(void) {
    int state = __atomic_load_n(&zomdroid_ngtrace_state, __ATOMIC_ACQUIRE);
    if (state >= 0) return state;
    if (state == -2) return 0; // another thread is doing the one-time environment read

    int expected = -1;
    if (!__atomic_compare_exchange_n(&zomdroid_ngtrace_state, &expected, -2, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return expected >= 0 ? expected : 0;

    const char* enabled_value = getenv("ZOMDROID_NG_TRACE");
    int enabled = enabled_value && atoi(enabled_value) != 0;
    if (enabled) {
        const char* threshold_value = getenv("ZOMDROID_NG_TRACE_THRESHOLD_US");
        if (threshold_value && *threshold_value) {
            unsigned long long threshold_us = strtoull(threshold_value, NULL, 10);
            if (threshold_us >= 1000ull && threshold_us <= 1000000ull)
                zng_trace_threshold_ns = threshold_us * 1000ull;
        }
    }
    __atomic_store_n(&zomdroid_ngtrace_state, enabled, __ATOMIC_RELEASE);

    if (enabled) {
        char line[192];
        int length = snprintf(line, sizeof(line),
                "[NGTRACE] enabled schema=2 bucket_us=%llu slow_bucket_us=%llu "
                "draw_bins=1-6/7-24/25-96/97+ "
                "batch=followers/runs/draws/max output=native.log\n",
                (unsigned long long)(ZNG_BUCKET_NS / 1000ull),
                (unsigned long long)(zng_trace_threshold_ns / 1000ull));
        if (length > 0) zng_write_line(line, (size_t)length);
    }
    return enabled;
}

zomdroid_ngtrace_scope_t zomdroid_ngtrace_begin_enabled(
        zomdroid_ngtrace_category_t category, uint64_t units, uint64_t bytes) {
    zomdroid_ngtrace_scope_t scope = {0};
    if ((unsigned)category >= ZNG_TRACE_CATEGORY_COUNT) return scope;
    scope.category = (unsigned char)category;
    scope.active = 1;
    scope.units = units;
    scope.bytes = bytes;
    // Uploads, program/FBO operations and explicit waits are ordering barriers.
    // DRAW_DRIVER and DRAW_REALIZE are nested measurements of the same draw.
    if (category != ZNG_TRACE_DRAW_TOTAL && category != ZNG_TRACE_DRAW_DRIVER &&
            category != ZNG_TRACE_DRAW_REALIZE)
        zomdroid_ngtrace_state_barrier_enabled();
    scope.record = (++zng_depth[category] == 1);
    if (scope.record) scope.start_ns = zng_now_ns();
    return scope;
}

void zomdroid_ngtrace_state_barrier_enabled(void) {
    if (++zng_state_epoch == 0) {
        zng_state_epoch = 1;
        zng_previous_draw.valid = 0;
    }
}

void zomdroid_ngtrace_draw_sample_enabled(
        uint32_t mode, uint32_t type, uint64_t units, uint64_t program,
        uintptr_t vao, uint32_t element_buffer, uint32_t framebuffer) {
    // The driver timing scope has just ended, so zng_bucket already refers to the
    // bucket containing this draw. Keep this function clock-free and allocation-free.
    unsigned bin = units <= 6 ? 0 : (units <= 24 ? 1 : (units <= 96 ? 2 : 3));
    zng_bucket.draw_bins[bin]++;

    // Some internal render-list paths call the FPE driver helper directly. Their
    // state changes bypass the public wrappers, so do not claim batch safety for
    // them. The histogram and timing remain useful; only candidacy is suppressed.
    if (!zng_depth[ZNG_TRACE_DRAW_TOTAL]) {
        zng_previous_draw.valid = 0;
        return;
    }

    int same_epoch = zng_previous_draw.valid && zng_previous_draw.epoch == zng_state_epoch;
    int same_signature = zng_previous_draw.valid &&
            zng_previous_draw.mode == mode && zng_previous_draw.type == type &&
            zng_previous_draw.program == program && zng_previous_draw.vao == vao &&
            zng_previous_draw.element_buffer == element_buffer &&
            zng_previous_draw.framebuffer == framebuffer;
    if (zng_previous_draw.valid && same_epoch && same_signature) {
        zng_bucket.batch_followers++;
        if (zng_previous_draw.run_length == 1) {
            zng_bucket.batch_runs++;
            zng_bucket.batch_draws += 2;
        } else {
            zng_bucket.batch_draws++;
        }
        zng_previous_draw.run_length++;
        if (zng_previous_draw.run_length > zng_bucket.batch_max_run)
            zng_bucket.batch_max_run = zng_previous_draw.run_length;
    } else {
        if (zng_previous_draw.valid) {
            if (!same_epoch) zng_bucket.batch_state_breaks++;
            else zng_bucket.batch_signature_breaks++;
        }
        zng_previous_draw.run_length = 1;
    }
    zng_previous_draw.epoch = zng_state_epoch;
    zng_previous_draw.mode = mode;
    zng_previous_draw.type = type;
    zng_previous_draw.program = program;
    zng_previous_draw.vao = vao;
    zng_previous_draw.element_buffer = element_buffer;
    zng_previous_draw.framebuffer = framebuffer;
    zng_previous_draw.valid = 1;
}

void zomdroid_ngtrace_end_enabled(zomdroid_ngtrace_scope_t* scope) {
    if (!scope || !scope->active) return;
    scope->active = 0;
    unsigned category = scope->category;
    if (category >= ZNG_TRACE_CATEGORY_COUNT) return;
    if (zng_depth[category]) --zng_depth[category];
    if (!scope->record) return;

    uint64_t now_ns = zng_now_ns();
    uint64_t elapsed_ns = now_ns - scope->start_ns;
    uint64_t bucket_id = now_ns / ZNG_BUCKET_NS;
    if (zng_bucket.bucket_id && zng_bucket.bucket_id != bucket_id) {
        zng_emit_bucket();
        memset(&zng_bucket, 0, sizeof(zng_bucket));
        // Keep candidate runs wholly inside one reported bucket. Otherwise a run
        // beginning in an omitted fast bucket would inflate a later slow bucket.
        zng_previous_draw.valid = 0;
    }
    zng_bucket.bucket_id = bucket_id;
    zng_bucket.total_ns[category] += elapsed_ns;
    zng_bucket.calls[category]++;
    zng_bucket.units[category] += scope->units;
    zng_bucket.bytes[category] += scope->bytes;
    if (elapsed_ns > zng_bucket.max_ns[category]) zng_bucket.max_ns[category] = elapsed_ns;
}

// ZOMDROID MEMDIAG: shadow-copy accounting + process RSS/Swap ground truth.
// The 42.19 world-gen deaths are memory deaths (LMK reclaim 1.9GB rss + 2.1GB swap,
// then malloc-abort with 325MB swap left). ZINK survives the same stand, so the
// delta is OUR overhead — this instrument shows where the gigabytes sit.
#include <string.h>
static long zshadow_total = 0;       // bytes currently held in buffer shadow copies
static long zshadow_last_logged = 0;
void zomdroid_memstat_tag(const char* tag) {
    long rss_kb = -1, swap_kb = -1;
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "VmRSS:", 6)) rss_kb = atol(line + 6);
            else if (!strncmp(line, "VmSwap:", 7)) swap_kb = atol(line + 7);
        }
        fclose(f);
    }
    zomdroid_gltrace("MEMSTAT %s shadow=%ldMB rss=%ldMB swap=%ldMB", tag, zshadow_total / (1024 * 1024),
                     rss_kb / 1024, swap_kb / 1024);
}
// Step logging with hysteresis (up 32MB, down 64MB) and its own line budget, so a
// total oscillating on a boundary can never chat its way into frame time.
static int zshadow_step_budget = 20;
void zomdroid_shadow_add(long n) {
    zshadow_total += n;
    if (zshadow_step_budget > 0 && zshadow_total - zshadow_last_logged >= 32L * 1024 * 1024) {
        zshadow_last_logged = zshadow_total;
        zshadow_step_budget--;
        zomdroid_memstat_tag("shadow-step");
    }
}
void zomdroid_shadow_sub(long n) {
    zshadow_total -= n;
    if (zshadow_total < 0) zshadow_total = 0;
    if (zshadow_step_budget > 0 && zshadow_last_logged - zshadow_total >= 64L * 1024 * 1024) {
        zshadow_last_logged = zshadow_total;
        zshadow_step_budget--;
        zomdroid_memstat_tag("shadow-step");
    }
}

// ZOMDROID DIAG (stutter hunt 2026-07-29): driver uploads from the unmap emulation
// are the prime suspect for Adreno stalls (SubData into an in-flight buffer forces an
// internal sync; PZ mapped with UNSYNCHRONIZED and expects none). Sites report here
// when a single upload takes >= 3 ms; budget-capped.
void zomdroid_slowsub(const char* tag, long ms, long size, unsigned buf) {
    static int zbudget = 40;
    if (zbudget <= 0) return;
    zbudget--;
    zomdroid_gltrace("SLOWSUB %s ms=%ld size=%ld buf=%u", tag, ms, size, buf);
}

// ZOMDROID GPU-ALLOC DIAG (2026-07-30): dumpsys caught GL mtrack at 4.99GB seconds
// before the lmkd kill on the 42.20 world load (EGL sane, JVM heap capped, shadows
// 64MB) — the killing mass is driver-side GL objects. This ledger tracks what the
// app REQUESTS through our entry points, per category and object id. live vs cum
// separates a leak (live grows, nothing deleted) from churn (live low but cum huge
// = respecification ghosts held by the driver).
#define ZGA_CATS 4 // 0=texture 1=buffer 2=renderbuffer 3=stream-scratch (cum only)
#define ZGA_CAP (1 << 18)
typedef struct {
    unsigned long long key; // (cat+1)<<32 | object id; 0 = empty slot
    long bytes;
} zga_ent_t;
static zga_ent_t* zga_tab = NULL;
static long zga_live[ZGA_CATS], zga_cum[ZGA_CATS], zga_cnt[ZGA_CATS];
static long zga_last_logged = 0;
static zga_ent_t* zga_slot(unsigned long long key) {
    if (!zga_tab) {
        zga_tab = (zga_ent_t*)calloc(ZGA_CAP, sizeof(zga_ent_t));
        if (!zga_tab) return NULL;
    }
    unsigned h = (unsigned)((key * 2654435761ull) >> 12) & (ZGA_CAP - 1);
    for (unsigned i = 0; i < ZGA_CAP; i++) {
        zga_ent_t* e = &zga_tab[(h + i) & (ZGA_CAP - 1)];
        if (e->key == key || e->key == 0) {
            e->key = key;
            return e;
        }
    }
    return NULL; // table full — stop accounting rather than loop
}
static void zga_report(const char* tag) {
    zomdroid_gltrace("GLALLOC %s live tex=%ldMB/%ld buf=%ldMB/%ld rb=%ldMB/%ld | cum tex=%ldMB buf=%ldMB rb=%ldMB scratch=%ldMB",
                     tag, zga_live[0] >> 20, zga_cnt[0], zga_live[1] >> 20, zga_cnt[1], zga_live[2] >> 20, zga_cnt[2],
                     zga_cum[0] >> 20, zga_cum[1] >> 20, zga_cum[2] >> 20, zga_cum[3] >> 20);
}
// step logging on 128MB movement of the explosion metric (live totals + scratch churn)
static void zga_check(void) {
    long total = zga_live[0] + zga_live[1] + zga_live[2] + zga_cum[3];
    long delta = total - zga_last_logged;
    if (delta < 0) delta = -delta;
    if (delta >= 128L * 1024 * 1024) {
        static int zbudget = 60;
        zga_last_logged = total;
        if (zbudget <= 0) return;
        zbudget--;
        zga_report("step");
    }
}
void zomdroid_glalloc_set(int cat, unsigned id, long bytes) {
    if (cat < 0 || cat >= ZGA_CATS || id == 0 || bytes < 0) return;
    zga_ent_t* e = zga_slot(((unsigned long long)(cat + 1) << 32) | id);
    if (!e) return;
    if (e->bytes == 0 && bytes > 0) zga_cnt[cat]++;
    if (e->bytes > 0 && bytes == 0) zga_cnt[cat]--;
    zga_live[cat] += bytes - e->bytes;
    zga_cum[cat] += bytes;
    e->bytes = bytes;
    zga_check();
}
void zomdroid_glalloc_del(int cat, unsigned id) {
    if (cat < 0 || cat >= ZGA_CATS || id == 0 || !zga_tab) return;
    zga_ent_t* e = zga_slot(((unsigned long long)(cat + 1) << 32) | id);
    if (e && e->bytes > 0) {
        zga_live[cat] -= e->bytes;
        zga_cnt[cat]--;
        e->bytes = 0;
    }
    zga_check();
}
// live texture bytes — the adaptive texture policy in texture.c reads this
long zomdroid_glalloc_live_tex(void) {
    return zga_live[0];
}
void zomdroid_glalloc_cum(int cat, long bytes) {
    if (cat < 0 || cat >= ZGA_CATS || bytes <= 0) return;
    zga_cum[cat] += bytes;
    zga_check();
}

// ZOMDROID DIAG: distinguishes "someone called exit()" from a signal kill — on the Mali
// tester the process dies at Bullet.init with NO signal traces anywhere; if this line
// shows up in the log, the death is a plain exit() inside emulated code (box64 territory).
static void zomdroid_exit_probe(void) {
    extern int zccache_hits, zccache_miss;
    extern long zomdroid_fbo_bind_app, zomdroid_fbo_bind_native, zomdroid_fbo_bind_skip, zomdroid_clearbuf_native;
    zomdroid_memstat_tag("exit");
    zga_report("exit");
    zomdroid_gltrace("FBO session: binds app=%ld native=%ld skipped=%ld clearbuf-native=%ld", zomdroid_fbo_bind_app,
                     zomdroid_fbo_bind_native, zomdroid_fbo_bind_skip, zomdroid_clearbuf_native);
    {
        extern long zdrawmix_total, zdrawmix_quads, zdrawmix_ebo, zdrawmix_direct, zdrawmix_inst;
        extern long zebo_used, zebo_try_de, zebo_try_dre;
        zomdroid_gltrace("DRAWMIX session: draws=%ld quads=%ld ebo=%ld direct-ok=%ld inst=%ld direct-taken=%ld"
                         " via-DrawElements=%ld via-RangeElements=%ld",
                         zdrawmix_total, zdrawmix_quads, zdrawmix_ebo, zdrawmix_direct, zdrawmix_inst, zebo_used,
                         zebo_try_de, zebo_try_dre);
    }
    {
        extern long zebo_batch_input, zebo_batch_driver, zebo_batch_saved, zebo_batch_runs;
        extern long zebo_batch_guard_fallback;
        if (zebo_batch_input || zebo_batch_guard_fallback)
            zomdroid_gltrace("EBOBATCH session: input=%ld driver=%ld saved=%ld runs=%ld guard-fallback=%ld",
                             zebo_batch_input, zebo_batch_driver, zebo_batch_saved, zebo_batch_runs,
                             zebo_batch_guard_fallback);
    }
    {
        extern long zomdroid_etc2_n, zomdroid_etc2_hits, zomdroid_etc2_evicted;
        extern unsigned long long zomdroid_etc2_total_ms(void);
        extern unsigned long long zomdroid_etc2_io_ms(void);
        if (zomdroid_etc2_n || zomdroid_etc2_hits)
            zomdroid_gltrace("ETC2 session: encodes=%ld cache-hits=%ld evicted=%ld encode-ms=%llu io-ms=%llu",
                             zomdroid_etc2_n, zomdroid_etc2_hits, zomdroid_etc2_evicted,
                             zomdroid_etc2_total_ms(), zomdroid_etc2_io_ms());
    }
    zomdroid_gltrace("CCACHE session: hits=%d miss=%d", zccache_hits, zccache_miss);
    zomdroid_gltrace("EXIT-PROBE: process exiting via exit(), not a signal kill");
}
void zomdroid_exit_probe_register(void) {
    static int done = 0;
    if (!done) {
        done = 1;
        atexit(zomdroid_exit_probe);
    }
}
