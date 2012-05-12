/* Copyright 2009 (C) Nicira, Inc.
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
#ifndef BITMAP_HH
#define BITMAP_HH 1

#include <list>

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#define ROUND_UP(X, Y) (((X) + ((Y) - 1)) / (Y) * (Y))

namespace vigil
{

/*
 * A bitmap useful for storing a set of booleans.  Not to be confused
 * with the BMP image file format.
 */
class Bitmap
{
public:
    Bitmap(size_t sz) : size(sz), data(new uint64_t[_data_len(sz)]) {}

    Bitmap(const Bitmap &other)
        : size(other.size), data(new uint64_t[_data_len(other.size)])
    {
        memcpy (data, other.data, _data_len(size)*sizeof(uint64_t));
    }

    ~Bitmap()
    {
        delete [] data;
    }

    size_t get_size() const
    {
        return size;
    }

    bool is_set(size_t offset) const;

    void set(size_t offset, bool value);
    void set_0(size_t offset);
    void set_1(size_t offset);

    bool operator==(const Bitmap& other) const;

    void operator&=(const Bitmap& other);
    void operator|=(const Bitmap& other);

    typedef std::list<size_t> IndexList;

    IndexList get_0_indices();
    IndexList get_1_indices();

private:
    static const uint8_t PART_BITS = sizeof(uint64_t) * 8;
    static const uint64_t PART_VALUES[];

    size_t size;
    uint64_t* data;

    static size_t _data_len(size_t bitlen);
    uint64_t* _get_part(size_t offset) const;
    uint8_t _get_part_bit(size_t offset) const;
};

/****************************************************************************/

inline bool
Bitmap::is_set(size_t offset) const
{
    assert(offset < size);
    return (*(_get_part(offset))) & PART_VALUES[_get_part_bit(offset)];
};

inline void
Bitmap::set(size_t offset, bool value)
{
    assert(offset < size);
    if (value)
    {
        set_1(offset);
    }
    else
    {
        set_0(offset);
    }
}

inline void
Bitmap::set_0(size_t offset)
{
    assert(offset < size);
    (*_get_part(offset)) &= ~PART_VALUES[_get_part_bit(offset)];
}

inline void
Bitmap::set_1(size_t offset)
{
    assert(offset < size);
    (*_get_part(offset)) |= PART_VALUES[_get_part_bit(offset)];
}

inline bool
Bitmap::operator==(const Bitmap& other) const
{
    if (size != other.size)
    {
        return false;
    }
    for (int i = 0; i < _data_len(size); ++i)
    {
        if (data[i] != other.data[i])
        {
            return false;
        }
    }
    return true;
}

inline void
Bitmap::operator&=(const Bitmap& other)
{
    int effsz = (size < other.size ? size : other.size);
    int i = 0;
    for (; i < _data_len(effsz); ++i)
    {
        data[i] &= other.data[i];
    }
    for (; i < _data_len(size); ++i)
    {
        data[i] = 0;
    }
}

inline void
Bitmap::operator|=(const Bitmap& other)
{
    int effsz = size < other.size ? size : other.size;
    for (int i = 0; i < _data_len(effsz); ++i)
    {
        data[i] |= other.data[i];
    }
}

inline Bitmap::IndexList
Bitmap::get_0_indices()
{
    IndexList ret;
    for (int i = 0; i < size; ++i)
    {
        if (!is_set(i))
        {
            ret.push_back(i);
        }
    }
    return ret;
}

inline Bitmap::IndexList
Bitmap::get_1_indices()
{
    IndexList ret;
    for (int i = 0; i < size; ++i)
    {
        if (is_set(i))
        {
            ret.push_back(i);
        }
    }
    return ret;
}

inline size_t
Bitmap::_data_len(size_t bitlen)
{
    return ROUND_UP(bitlen, PART_BITS)/PART_BITS;
}

inline uint64_t*
Bitmap::_get_part(size_t offset) const
{
    return &data[offset / PART_BITS];
}

inline uint8_t
Bitmap::_get_part_bit(size_t offset) const
{
    return offset % PART_BITS;
}

} /* namespace vigil */

#endif /* BITMAP_HH */
