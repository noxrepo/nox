#ifndef NETWORK_ARCHIVE_HH
#define NETWORK_ARCHIVE_HH

#include <byteswap.h>

#include <boost/config.hpp>
#include <boost/static_assert.hpp>

#include <climits>

#if CHAR_BIT != 8
#error This code assumes an eight-bit byte.
#endif

#include <boost/archive/basic_archive.hpp>
#include <boost/detail/endian.hpp>

namespace vigil
{

enum network_archive_flags
{
    endian_big        = 0x4000,
    endian_little     = 0x8000
};

inline void
reverse_bytes(char size, char* address)
{
    char* first = address;
    char* last = first + size - 1;
    for (; first < last; ++first, --last)
    {
        char x = *last;
        *last = *first;
        *first = x;
    }
}

} // namespace vigil

#endif // NETWORK_ARCHIVE_HH
