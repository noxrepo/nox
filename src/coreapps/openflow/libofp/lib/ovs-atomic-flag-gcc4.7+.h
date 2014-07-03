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

/* This header implements of_atomic_flag on Clang and on GCC 4.7 and later. */
#ifndef IN_OVS_ATOMIC_H
#error "This header should only be included indirectly via ovs-atomic.h."
#endif

/* of_atomic_flag */

typedef struct {
    unsigned char b;
} of_atomic_flag;
#define OF_ATOMIC_FLAG_INIT { .b = false }

static inline bool
of_atomic_flag_test_and_set_explicit(volatile of_atomic_flag *object,
                                  memory_order order)
{
    return __atomic_test_and_set(&object->b, order);
}

static inline bool
of_atomic_flag_test_and_set(volatile of_atomic_flag *object)
{
    return of_atomic_flag_test_and_set_explicit(object, memory_order_seq_cst);
}

static inline void
of_atomic_flag_clear_explicit(volatile of_atomic_flag *object, memory_order order)
{
    __atomic_clear(object, order);
}

static inline void
of_atomic_flag_clear(volatile of_atomic_flag *object)
{
    of_atomic_flag_clear_explicit(object, memory_order_seq_cst);
}
