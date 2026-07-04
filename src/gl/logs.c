#include "logs.h"
#include "init.h"
#include <stdarg.h>
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
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fflush(f);
    count++;
}

