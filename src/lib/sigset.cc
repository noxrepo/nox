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
#include "sigset.hh"

namespace vigil
{

/* Initializes 'this' as an empty signal set. */
Sigset::Sigset()
{
    sigemptyset(&ss);
}

/* Initializes 'this' to contain the signals in 'ss'. */
Sigset::Sigset(const sigset_t& ss_)
    : ss(ss_)
{
}

/* Clears all the signals from 'this'. */
void
Sigset::clear()
{
    sigemptyset(&ss);
}

/* Adds all the signals to 'this'. */
void
Sigset::fill()
{
    sigfillset(&ss);
}

/* Returns the lowest-numbered signal that is a member of 'this' and greater
 * than or equal to 'start', or 0 if no signal greater than or equal to 'start'
 * is is a member of 'this'.  */
int
Sigset::scan(int start) const
{
    const int n_signals = n_sig();
    int i;
    for (i = start; i < n_signals; ++i)
    {
        if (sigismember(&ss, i))
        {
            return i;
        }
    }
    return i;
}

/* Returns true if 'sig_nr' is a member of 'this', false otherwise. */
bool
Sigset::contains(int sig_nr) const
{
    return sigismember(&ss, sig_nr);
}

/* Add 'sig_nr' to 'this'. */
void
Sigset::add(int sig_nr)
{
    sigaddset(&ss, sig_nr);
}

/* Removes 'sig_nr' to 'this'. */
void
Sigset::remove(int sig_nr)
{
    sigdelset(&ss, sig_nr);
}

/* Returns the sigset_t used as a basis for 'this'. */
const sigset_t&
Sigset::sigset() const
{
    return ss;
}

/* Adds all the signals in 'rhs' to 'this'. */
const Sigset&
Sigset::operator|=(const Sigset& rhs)
{
    const int n_signals = n_sig();
    for (int i = 1; i < n_signals; ++i)
    {
        if (rhs.contains(i))
        {
            add(i);
        }
    }
    return *this;
}

/* Removes all the signals not in 'rhs' from 'this'. */
const Sigset&
Sigset::operator&=(const Sigset& rhs)
{
    const int n_signals = n_sig();
    for (int i = 1; i < n_signals; ++i)
    {
        if (!rhs.contains(i))
        {
            remove(i);
        }
    }
    return *this;
}

/* Returns the number of signals supported by the system. */
int
Sigset::n_sig()
{
#ifdef NSIG
    /* This is a common extension. */
    return NSIG;
#else
    /* This should be portable. */
    static int n_signals = 0;
    if (n_signals == 0)
    {
        sigset_t signals;
        sigemptyset(&signals);
        for (int sig_nr = 1; ; ++sig_nr)
        {
            if (sigaddset(&signals, sig_nr))
            {
                n_signals = sig_nr;
                break;
            }
        }
    }
    return n_signals;
#endif
}

/* Returns the union of the signals in 'lhs' and 'rhs'. */
Sigset
operator|(const Sigset& lhs, const Sigset& rhs)
{
    Sigset result = lhs;
    result |= rhs;
    return result;
}

/* Returns the set of signals that are in both 'lhs' and 'rhs'. */
Sigset
operator&(const Sigset& lhs, const Sigset& rhs)
{
    Sigset result = lhs;
    result &= rhs;
    return result;
}

} // namespace vigil
