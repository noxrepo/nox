/* Copyright 2008 (C) Nicira, Inc.
 *
 * This file is part of NOX.
 *
 * NOX is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * NOX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NOX.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HASH_HH
#define HASH_HH 1

#include <limits.h>

/* G++ 4.2 has working std::tr1::unordered_map and std::tr1::unordered_set, so
 * in such an environment template specializations for hash<> need to go in
 * std::tr1.
 *
 * G++ before 4.2 has __gnu_cxx::hash_map and __gnu_cxx::hash_set, and in those
 * environments specializations for hash<> need to go in __gnu_cxx.
 *
 * Fortunately, the hash<> template works the same way regardless of where it
 * goes, so this is the only change needed.
 */
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 1)
#include <tr1/functional>
#define HASH_NAMESPACE std::tr1
#define ENTER_HASH_NAMESPACE namespace std { namespace tr1 {
#define EXIT_HASH_NAMESPACE } }
#else /* G++ before 4.2 */
#include <ext/hash_fun.h>

#define HASH_NAMESPACE __gnu_cxx
#define ENTER_HASH_NAMESPACE namespace __gnu_cxx {
#define EXIT_HASH_NAMESPACE }

/* Here's an example of how to use this mechanism.  It's needed here
 * because G++ 4.1 doesn't have handle long long types. */
ENTER_HASH_NAMESPACE

#if SIZE_MAX < ULLONG_MAX
#define LLMIX(X) ((X) ^ ((X) >> 32))
#else
#define LLMIX(X) (X)
#endif

template <>
struct hash<unsigned long long>
{
    size_t operator()(unsigned long long x) const
    {
        return LLMIX(x);
    }
};

template <>
struct hash<long long>
{
    size_t operator()(long long x) const
    {
        return LLMIX(x);
    }
};

#undef LLMIX

EXIT_HASH_NAMESPACE
#endif

#endif /* hash.hh */
