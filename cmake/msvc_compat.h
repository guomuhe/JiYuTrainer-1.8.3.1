#pragma once

/*
 * msvc_compat.h - Minimal MSVC safe function shims for MinGW/GCC
 *
 * Included via -include compile option for ALL translation units.
 * Only defines things that DON'T conflict with C++ standard library.
 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

#ifndef _MSC_VER

/* memcpy_s / strcpy_s / strcat_s */
#ifndef memcpy_s
#define memcpy_s(dest, destsz, src, count) memcpy((dest), (src), (count))
#endif
#ifndef strcpy_s
#define strcpy_s(dest, destsz, src) strcpy((dest), (src))
#endif
#ifndef strcat_s
#define strcat_s(dest, destsz, src) strcat((dest), (src))
#endif

/* _snprintf_s / _snwprintf_s */
#ifndef _snprintf_s
#define _snprintf_s(buf, bufsz, maxlen, ...) snprintf((buf), (maxlen), __VA_ARGS__)
#endif
#ifndef _snwprintf_s
#define _snwprintf_s(buf, bufsz, maxlen, ...) swprintf((buf), (maxlen), __VA_ARGS__)
#endif

/* _snprintf / _snwprintf */
#ifndef _snprintf
#define _snprintf snprintf
#endif
#ifndef _snwprintf
#define _snwprintf swprintf
#endif

/* _isnan / _finite (MSVC math) */
#ifndef _isnan
#define _isnan(x) isnan(x)
#endif
#ifndef _finite
#define _finite(x) (!isnan(x) && !isinf(x))
#endif

/* _wtoll / _wtoi / _wtoi64 - MinGW may not provide _prefix versions */
#ifndef _wtoll
#define _wtoll wtoll
#endif

/*
 * SEH stubs intentionally removed - they conflict with MinGW libstdc++
 * which defines __try as 'try' internally for C++ exception handling.
 * Any actual __try/__except usage in project source should be refactored
 * to standard C++ try/catch or removed.
 */

/*
 * GWL_WNDPROC etc. - MinGW undefs these in 64-bit builds (only GWLP_* kept).
 * Since this header is included AFTER <windows.h>, we can safely (re)define them.
 * Values are the same as GWLP_*; use with SetWindowLongPtr for 64-bit safety.
 */
#define GWL_WNDPROC   (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_USERDATA (-21)
#define GWL_ID       (-12)
#define GWL_STYLE    (-16)
#define GWL_EXSTYLE  (-20)

#endif /* !_MSC_VER */
