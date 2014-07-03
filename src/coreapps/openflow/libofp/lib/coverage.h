/*
 * Copyright (c) 2009, 2010, 2011, 2012, 2013 Nicira, Inc.
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

#ifndef COVERAGE_H
#define COVERAGE_H 1

/* This file implements a simple form of coverage instrumentation.  Points in
 * source code that are of interest must be explicitly annotated with
 * COVERAGE_INC.  The coverage counters may be logged at any time with
 * coverage_log().
 *
 * This form of coverage instrumentation is intended to be so lightweight that
 * it can be enabled in production builds.  It is obviously not a substitute
 * for traditional coverage instrumentation with e.g. "gcov", but it is still
 * a useful debugging tool. */

#include "ovs-thread.h"
#include "vlog.h"

/* A coverage counter. */
struct coverage_counter {
    const char *const name;            /* Textual name. */
    unsigned int (*const count)(void); /* Gets, zeros this thread's count. */
    unsigned long long int total;      /* Total count. */
};

/* Defines COUNTER.  There must be exactly one such definition at file scope
 * within a program. */
#if USE_LINKER_SECTIONS
#define COVERAGE_DEFINE(COUNTER)                                        \
        DEFINE_STATIC_PER_THREAD_DATA(unsigned int,                     \
                                      counter_##COUNTER, 0);            \
        static unsigned int COUNTER##_count(void)                       \
        {                                                               \
            unsigned int *countp = counter_##COUNTER##_get();           \
            unsigned int count = *countp;                               \
            *countp = 0;                                                \
            return count;                                               \
        }                                                               \
        static inline void COUNTER##_add(unsigned int n)                \
        {                                                               \
            *counter_##COUNTER##_get() += n;                            \
        }                                                               \
        extern struct coverage_counter counter_##COUNTER;               \
        struct coverage_counter counter_##COUNTER                       \
            = { #COUNTER, COUNTER##_count, 0 };                         \
        extern struct coverage_counter *counter_ptr_##COUNTER;          \
        struct coverage_counter *counter_ptr_##COUNTER                  \
            __attribute__((section("coverage"))) = &counter_##COUNTER
#else
#define COVERAGE_DEFINE(COUNTER)                                        \
        DECLARE_EXTERN_PER_THREAD_DATA(unsigned int,                    \
                                       counter_##COUNTER);              \
        static inline void COUNTER##_add(unsigned int n)                \
        {                                                               \
            *counter_##COUNTER##_get() += n;                            \
        }                                                               \
        extern struct coverage_counter counter_##COUNTER
#endif

/* Adds 1 to COUNTER. */
#define COVERAGE_INC(COUNTER) COVERAGE_ADD(COUNTER, 1)

/* Adds AMOUNT to COUNTER. */
#define COVERAGE_ADD(COUNTER, AMOUNT) COUNTER##_add(AMOUNT)

void coverage_init(void);
void coverage_log(void);
void coverage_clear(void);

/* Implementation detail. */
#define COVERAGE_DEFINE__(COUNTER)                              \

#endif /* coverage.h */
