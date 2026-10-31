#pragma once

/*
 * sal_compat.h - SAL annotations placeholder for MinGW/GCC.
 *
 * MSVC provides SAL (__in, __out, etc.) in <sal.h>. These are pure
 * documentation. MinGW doesn't have SAL.
 *
 * IMPORTANT: We do NOT define __in, __out, __inout because MinGW's
 * libstdc++ uses __in and __out as internal variable names in <tuple>
 * and other STL headers. Defining them as macros would corrupt STL.
 *
 * The __in/__out/__inout annotations in the project source have been
 * manually stripped. Only less common SAL macros are defined here,
 * which don't conflict with MinGW STL.
 */

#ifndef _MSC_VER

#ifndef __out_bcount
#define __out_bcount(x)
#endif
#ifndef __out_bcount_part
#define __out_bcount_part(x, y)
#endif
#ifndef __out_bcount_part_opt
#define __out_bcount_part_opt(x, y)
#endif
#ifndef __out_ecount
#define __out_ecount(x)
#endif
#ifndef __out_ecount_part
#define __out_ecount_part(x, y)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(x)
#endif
#ifndef __out_ecount_part_opt
#define __out_ecount_part_opt(x, y)
#endif
#ifndef __deref_out
#define __deref_out
#endif
#ifndef __deref_out_opt
#define __deref_out_opt
#endif
#ifndef __deref_in
#define __deref_in
#endif
#ifndef __deref_in_opt
#define __deref_in_opt
#endif
#ifndef __deref_inout
#define __deref_inout
#endif
#ifndef __deref_inout_opt
#define __deref_inout_opt
#endif
#ifndef __bcount
#define __bcount(x)
#endif
#ifndef __bcount_opt
#define __bcount_opt(x)
#endif
#ifndef __ecount
#define __ecount(x)
#endif
#ifndef __ecount_opt
#define __ecount_opt(x)
#endif
#ifndef __ecount_part
#define __ecount_part(x, y)
#endif
#ifndef __ecount_part_opt
#define __ecount_part_opt(x, y)
#endif
#ifndef _Out_writes_
#define _Out_writes_(x)
#endif
#ifndef _Out_writes_opt_
#define _Out_writes_opt_(x)
#endif
#ifndef _Out_writes_part_
#define _Out_writes_part_(x, y)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(x)
#endif
#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(x)
#endif
#ifndef _Out_writes_all_
#define _Out_writes_all_(x)
#endif
#ifndef _Out_writes_to_
#define _Out_writes_to_(x, y)
#endif
#ifndef _Out_writes_to_opt_
#define _Out_writes_to_opt_(x, y)
#endif
#ifndef _Out_writes_all_opt_
#define _Out_writes_all_opt_(x)
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(x)
#endif
#ifndef _Field_size_bytes_opt_
#define _Field_size_bytes_opt_(x)
#endif
#ifndef _Field_size_bytes_part_
#define _Field_size_bytes_part_(x, y)
#endif

#endif /* !_MSC_VER */
