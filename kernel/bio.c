// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 53

struct {
  struct spinlock bcachebucketlock[NBUCKETS];
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS];
} bcache;

static int bcache_buf_bucket[NBUF];

static inline int
buf_index(struct buf *b)
{
  return b - bcache.buf;
}

static void
bucket_insert(int bucket, struct buf *b)
{
  b->next = bcache.hashbucket[bucket].next;
  b->prev = &bcache.hashbucket[bucket];
  bcache.hashbucket[bucket].next->prev = b;
  bcache.hashbucket[bucket].next = b;
  bcache_buf_bucket[buf_index(b)] = bucket;
}

static void
bucket_remove(struct buf *b)
{
  b->next->prev = b->prev;
  b->prev->next = b->next;
  bcache_buf_bucket[buf_index(b)] = -1;
}

static inline int
bcache_bucket(uint dev, uint blockno)
{
  return (dev ^ blockno) % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  for(int i = 0; i < NBUCKETS; i++){
    initlock(&bcache.bcachebucketlock[i], "bcachebucket");
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    b->valid = 0;
    b->refcnt = 0;
    int bucket = (b - bcache.buf) % NBUCKETS;
    bucket_insert(bucket, b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = bcache_bucket(dev, blockno);

  acquire(&bcache.bcachebucketlock[bucket]);

  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bcachebucketlock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->refcnt == 0) {
      b->refcnt = 1;
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->next->prev = b->prev;
      b->prev->next = b->next;
      bucket_insert(bucket, b);
      release(&bcache.bcachebucketlock[bucket]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bcachebucketlock[bucket]);

  struct buf *victim = 0;
  int victim_bucket = -1;

  for(int offset = 1; offset <= NBUCKETS; offset++){
    int idx = (bucket + offset) % NBUCKETS;
    acquire(&bcache.bcachebucketlock[idx]);
    for(b = bcache.hashbucket[idx].next; b != &bcache.hashbucket[idx]; b = b->next){
      if(b->refcnt == 0) {
        victim = b;
        victim_bucket = idx;
        bucket_remove(b);
        break;
      }
    }
    release(&bcache.bcachebucketlock[idx]);
    if(victim)
      break;
  }

  if(victim == 0)
    panic("bget: no buffers");

  acquire(&bcache.bcachebucketlock[bucket]);
  for(b = bcache.hashbucket[bucket].next; b != &bcache.hashbucket[bucket]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bcachebucketlock[bucket]);

      acquire(&bcache.bcachebucketlock[victim_bucket]);
      bucket_insert(victim_bucket, victim);
      release(&bcache.bcachebucketlock[victim_bucket]);

      acquiresleep(&b->lock);
      return b;
    }
  }

  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  bucket_insert(bucket, victim);
  release(&bcache.bcachebucketlock[bucket]);

  acquiresleep(&victim->lock);
  return victim;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket = bcache_bucket(b->dev, b->blockno);
  acquire(&bcache.bcachebucketlock[bucket]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
    bucket_insert(bucket, b);
  }
  release(&bcache.bcachebucketlock[bucket]);
}

void
bpin(struct buf *b) {
  int bucket = bcache_bucket(b->dev, b->blockno);
  acquire(&bcache.bcachebucketlock[bucket]);
  b->refcnt++;
  release(&bcache.bcachebucketlock[bucket]);
}

void
bunpin(struct buf *b) {
  int bucket = bcache_bucket(b->dev, b->blockno);
  acquire(&bcache.bcachebucketlock[bucket]);
  b->refcnt--;
  release(&bcache.bcachebucketlock[bucket]);
}
