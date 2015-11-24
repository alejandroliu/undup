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
const char version[] = "2.0.0";
#include "undup.h"
#include "dedup.h"
#include "utils.h"
#ifdef _DEBUG
#include <mcheck.h>
#endif
#include "calchash.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include "fscanner.h"
#include <unistd.h>
#include "human_readable.h"

struct undup_opts gopts;

struct dedup_stats {
  int files;
  int blocks;
};

static const char *filetype(int mode) {
  switch (mode & S_IFMT) {
  case S_IFSOCK: return "soc";
  case S_IFLNK: return "lnk";
  case S_IFREG: return "reg";
  case S_IFBLK: return "blk";
  case S_IFDIR: return "dir";
  case S_IFCHR: return "chr";
  case S_IFIFO: return "pip";
  }
  return "???";
}

static void catcb(char *dir, char *file,struct stat *stdat,void *ext) {
  FILE *fp = (FILE *)ext;
  fprintf(fp, "%d %s %03o n:%d u:%d g:%d s:%llu b:%d ts:%d %s%s%s\n",
	  (int)stdat->st_ino, filetype(stdat->st_mode),
	  stdat->st_mode & ~S_IFMT,
	  (int)stdat->st_nlink, stdat->st_uid, stdat->st_gid,
	  (unsigned long long)stdat->st_size, (int)stdat->st_blocks,
	  (int)stdat->st_mtime,
	  dir, dir[0] ? "/" : "", file);
}

/* Sorts inodes so oldest is first */
static struct inodetab *idx;
static int dedup_cmp(const void *a,const void *b) {
  ino_t ino_a = *((ino_t *)a);
  ino_t ino_b = *((ino_t *)b);
  time_t mtime_a, mtime_b;

  if (!inodetab_get(idx,ino_a,&mtime_a)) return 0;
  if (!inodetab_get(idx,ino_b,&mtime_b)) return 0;

  if (mtime_a < mtime_b) return -1;
  if (mtime_a > mtime_b) return 1;
  return 0;
}
static void do_dedup(struct fs_dat *fs,ino_t *inos,int icnt,struct stat *stp,void *ext) {
  struct dedup_stats *s = (struct dedup_stats *)ext;
  int i;
  char **fpt, *basepath, *vpath;
  struct stat stb;
  idx = fs->itab;
  qsort(inos,icnt,sizeof(ino_t),dedup_cmp);

  fpt = inodetab_get(fs->itab,inos[0],&stp->st_mtime);
  stb.st_ino = inos[0];
  if (gopts.verbose > 1) {
    printf("- (%llx):u=%d g=%d m=%03o (%s):%s\n",
	   (long long)inos[0], stp->st_uid, stp->st_gid, stp->st_mode,
	   make_human_readable_str(stp->st_size,0,0), *fpt);
  }

  if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[0]);
  basepath = mystrcat(fs->root,"/",*fpt,NULL);

  for (i = 1; i < icnt; i++) {
    fpt = inodetab_get(fs->itab,inos[i],&stp->st_mtime);
    if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[i]);
    while (*fpt) {
      if (gopts.verbose > 1) {
	printf("    -> (%llx):%s\n", (long long)inos[i], *fpt);
      }
      vpath = mystrcat(fs->root,"/",*(fpt++),NULL);
      if (stb.st_ino != inos[i]) {
	/* Compute statistics... */
	if (lstat(vpath,&stb) == -1) errormsg("lstat(%s)",vpath);
	s->blocks += stb.st_blocks;
      }
      s->files++;
      if (!gopts.dryrun) {
	if (unlink(vpath)==-1) errormsg("unlink(%s)",vpath);
	if (link(basepath,vpath)==-1) errormsg("link(%s->%s)",basepath,vpath);
      } else {
	if (gopts.verbose > 2) {
	  fprintf(stderr,"Linking %s -> %s\n", vpath, basepath);
	}
      }
      free(vpath);
    }
  }
  free(basepath);
}

static void show_version() {
#ifdef _DEBUG
  printf("undup v%s (DEBUG)\n",version);
#else
  printf("undup v%s\n",version);
#endif
  exit(0);
}

