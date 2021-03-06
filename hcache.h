/*
 *    This file is part of undup
 *    Copyright (C) 2015, Alejandro Liu
 *
 *    undup is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    undup is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, see <http://www.gnu.org/licenses>
 */
#ifndef _HCACHE_H
#define _HCACHE_H
#include <sys/types.h>
#include <sys/stat.h>

struct hcache;
struct hcache *hcache_new(const char *base,int type,int len);
struct hcache *hcache_free(struct hcache *cache);
void hcache_validate(struct hcache *cache, struct stat *st);
int hcache_get(struct hcache *cache, struct stat *st,char **hash);
void hcache_put(struct hcache *cache, struct stat *st,char *hash);
void hcache_del(struct hcache *cache, struct stat *st);
const char *hcache_getpath(struct hcache *cache);

void hcache_stats(struct hcache *cache,int *hits, int *misses);
#endif
