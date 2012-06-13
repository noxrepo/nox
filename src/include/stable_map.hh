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
#ifndef STABLE_MAP_HH
#define STABLE_MAP_HH 1

#include <map>

namespace vigil
{

/*
 * Stable_map is much like std::map, except that deleting an element does not
 * in itself invalidate any iterators.  Call the purge member function to
 * finish deletion (and thereby invalidate iterators).
 *
 * ("Stable_map" is not a particularly good name.  Got a better one?)
 */
template <class Key, class Value>
class Stable_map
{
private:
    struct mapped_type
    {
        bool erased;
        Value value;

        mapped_type(bool erased_, Value value_)
            : erased(erased_), value(value_) { }
    };

    typedef std::pair<Key, Value> value_type;

    class value_type_ptr
    {
    public:
        const value_type v;
        value_type_ptr(const value_type& v_) : v(v_) { }
        const value_type* operator->()
        {
            return &v;
        }
    };

    typedef std::map<Key, mapped_type> Container;
public:

    struct iterator
    {
    public:
        iterator() { }
        iterator(const iterator& that) : i(that.i) { }
        iterator(typename Container::iterator i,
                 typename Container::iterator end);
        iterator& operator=(const iterator& that)
        {
            i = that.i;
        }

        bool operator==(const iterator& that)
        {
            return i == that.i;
        }
        bool operator!=(const iterator& that)
        {
            return !(*this == that);
        }

        value_type operator*() const
        {
            return std::make_pair(i->first, i->second.value);
        }
        value_type_ptr operator->() const
        {
            return value_type_ptr(**this);
        }

        iterator& operator++();
        iterator operator++(int);

        void erase()
        {
            i->second.erased = true;
        }
    private:
        typename Container::iterator i;
        typename Container::iterator end;

        void skip_erased();
    };

    bool empty() const
    {
        return c.empty();
    }
    bool insert(Key, Value);
    bool erase(Key);
    void purge();
    iterator find(Key);
    iterator begin();
    iterator end();
private:
    Container c;
};

template <class Key, class Value>
bool Stable_map<Key, Value>::insert(Key key, Value value)
{
    typename Container::iterator i = c.find(key);
    if (i == c.end())
    {
        c.insert(std::make_pair(key, mapped_type(false, value)));
        return true;
    }
    else if (i->second.erased)
    {
        i->second.erased = false;
        i->second.value = value;
        return true;
    }
    else
    {
        return false;
    }
}

template <class Key, class Value>
bool Stable_map<Key, Value>::erase(Key key)
{
    typename Container::iterator i = c.find(key);
    if (i != c.end() && !i->second.erased)
    {
        i->second.erased = true;
        return true;
    }
    else
    {
        return false;
    }
}

template <class Key, class Value>
void Stable_map<Key, Value>::purge()
{
    for (typename Container::iterator i = c.begin(); i != c.end();)
    {
        typename Container::iterator j = i++;
        if (j->second.erased)
        {
            c.erase(j);
        }
    }
}

template <class Key, class Value>
typename Stable_map<Key, Value>::iterator Stable_map<Key, Value>::find(Key key)
{
    typename Container::iterator i = c.find(key);
    return i != c.end() && !i->second.erased ? iterator(i, c.end()) : end();
}

template <class Key, class Value>
typename Stable_map<Key, Value>::iterator Stable_map<Key, Value>::begin()
{
    return iterator(c.begin(), c.end());
}

template <class Key, class Value>
typename Stable_map<Key, Value>::iterator Stable_map<Key, Value>::end()
{
    return iterator(c.end(), c.end());
}

template <class Key, class Value>
typename Stable_map<Key, Value>::iterator&
Stable_map<Key, Value>::iterator::operator++()
{
    ++i;
    skip_erased();
    return *this;
}

template <class Key, class Value>
typename Stable_map<Key, Value>::iterator
Stable_map<Key, Value>::iterator::operator++(int)
{
    iterator old(*this);
    ++*this;
    return old;
}

template <class Key, class Value>
Stable_map<Key, Value>::iterator::iterator(typename Container::iterator i_,
                                           typename Container::iterator end_)
    : i(i_), end(end_)
{
    skip_erased();
}

template <class Key, class Value>
void Stable_map<Key, Value>::iterator::skip_erased()
{
    while (i != end && i->second.erased)
    {
        ++i;
    }
}

} // namespace vigil

#endif /* stable_map.hh */
