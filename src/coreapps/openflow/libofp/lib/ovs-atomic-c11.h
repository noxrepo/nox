/*
 * Copyright (c) 2013 Nicira, Inc.
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

/* This header implements atomic operation primitives on compilers that
 * have built-in support for C11 <stdatomic.h>  */
#ifndef IN_OVS_ATOMIC_H
#error "This header should only be included indirectly via ovs-atomic.h."
#endif

#include <stdatomic.h>

/* Nonstandard atomic types. */
typedef _Atomic(uint8_t)   atomic_uint8_t;
typedef _Atomic(uint16_t)  atomic_uint16_t;
typedef _Atomic(uint32_t)  atomic_uint32_t;
typedef _Atomic(uint64_t)  atomic_uint64_t;

typedef _Atomic(int8_t)    atomic_int8_t;
typedef _Atomic(int16_t)   atomic_int16_t;
typedef _Atomic(int32_t)   atomic_int32_t;
typedef _Atomic(int64_t)   atomic_int64_t;

#define of_atomic_read(SRC, DST) \
    of_atomic_read_explicit(SRC, DST, memory_order_seq_cst)
#define of_atomic_read_explicit(SRC, DST, ORDER)   \
    (*(DST) = atomic_load_explicit(SRC, ORDER), \
     (void) 0)

#define of_atomic_add(RMW, ARG, ORIG) \
    of_atomic_add_explicit(RMW, ARG, ORIG, memory_order_seq_cst)
#define of_atomic_sub(RMW, ARG, ORIG) \
    of_atomic_sub_explicit(RMW, ARG, ORIG, memory_order_seq_cst)
#define of_atomic_or(RMW, ARG, ORIG) \
    of_atomic_or_explicit(RMW, ARG, ORIG, memory_order_seq_cst)
#define of_atomic_xor(RMW, ARG, ORIG) \
    of_atomic_xor_explicit(RMW, ARG, ORIG, memory_order_seq_cst)
#define of_atomic_and(RMW, ARG, ORIG) \
    of_atomic_and_explicit(RMW, ARG, ORIG, memory_order_seq_cst)

#define of_atomic_add_explicit(RMW, ARG, ORIG, ORDER) \
    (*(ORIG) = atomic_fetch_add_explicit(RMW, ARG, ORDER), (void) 0)
#define of_atomic_sub_explicit(RMW, ARG, ORIG, ORDER) \
    (*(ORIG) = atomic_fetch_sub_explicit(RMW, ARG, ORDER), (void) 0)
#define of_atomic_or_explicit(RMW, ARG, ORIG, ORDER) \
    (*(ORIG) = atomic_fetch_or_explicit(RMW, ARG, ORDER), (void) 0)
#define of_atomic_xor_explicit(RMW, ARG, ORIG, ORDER) \
    (*(ORIG) = atomic_fetch_xor_explicit(RMW, ARG, ORDER), (void) 0)
#define of_atomic_and_explicit(RMW, ARG, ORIG, ORDER) \
    (*(ORIG) = atomic_fetch_and_explicit(RMW, ARG, ORDER), (void) 0)
