/*
 * Does the actual de-duplication
 */
#include "dedup.h"
#include "duptable.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "fscanner.h"
#include "utils.h"
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "calchash.h"
#include "hcache.h"

#define BLKSZ	4096
/*
 * First run... for the duptab based on size, get all the candidates
 * (cnt > 1)
 */
struct duptab *dedup_cluster(struct duptab *in) {
  struct duptab *out = duptab_new();
  ino_t *inos;
  int icnt, i;
  struct stat st;

  for (inos = duptab_first(in,&icnt,&st);
       inos != NULL ;
       inos = duptab_next(in,&icnt,&st)) {
    if (icnt < 2) continue;
    for (i=0;i<icnt;i++) {
      st.st_ino = inos[i]; // Add i-nodes to new table...
      duptab_add(out, &st, 0, NULL);
    }
  }
  if (duptab_count(out) == 0) out = duptab_free(out);
  return out;
}

/*
 * Final pass
 */
static void dedup_final(struct fs_dat *fs,struct dedup_cb *cb, struct stat *stp) {
  ino_t *inos;
  int icnt;

  for (inos = duptab_first(fs->dtab,&icnt,stp) ;
       inos != NULL ;
       inos = duptab_next(fs->dtab,&icnt,stp)) {
    if (icnt < 2) continue;
    cb->do_dedup(fs,inos,icnt,stp,cb->ext);
  }
}
/*
 * PASS3: check hashes
 */
static void dedup_pass3(struct fs_dat *fs,struct dedup_cb *cb, struct stat *stp) {
  ino_t *inos;
  int icnt;

  for (inos = duptab_first(fs->dtab,&icnt,stp) ;
       inos != NULL ;
       inos = duptab_next(fs->dtab,&icnt,stp)) {
    if (icnt < 2) continue;
    if (stp->st_size <= (BLKSZ+BLKSZ)) {
      // OK files are smaller than BLKSZ*2, therefore we already compared
      // all contents
      cb->do_dedup(fs,inos,icnt,stp,cb->ext);
      continue;
    }
    struct duptab *dups = duptab_new();
    char **fpt, *vn, *hash;
    for (int i = 0; i < icnt; i++) {
      stp->st_ino = inos[i];
      fpt = inodetab_get(fs->itab,inos[i],&stp->st_mtime);
      if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[i]);

      vn = mystrcat(fs->root,"/",*fpt);
      if (fs->cache) {
	hcache_get(fs->cache,stp,&hash);
	if (hash == NULL) {
	  // Compute and cache hash
	  hash = hash_file(vn);
	  hcache_put(fs->cache,stp,hash);
	}
      } else {
	hash = hash_file(vn);
      }
      free(vn);
      duptab_add(dups, stp, hash_len(), hash);
    }
    // Check new cluster...
    dedup_final(fs,cb,stp);
    duptab_free(dups);
  }
}
/*
 * PASS2: check last bytes
 */
static void dedup_pass2(struct fs_dat *fs,struct dedup_cb *cb, struct stat *stp) {
  ino_t *inos;
  int icnt;

  for (inos = duptab_first(fs->dtab,&icnt,stp) ;
       inos != NULL ;
       inos = duptab_next(fs->dtab,&icnt,stp)) {
    if (icnt < 2) continue;
    if (stp->st_size <= BLKSZ) {
      // OK files are smaller than BLKSZ, therefore we already compared
      cb->do_dedup(fs,inos,icnt,stp,cb->ext);
      continue;
    }
    struct duptab *dups = duptab_new();
    int i, fd, len;
    char **fpt, *vn;
    for (i = 0; i < icnt; i++) {
      char buf[BLKSZ]; 
      stp->st_ino = inos[i];
      fpt = inodetab_get(fs->itab,inos[i],&stp->st_mtime);
      if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[i]);
      vn = mystrcat(fs->root,"/",*fpt);
      if ((fd = open(vn,O_RDONLY)) != -1) {
	if (stp->st_size <= (BLKSZ+BLKSZ)) {
	  if (lseek(fd,BLKSZ,SEEK_SET) == (off_t)-1) errormsg("lseek(%s)",vn);
	} else {
	  if (lseek(fd,-BLKSZ,SEEK_END) == (off_t)-1) errormsg("lseek(%s)",vn);
	}
	len = read(fd,buf,BLKSZ);
	switch(len) {
	case 0:
	  fatal(ENODATA,"read(%s): no data", vn);
	case -1:
	  errormsg("read(%s)",vn);
	}
	close(fd);
      } else errormsg("open(%s)", vn);
      free(vn);
      duptab_add(dups, stp, len, buf);
    }
    // Check new cluster...
    dedup_pass3(fs,cb,stp);
    duptab_free(dups);
  }
}
/*
 * PASS1: check first bytes
 */
static void dedup_pass1(struct fs_dat *fs,struct dedup_cb *cb, struct stat *stp) {
  ino_t *inos;
  int icnt;

  for (inos = duptab_first(fs->dtab,&icnt,stp) ;
       inos != NULL ;
       inos = duptab_next(fs->dtab,&icnt,stp)) {
    if (icnt < 2) continue;
    struct duptab *dups = duptab_new();
    int i, fd, len;
    char **fpt, *vn;
    for (i = 0; i < icnt; i++) {
      char buf[BLKSZ];

      stp->st_ino = inos[i];
      fpt = inodetab_get(fs->itab,inos[i],&stp->st_mtime);
      if (fpt == NULL || *fpt==NULL) fatal(ENOENT,"Missing i-node %llx",(long long)inos[i]);
      vn = mystrcat(fs->root,"/",*fpt);
      if ((fd = open(vn,O_RDONLY)) != -1) {
	len = read(fd,buf,BLKSZ);
	switch(len) {
	case 0:
	  fatal(ENODATA,"read(%s): no data", vn);
	case -1:
	  errormsg("read(%s)",vn);
	}
	close(fd);
      } else errormsg("open(%s)", vn);
      free(vn);
      duptab_add(dups, stp, len, buf);
    }
    // Check new cluster...
    dedup_pass2(fs,cb,stp);
    duptab_free(dups);
  }
}
/*
 * Initial entry point
 */
void dedup_pass(struct fs_dat *fs,struct dedup_cb *cb) {
  struct stat st;
  dedup_pass1(fs,cb,&st);
}
