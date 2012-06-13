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
#ifndef STABLE_LIST_HH
#define STABLE_LIST_HH 1

#include <map>
#include <list>

namespace vigil
{

/*
 * Stable_list
 *
 *  List of key/value pairs which doesn't invalidate iterators during
 *  deletion (until the purge(..) method is called.  This code is
 *  largely copied from stable_map.hh.
 *
 */
template <class Key, class Value>
class Stable_list
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

    typedef std::list<std::pair<Key, mapped_type> > Container;
public:

    struct iterator
    {
    public:
        iterator() { }
        iterator(const iterator& that) : i(that.i) { }
        iterator(const typename Container::iterator& i,
                 const typename Container::iterator& end);
        iterator& operator=(const iterator& that)
        {
            i = that.i;
            return *this;
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
    bool insert(const Key&, const Value&);
    bool erase(const Key&);
    void purge();

    iterator find(const Key&);
    iterator begin();
    iterator end();
private:
    Container c;
};

template <class Key, class Value>
inline
typename Stable_list<Key, Value>::iterator  Stable_list<Key, Value>::find(const Key& key)
{
    typename Container::iterator i = c.begin();

    for (; i != c.end(); ++i)
    {
        if (i->first == key && !i->second.erased)
        {
            return iterator(i, c.end());
        }
    }
    return end();
}

template <class Key, class Value>
inline
bool Stable_list<Key, Value>::insert(const Key& key, const Value& value)
{
    typename Container::iterator i = c.begin();

    for (; i != c.end(); ++i)
    {
        if (i->first == key)
        {
            break;
        }
    }

    if (i == c.end())
    {
        c.push_back(std::make_pair(key, mapped_type(false, value)));
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
inline
bool Stable_list<Key, Value>::erase(const Key& key)
{
    typename Container::iterator i = c.begin();

    for (; i != c.end(); ++i)
    {
        if (i->first == key)
        {
            break;
        }
    }

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
inline
typename Stable_list<Key, Value>::iterator Stable_list<Key, Value>::begin()
{
    return iterator(c.begin(), c.end());
}

template <class Key, class Value>
inline
typename Stable_list<Key, Value>::iterator Stable_list<Key, Value>::end()
{
    return iterator(c.end(), c.end());
}

template <class Key, class Value>
inline
typename Stable_list<Key, Value>::iterator&
Stable_list<Key, Value>::iterator::operator++()
{
    ++i;
    skip_erased();
    return *this;
}

template <class Key, class Value>
inline
typename Stable_list<Key, Value>::iterator
Stable_list<Key, Value>::iterator::operator++(int)
{
    iterator old(*this);
    ++*this;
    return old;
}

template <class Key, class Value>
inline
Stable_list<Key, Value>::iterator::iterator(const typename Container::iterator& i_,
                                            const typename Container::iterator& end_)
    : i(i_), end(end_)
{
    skip_erased();
}

template <class Key, class Value>
inline
void Stable_list<Key, Value>::iterator::skip_erased()
{
    while (i != end && i->second.erased)
    {
        ++i;
    }
}

template <class Key, class Value>
inline
void Stable_list<Key, Value>::purge()
{
    for (typename Container::iterator i = c.begin(); i != c.end();)
    {
        if (i->second.erased)
        {
            i = c.erase(i);
        }
        else
        {
            ++i;
        }
    }
}


} // namespace vigil


#endif /* Stable_list.hh */
