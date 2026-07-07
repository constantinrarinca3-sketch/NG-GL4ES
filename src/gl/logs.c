#include "logs.h"
#include "init.h"
#include <stdarg.h>
#include <stdlib.h>
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
    if (!init) { init = 1; f = fopen("/data/data/com.zomdroid/files/gl_trace.txt", "w"); }
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
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

// ZOMDROID DIAG: distinguishes "someone called exit()" from a signal kill — on the Mali
// tester the process dies at Bullet.init with NO signal traces anywhere; if this line
// shows up in the log, the death is a plain exit() inside emulated code (box64 territory).
static void zomdroid_exit_probe(void) {
    zomdroid_gltrace("EXIT-PROBE: process exiting via exit(), not a signal kill");
}
void zomdroid_exit_probe_register(void) {
    static int done = 0;
    if (!done) {
        done = 1;
        atexit(zomdroid_exit_probe);
    }
}

