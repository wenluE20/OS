// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

static struct kmem kmems[NCPU];
static char kmemlockname[NCPU][16];

static void kfree_into_cpu(void *pa, int cpu);
static struct run *kalloc_from_cpu(int cpu);

void
kinit()
{
  for(int i = 0; i < NCPU; i++){
    snprintf(kmemlockname[i], sizeof(kmemlockname[i]), "kmem%d", i);
    initlock(&kmems[i].lock, kmemlockname[i]);
    kmems[i].freelist = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int cpu = 0;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree_into_cpu(p, cpu);
    cpu = (cpu + 1) % NCPU;
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  push_off();
  int id = cpuid();
  pop_off();

  kfree_into_cpu(pa, id);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id;

  push_off();
  id = cpuid();
  pop_off();

  r = kalloc_from_cpu(id);
  if(r == 0){
    for(int i = 0; i < NCPU; i++){
      if(i == id)
        continue;
      r = kalloc_from_cpu(i);
      if(r)
        break;
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

static void
kfree_into_cpu(void *pa, int cpu)
{
  struct run *r;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[cpu].lock);
  r->next = kmems[cpu].freelist;
  kmems[cpu].freelist = r;
  release(&kmems[cpu].lock);
}

static struct run *
kalloc_from_cpu(int cpu)
{
  struct run *r;

  acquire(&kmems[cpu].lock);
  r = kmems[cpu].freelist;
  if(r)
    kmems[cpu].freelist = r->next;
  release(&kmems[cpu].lock);
  return r;
}
