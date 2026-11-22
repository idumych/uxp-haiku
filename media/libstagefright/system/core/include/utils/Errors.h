/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_ERRORS_H
#define ANDROID_ERRORS_H

#include <sys/types.h>
#include <stdint.h>
#include <errno.h>

// Haiku's errno values are already negative (and -ENOMEM would overflow).
#ifdef __HAIKU__
#define STAGEFRIGHT_NEG_ERRNO(e) (e)
#else
#define STAGEFRIGHT_NEG_ERRNO(e) (-(e))
#endif

namespace stagefright {

// use this type to return error codes
#ifdef HAVE_MS_C_RUNTIME
typedef int         status_t;
#else
typedef int32_t     status_t;
#endif

/* the MS C runtime lacks a few error codes */

/*
 * Error codes. 
 * All error codes are negative values.
 */

// Win32 #defines NO_ERROR as well.  It has the same value, so there's no
// real conflict, though it's a bit awkward.
#ifdef _WIN32
# undef NO_ERROR
#endif
 
enum {
    OK                = 0,    // Everything's swell.
    NO_ERROR          = 0,    // No errors.
    
    UNKNOWN_ERROR       = 0x80000000,

    NO_MEMORY           = STAGEFRIGHT_NEG_ERRNO(ENOMEM),
    INVALID_OPERATION   = STAGEFRIGHT_NEG_ERRNO(ENOSYS),
    BAD_VALUE           = STAGEFRIGHT_NEG_ERRNO(EINVAL),
    BAD_TYPE            = 0x80000001,
    NAME_NOT_FOUND      = STAGEFRIGHT_NEG_ERRNO(ENOENT),
    PERMISSION_DENIED   = STAGEFRIGHT_NEG_ERRNO(EPERM),
    NO_INIT             = STAGEFRIGHT_NEG_ERRNO(ENODEV),
    ALREADY_EXISTS      = STAGEFRIGHT_NEG_ERRNO(EEXIST),
    DEAD_OBJECT         = STAGEFRIGHT_NEG_ERRNO(EPIPE),
    FAILED_TRANSACTION  = 0x80000002,
    JPARKS_BROKE_IT     = STAGEFRIGHT_NEG_ERRNO(EPIPE),
#if !defined(HAVE_MS_C_RUNTIME)
    BAD_INDEX           = STAGEFRIGHT_NEG_ERRNO(EOVERFLOW),
    NOT_ENOUGH_DATA     = STAGEFRIGHT_NEG_ERRNO(ENODATA),
    WOULD_BLOCK         = STAGEFRIGHT_NEG_ERRNO(EWOULDBLOCK), 
    TIMED_OUT           = STAGEFRIGHT_NEG_ERRNO(ETIMEDOUT),
    UNKNOWN_TRANSACTION = STAGEFRIGHT_NEG_ERRNO(EBADMSG),
#else    
    BAD_INDEX           = STAGEFRIGHT_NEG_ERRNO(E2BIG),
    NOT_ENOUGH_DATA     = 0x80000003,
    WOULD_BLOCK         = 0x80000004,
    TIMED_OUT           = 0x80000005,
    UNKNOWN_TRANSACTION = 0x80000006,
#endif    
    FDS_NOT_ALLOWED     = 0x80000007,
};

// Restore define; enumeration is in "android" namespace, so the value defined
// there won't work for Win32 code in a different namespace.
#ifdef _WIN32
# define NO_ERROR 0L
#endif

}; // namespace stagefright
    
// ---------------------------------------------------------------------------
    
#endif // ANDROID_ERRORS_H
