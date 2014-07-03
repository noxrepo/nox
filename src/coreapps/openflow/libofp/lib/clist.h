/*
 * Copyright (c) 2008, 2009, 2010, 2011, 2013 Nicira, Inc.
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
#ifndef LIST_H
#define LIST_H 1

/* Doubly linked list. */

#include <stdbool.h>
#include <stddef.h>
#include "util.h"

/* Doubly linked clist.head or element. */
struct clist {
    struct clist *prev;     /* Previous list element. */
    struct clist *next;     /* Next list element. */
};

#define LIST_INITIALIZER(LIST) { LIST, LIST }

void list_init(struct clist *);
void list_poison(struct clist *);

/* List insertion. */
void list_insert(struct clist *, struct clist *);
void list_splice(struct clist *before, struct clist *first, struct clist *last);
void list_push_front(struct clist *, struct clist *);
void list_push_back(struct clist *, struct clist *);
void list_replace(struct clist *, const struct clist *);
void list_moved(struct clist *);
void list_move(struct clist *dst, struct clist *src);

/* List removal. */
struct clist *list_remove(struct clist *);
struct clist *list_pop_front(struct clist *);
struct clist *list_pop_back(struct clist *);

/* List elements. */
struct clist *list_front(const struct clist *);
struct clist *list_back(const struct clist *);

/* List properties. */
size_t list_size(const struct clist *);
bool list_is_empty(const struct clist *);
bool list_is_singleton(const struct clist *);
bool list_is_short(const struct clist *);

#define LIST_FOR_EACH(ITER, MEMBER, LIST)                               \
    for (ASSIGN_CONTAINER(ITER, (LIST)->next, MEMBER);                  \
         &(ITER)->MEMBER != (LIST);                                     \
         ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.next, MEMBER))
#define LIST_FOR_EACH_CONTINUE(ITER, MEMBER, LIST)                      \
    for (ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.next, MEMBER);           \
         &(ITER)->MEMBER != (LIST);                                     \
         ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.next, MEMBER))
#define LIST_FOR_EACH_REVERSE(ITER, MEMBER, LIST)                       \
    for (ASSIGN_CONTAINER(ITER, (LIST)->prev, MEMBER);                  \
         &(ITER)->MEMBER != (LIST);                                     \
         ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.prev, MEMBER))
#define LIST_FOR_EACH_REVERSE_CONTINUE(ITER, MEMBER, LIST)              \
    for (ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.prev, MEMBER);           \
         &(ITER)->MEMBER != (LIST);                                     \
         ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.prev, MEMBER))
#define LIST_FOR_EACH_SAFE(ITER, NEXT, MEMBER, LIST)               \
    for (ASSIGN_CONTAINER(ITER, (LIST)->next, MEMBER);             \
         (&(ITER)->MEMBER != (LIST)                                \
          ? ASSIGN_CONTAINER(NEXT, (ITER)->MEMBER.next, MEMBER), 1 \
          : 0);                                                    \
         (ITER) = (NEXT))

#endif /* clist.h */
