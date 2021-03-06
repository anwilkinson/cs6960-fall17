
// pipe.c - modified by Alex Steele

#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

void mymemcpy(void *dst, const void *src, uint n)
{
  char *d = dst;
  const char *s = src;
  uint eights = n / 8;
  uint singles = n % 8;

  while (eights-- > 0) {
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
    *d++ = *s++;
  }

  while (singles--) {
    *d++ = *s++;
  }
}

#define PIPESIZE 4028

struct pipe {
  char data[PIPESIZE];
  struct spinlock lock;
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *p;

  p = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((p = (struct pipe*)kalloc()) == 0)
    goto bad;
  p->readopen = 1;
  p->writeopen = 1;
  p->nwrite = 0;
  p->nread = 0;
  initlock(&p->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = p;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = p;
  return 0;

//PAGEBREAK: 20
 bad:
  if(p)
    kfree((char*)p);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *p, int writable)
{
  acquire(&p->lock);
  if(writable){
    p->writeopen = 0;
    wakeup(&p->nread);
  } else {
    p->readopen = 0;
    wakeup(&p->nwrite);
  }
  if(p->readopen == 0 && p->writeopen == 0){
    release(&p->lock);
    kfree((char*)p);
  } else
    release(&p->lock);
}

//PAGEBREAK: 40
int
pipewrite(struct pipe *p, char *addr, int n)
{
  int nw = 0;
  int x;

  acquire(&p->lock);
  while (nw < n) {
    while(p->nwrite == p->nread + PIPESIZE){  //DOC: pipewrite-full
      if(p->readopen == 0 || myproc()->killed){
        release(&p->lock);
        return -1;
      }
      wakeup(&p->nread);
      sleep(&p->nwrite, &p->lock);  //DOC: pipewrite-sleep
    }
    x = n - nw;
    if (PIPESIZE + p->nread - p->nwrite < x) {
      x = PIPESIZE + p->nread - p->nwrite;
    }
    if (PIPESIZE - (p->nwrite % PIPESIZE) < x) {
      x = PIPESIZE - (p->nwrite % PIPESIZE);
    }
    mymemcpy(p->data + (p->nwrite % PIPESIZE), addr + nw, x);
    p->nwrite += x;
    nw += x;
  }
  wakeup(&p->nread);  //DOC: pipewrite-wakeup1
  release(&p->lock);
  return n;
}

int
piperead(struct pipe *p, char *addr, int n)
{
  int nr;
  int x;

  acquire(&p->lock);
  while(p->nread == p->nwrite && p->writeopen){  //DOC: pipe-empty
    if(myproc()->killed){
      release(&p->lock);
      return -1;
    }
    sleep(&p->nread, &p->lock); //DOC: piperead-sleep
  }
  nr = n;
  if (p->nwrite - p->nread < nr) {
    nr = p->nwrite - p->nread;
  }
  if (PIPESIZE - (p->nread % PIPESIZE) < nr) {
    nr = PIPESIZE - (p->nread % PIPESIZE);
  }
  mymemcpy(addr, p->data + (p->nread % PIPESIZE), nr);
  p->nread += nr;
  if (nr < n && p->nread < p->nwrite) {
    x = n - nr;
    if (p->nwrite - p->nread < x) {
      x = p->nwrite - p->nread;
    }
    mymemcpy(addr + nr, p->data + (p->nread % PIPESIZE), x);
    p->nread += x;
    nr += x;
  }
  wakeup(&p->nwrite);  //DOC: piperead-wakeup
  release(&p->lock);
  return nr;
}
