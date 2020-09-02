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
#include <cu.h>
#include "dedup.h"
#include "test.h"
#include "inodetab.h"
#include "duptable.h"
#include "calchash.h"
#include "utils.h"
#include <malloc.h>
#include <string.h>
#include <stdlib.h>

#define BLKSZ	4096

TEST(dedup_cluster_test) {
  hash_set(CH_HASH_TYPE);

  char *base, tpl[] = "/tmp/tmpdirXXXXXXX";
  base = mkdtemp(tpl);
  assertTrue(base);
  if (base == NULL) return;
  mkfile(base,"msg1.txt","one");
  mkfile(base,"msg1a.txt","one");
  mkfile(base,"msg2.txt","two");
  mkfile(base,"msg3.txt","one1");
  mkfile(base,"msg3a.txt","one1");
  mklink(base,"msg3a.txt","msg3a.lnk");
  mkfile(base,"abc.txt","datafactor");
  mkfile(base,"abcd.txt","datafactors");

  struct fs_dat fs;
  fscanner_init(&fs, base, false);
  fscanner(&fs, NULL);

  assertEquals(inodetab_count(fs.itab),7);
  struct duptab *clusters = dedup_cluster(fs.dtab);
  assertTrue(clusters);
  if (!clusters) return;
  assertEquals(inodetab_count(fs.itab),7);

  duptab_free(fs.dtab); // We are only interested in the clusters!
  fs.dtab = clusters;
  duptab_sort(fs.dtab); // Sort by size....
  assertEquals(duptab_count(fs.dtab),2);

  fscanner_close(&fs);
  rm_rf(base);
}

#define MAXWORDS 1024
static char *words[MAXWORDS];
static int cmpstringp(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
    pointers to char", but strcmp(3) arguments are "pointers
    to char", hence the following cast plus dereference */
  return strcmp(* (char * const *) p1, * (char * const *) p2);
}
#define MAXLINES 1024
struct lines_t {
  int start;
  int count;
};
static struct lines_t lines[MAXLINES];
static int cmplines(const void *p1, const void *p2) {
  int i, r;
  struct lines_t *l1 = (struct lines_t *)p1;
  struct lines_t *l2 = (struct lines_t *)p2;
  for (i=0; i < l1->count && i < l2->count ; i++) {
    r = strcmp(words[l1->start+i],words[l2->start+i]);
    if (r) return r;
  }
  return l1->count - l2->count;
}


static void do_dedup(struct fs_dat *fs,ino_t *inos,int icnt,struct stat *stp,void *ext) {
  char **fpt;
  int wk = 0, lk = 0, i;
  for (i = 0; i < icnt; i++) {
    fpt = inodetab_get(fs->itab,inos[i],NULL);
    if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[i]);
    int first = wk;
    while (*fpt) {
      words[wk++] = *(fpt++);
    }
    qsort(&words[first],wk - first,sizeof(char *),cmpstringp);
    lines[lk].start = first;
    lines[lk].count = wk - first;
    lk++;
  }
  qsort(lines, lk, sizeof(struct lines_t), cmplines);
  for (i = 0; i < lk ; i++) {
    printf("%d:", i);
    for (int j = 0; j < lines[i].count ; j++) {
      putchar(' ');
      fputs(words[lines[i].start+j],stdout);
    }
    putchar('\n');
  }
  if(stp) printf("STP!=NULL\n");
  if(ext) printf("EXT!=NULL\n");
}

static void dedup_test(const char *msg,char *base) {
  struct fs_dat fs;

  printf("TEST: %s\n",msg);
  fscanner_init(&fs, base, false);
  fscanner(&fs, NULL);

  assertEquals(inodetab_count(fs.itab),3);
  struct duptab *clusters = dedup_cluster(fs.dtab);
  assertTrue(clusters);
  if (!clusters) return;
  assertEquals(inodetab_count(fs.itab),3);

  duptab_free(fs.dtab); // We are only interested in the clusters!
  fs.dtab = clusters;
  duptab_sort(fs.dtab); // Sort by size....
  assertEquals(duptab_count(fs.dtab),1);

  struct dedup_cb dpcb = { .do_dedup = do_dedup, .ext = NULL };
  dedup_pass(&fs,&dpcb);

  fscanner_close(&fs);
}

TEST(dedup_undup1) {
  hash_set(CH_HASH_TYPE);
#ifdef __GLIBC__
  struct mallinfo m1, m2;
  m1 = mallinfo();
#endif

  static const char bytes[]="abcdefghijklmopqrstuvwxyzABCDEFGHIJKLMOPQRSTUVWXYZ0123456789";
  char *base, tpl[] = "/tmp/tmpdirXXXXXXX", buf[BLKSZ*3];
  base = mkdtemp(tpl);
  assertTrue(base);
  if (base == NULL) return;

  // Fill the buffer with dummy data...
  unsigned i;
  for (i=0;i< sizeof(buf)-1;i++) {
    buf[i] = bytes[i % (sizeof(bytes)-1)];
  }
  buf[i] = 0;

  mkfile(base,"msg1.txt","one");
  mkfile(base,"msg1a.txt","one");
  mkfile(base,"msg2.txt","two");

  dedup_test("# Small files",base);

  rm_rf(base); if (mkdir(base,0777) == -1) return;
  mkfile(base,"msg1.txt",buf);
  mkfile(base,"msg1a.txt",buf);
  buf[BLKSZ*2+BLKSZ/2] = 'q';
  buf[BLKSZ*2+BLKSZ/2+1] = 'x';
  mkfile(base,"msg2.txt",buf);

  dedup_test("# large files-tail end",base);

  rm_rf(base); if (mkdir(base,0777) == -1) return;
  mkfile(base,"msg1.txt",buf);
  mkfile(base,"msg1a.txt",buf);
  buf[BLKSZ+BLKSZ/2] = 'q';
  buf[BLKSZ+BLKSZ/2+1] = 'x';
  mkfile(base,"msg2.txt",buf);

  dedup_test("# large files-middle",base);

  rm_rf(base); if (mkdir(base,0777) == -1) return;
  buf[BLKSZ+BLKSZ/2] = '\0';
  mkfile(base,"msg1.txt",buf);
  mkfile(base,"msg1a.txt",buf);
  buf[BLKSZ+BLKSZ/2-1] = 'q';
  buf[BLKSZ*2+BLKSZ/2-2] = 'x';
  mkfile(base,"msg2.txt",buf);
  dedup_test("# medium files",base);

  rm_rf(base);

// Comment out this test, but...
#ifdef __GLIBC__
  m2 = mallinfo();
  //~ assertEquals(m1.uordblks,m2.uordblks);
#endif
}
