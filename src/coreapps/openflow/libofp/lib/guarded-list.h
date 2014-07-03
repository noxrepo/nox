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

#ifndef GUARDED_LIST_H
#define GUARDED_LIST_H 1

#include <stddef.h>
#include "compiler.h"
#include "clist.h"
#include "ovs-thread.h"

struct guarded_list {
    struct ovs_mutex mutex;
    struct clist list;
    size_t n;
};

void guarded_list_init(struct guarded_list *);
void guarded_list_destroy(struct guarded_list *);

bool guarded_list_is_empty(const struct guarded_list *);

size_t guarded_list_push_back(struct guarded_list *, struct clist *,
                              size_t max);
struct clist *guarded_list_pop_front(struct guarded_list *);
size_t guarded_list_pop_all(struct guarded_list *, struct clist *);

#endif /* guarded-list.h */
