/* Copyright (c) 2008, 2009, 2010 Nicira, Inc.
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

#ifndef UUID_H
#define UUID_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "util.h"

#define UUID_BIT 128            /* Number of bits in a UUID. */
#define UUID_OCTET (UUID_BIT / 8) /* Number of bytes in a UUID. */

/* A Universally Unique IDentifier (UUID) compliant with RFC 4122.
 *
 * Each of the parts is stored in host byte order, but the parts themselves are
 * ordered from left to right.  That is, (parts[0] >> 24) is the first 8 bits
 * of the UUID when output in the standard form, and (parts[3] & 0xff) is the
 * final 8 bits. */
struct uuid {
    uint32_t parts[4];
};
BUILD_ASSERT_DECL(sizeof(struct uuid) == UUID_OCTET);

/* Formats a UUID as a string, in the conventional format.
 *
 * Example:
 *   struct uuid uuid = ...;
 *   printf("This UUID is "UUID_FMT"\n", UUID_ARGS(&uuid));
 *
 */
#define UUID_LEN 36
#define UUID_FMT "%08x-%04x-%04x-%04x-%04x%08x"
#define UUID_ARGS(UUID)                             \
    ((unsigned int) ((UUID)->parts[0])),            \
    ((unsigned int) ((UUID)->parts[1] >> 16)),      \
    ((unsigned int) ((UUID)->parts[1] & 0xffff)),   \
    ((unsigned int) ((UUID)->parts[2] >> 16)),      \
    ((unsigned int) ((UUID)->parts[2] & 0xffff)),   \
    ((unsigned int) ((UUID)->parts[3]))

/* Returns a hash value for 'uuid'.  This hash value is the same regardless of
 * whether we are running on a 32-bit or 64-bit or big-endian or little-endian
 * architecture. */
static inline size_t
uuid_hash(const struct uuid *uuid)
{
    return uuid->parts[0];
}

/* Returns true if 'a == b', false otherwise. */
static inline bool
uuid_equals(const struct uuid *a, const struct uuid *b)
{
    return (a->parts[0] == b->parts[0]
            && a->parts[1] == b->parts[1]
            && a->parts[2] == b->parts[2]
            && a->parts[3] == b->parts[3]);
}

void uuid_init(void);
void uuid_generate(struct uuid *);
void uuid_zero(struct uuid *);
bool uuid_is_zero(const struct uuid *);
int uuid_compare_3way(const struct uuid *, const struct uuid *);
bool uuid_from_string(struct uuid *, const char *);
bool uuid_from_string_prefix(struct uuid *, const char *);

#endif /* uuid.h */
