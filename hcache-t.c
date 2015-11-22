#include <cu.h>
#include "hcache.h"
#include <string.h>
#include "utils.h"

static struct stat *mkstat(struct stat *stp,ino_t ino,uid_t uid,gid_t gid,off_t size,mode_t mode,time_t mtime) {
  memset(stp,0,sizeof(struct stat));
  stp->st_ino = ino;
  stp->st_uid = uid;
  stp->st_gid = gid;
  stp->st_size = size;
  stp->st_mode = mode;
  stp->st_mtime = mtime;
  return stp;
}

#define hash_type '\x01'
#define hash_len  8

TEST(hcache_checked) {
  char fn[] = "lckXXXXXX", *cf;
  assertTrue(mktemp(fn));
  unlink(fn);
  struct hcache *cache;

  struct stat stb;
  char hash1[] = "1.0xcafe";
  char hash2[] = "20xcafeb";
  char *hcode;

  cache = hcache_new(fn, hash_type,hash_len);
  assertTrue(cache);
  hcache_validate(cache,mkstat(&stb,/*ino*/0,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971));
  hcache_put(cache,mkstat(&stb,/*ino*/1,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), hash1);
  hcache_put(cache,mkstat(&stb,/*ino*/2,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), hash2);
  hcache_put(cache,mkstat(&stb,/*ino*/3,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), hash1);
  hcache_put(cache,mkstat(&stb,/*ino*/4,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), hash2);
  hcache_free(cache);

  cache = hcache_new(fn, hash_type,hash_len);
  hcache_validate(cache,mkstat(&stb,/*ino*/1,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971));
  hcache_validate(cache,mkstat(&stb,/*ino*/2,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971));

  assertTrue(hcache_get(cache,mkstat(&stb,/*ino*/1,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), &hcode));
  assertTrue(!memcmp(hcode,hash1,hash_len));
  assertTrue(hcache_get(cache,mkstat(&stb,/*ino*/2,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), &hcode));
  assertTrue(!memcmp(hcode,hash2,hash_len));
  assertTrue(hcache_get(cache,mkstat(&stb,/*ino*/2,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), NULL));

  assertFalse(hcache_get(cache,mkstat(&stb,/*ino*/1,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301972), &hcode));
  assertFalse(hcode);
  assertFalse(hcache_get(cache,mkstat(&stb,/*ino*/3,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), NULL));
  assertFalse(hcache_get(cache,mkstat(&stb,/*ino*/4,/*uid*/0,/*gid*/0,/*sz*/0,/*mode*/0777,/*time*/4301971), NULL));
  hcache_free(cache);

  cache = hcache_new(fn, hash_type,hash_len);
  cf = mystrdup(hcache_getpath(cache));
  hcache_free(cache);
  unlink(cf);
  free(cf);
}