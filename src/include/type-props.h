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
#ifndef TYPE_PROPS_H
#define TYPE_PROPS_H 1

#include <limits.h>

#define TYPE_IS_INTEGER(TYPE) ((TYPE) 1.5 == (TYPE) 1)
#define TYPE_IS_SIGNED(TYPE) ((TYPE) 0 > (TYPE) -1)
#define TYPE_VALUE_BITS(TYPE) (sizeof(TYPE) * CHAR_BIT - TYPE_IS_SIGNED(TYPE))
#define TYPE_MINIMUM(TYPE) (TYPE_IS_SIGNED(TYPE) \
                            ? ~(TYPE)0 << TYPE_VALUE_BITS(TYPE) \
                            : 0)
#define TYPE_MAXIMUM(TYPE) (TYPE_IS_SIGNED(TYPE) \
                            ? ~(~(TYPE)0 << TYPE_VALUE_BITS(TYPE)) \
                            : (TYPE)-1)

#endif /* type-props.h */
