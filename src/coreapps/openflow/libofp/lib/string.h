/*
 * Copyright (c) 2009, 2011 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef STRING_WRAPPER_H
#define STRING_WRAPPER_H 1

#include_next <string.h>

/* Glibc 2.7 has a bug in strtok_r when compiling with optimization that can
 * cause segfaults if the delimiters argument is a compile-time constant that
 * has exactly 1 character:
 *
 *      http://sources.redhat.com/bugzilla/show_bug.cgi?id=5614
 *
 * The bug is only present in the inline version of strtok_r(), so force the
 * out-of-line version to be used instead. */
#if HAVE_STRTOK_R_BUG
#undef strtok_r
#endif

#ifndef HAVE_STRNLEN
#undef strnlen
#define strnlen rpl_strnlen
size_t strnlen(const char *, size_t maxlen);
#endif

#endif /* string.h wrapper */