int undup_main(int argc,char **argv) {
  char *root;
  FILE *catfp = NULL;
  int opt;
  struct cat_cb cb = { .callback = catcb, .ext = NULL }, *cbp = NULL;
  struct fs_dat fs;
  struct dedup_stats stats;
  struct dedup_cb dpcb = { .do_dedup = do_dedup, .ext = &stats };

#ifdef _DEBUG
  mtrace();
#endif
  memset(&gopts,0,sizeof(struct undup_opts));
  gopts.dryrun = true;
  gopts.usecache = true;
  gopts.verbose = 1;
  hash_set(CH_HASH_TYPE);

  while ((opt = getopt(argc,argv,"h?Vqv5SemCKsc:l:")) != -1) {
    switch (opt) {
    case 'c':
      if (catfp) fclose(catfp);
      catfp = fopen(optarg,"w");
      if (catfp) {
	cb.ext = (void *)catfp;
	cbp = &cb;
      } else {
	perror(optarg);
	cbp = NULL;
      }
      break;
    case 'l':
      lockfile(optarg);
      break;
    case 'e':
      gopts.dryrun = false;
      break;
    case 'C':
      gopts.usecache = false;
      break;
    case 'K':
      gopts.cstats = true;
      break;
    case 'm':
      gopts.mstats = true;
      break;
    case '5':
      hash_set(CH_MD5);
      break;
    case 'S':
      hash_set(CH_SHA256);
      break;
    case 's':
      gopts.scanonly = true;
      break;
    case 'v':
      gopts.verbose++;
      break;
    case 'q':
      gopts.verbose = 0;
      break;
    case 'V':
      show_version();
    case 'h':
    case '?':
    default: /* '?' */
      //++
      // = UNDUP(1)
      // :Author: A Liu Ly
      // :Revision: 2.0
      //
      // == NAME
      //
      // undup - tracks duplicate files and merges them with hardlinks
      //
      // == SYNOPSIS
      //
      fprintf(stderr,"Usage: %s [options] dir\n", argv[0]);
      // *undup* [options] [directory]
      //
      // == DESCRIPTION
      //
      // *undup* examines the contents of a filesystem (directory) searching
      // for file duplicates.  When found, it saves diskspace by replacing
      // repeated files with hardlinks.
      //
      // == OPTIONS
      //
      fputs("\t-c catalogue: create a file catalogue\n",stderr);
      // *-c* catalogue::
      //    create a file catalogue
      fputs("\t-l lockfile: create a exclusive lock\n",stderr);
      // *-l* lockfile::
      //    create an exclusive lock (to avoid overruning)
      fputs("\t-C: disable hash caching\n",stderr);
      // *-C*::
      //    disables hash caching
      fputs("\t-e: execute (disables dry-run mode)\n",stderr);
      // *-e*::
      //    creates hardlinks (disables the default, dry-run mode)
      fputs("\t-m: shows memory stats\n",stderr);
      // *-m*::
      //    Shows memory statistics
      fputs("\t-K: shows cache stats\n",stderr);
      // *-K*::
      //    Shows caching stats
      fputs("\t-s: scan only\n",stderr);
      // *-s*::
      //    only scans the file system
      fputs("\t-5: use MD5 hashes\n",stderr);
      // *-5*::
      //    use MD5 for hashes
      fputs("\t-S: use SHA256 hashes\n",stderr);
      // *-S*::
      //    use SHA256 for hashes
      fputs("\t-q: supress additional info\n",stderr);
      // *-q*::
      //    quiet mode
      fputs("\t-v: show additional info\n",stderr);
      // *-v*::
      //    verbose mode
      fputs("\t-V: version info\n",stderr);
      // *-V*::
      //    show version info
      fputs("\t-h|?: this help message\n",stderr);
      // *-h*::
      //    show help information
      //--
      exit(EXIT_FAILURE);
    }
  }
  if (optind >= argc) {
    if (gopts.verbose > 1) show_version();
    fputs("FATAL: Expected arguments after options\n",stderr);
    exit(EXIT_FAILURE);
  }
  vmsg("Using hash: %s\n", hash_name());
  trimslashes(root = argv[optind]);
  if (gopts.scanonly) {
    vmsg("[SCAN-ONLY] ");
  }
  vmsg("Scanning %s\n", root);

  fscanner_init(&fs, root, gopts.usecache);
  fscanner(&fs, cbp);
  if (catfp) fclose(catfp);
  vmsg("Files found: %d\n", inodetab_count(fs.itab));
  struct duptab *clusters = dedup_cluster(fs.dtab);
  if (clusters == NULL) {
    vmsg("No size clusters found!\n");
  } else {
    //ckptm("%llx\n",(long long unsigned)clusters);
    vmsg("Size clusters found: %d\n", duptab_count(clusters));
  }
  if (clusters) {
    if (!gopts.scanonly) {
      duptab_free(fs.dtab); // We are only interested in the clusters!
      fs.dtab = clusters;
      duptab_sort(fs.dtab); // Sort by size....

      memset(&stats,0,sizeof(struct dedup_stats));
      dedup_pass(&fs,&dpcb);

      vmsg("Files de-duped: %d\n", stats.files);
      vmsg("Storage freed: %s\n", make_human_readable_str(stats.blocks,512,0));
    } else {
      duptab_free(clusters);
    }
  }
  if (fs.cache && gopts.cstats) {
    int hits,misses;
    hcache_stats(fs.cache,&hits,&misses);
    if (hits || misses) {
      fprintf(stderr,"Hash cache: %d:%d (%d%% hit ratio)\n",hits,misses,
	     hits * 100 / (hits + misses));
    }
  }

  if (gopts.mstats) {
    struct mallinfo mi;
    malloc_stats();
    mi = mallinfo();
    vmsg("Allocated memory: %s\n", make_human_readable_str(mi.uordblks,0,0));
  }
  fscanner_close(&fs);
#ifdef _DEBUG
  muntrace();
  if (gopts.mstats) {
    struct mallinfo mi;
    mi = mallinfo();
    if (mi.uordblks) {
      fprintf(stderr,"Leaked memory: %s\n",
	      make_human_readable_str(mi.uordblks,0,0));
    }
  }
#endif
  return 0;
}
