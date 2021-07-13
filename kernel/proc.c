#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "signals.h"
#include "threads.h"
#include "semaphore.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S



// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    for (int i = 0; i < NTHREADS; i++)
    {
      char *pa = kalloc();
      if (pa == 0)
        panic("kalloc");
      uint64 va = KSTACK((int)(((p - proc) * NTHREADS) + i));
      
      kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
  }
}

// initialize the proc table at boot time.
void procinit(void)
{
  struct proc *p;
  // printf("procinit: start\n");
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    for (int i = 0; i < NTHREADS; i++)
    {
      p->kstack[i] = KSTACK((int)(((p - proc) * NTHREADS) + i));
    }
  }
  // printf("procinit: end\n");
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{

  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  // printf("allocpid: start\n");
  int pid;
  
  acquire(&pid_lock);

  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);
  // printf("allocpid: end\n");
  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  // printf("allocproc: start\n");
  struct proc *p;
  struct thread *t;
  // struct semaphore *s;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  // printf("allocproc: found\n");
  
  p->pid = allocpid();
  p->state = USED;
  int thread_index = 0;
  for (int i = 0; i < NTHREADS; i++)
  {
    if ((p->threads[i] = (struct thread*) kalloc()) == 0)
    {
      freeproc(p);
      release(&p->lock);
      return 0;
    }
    t = p->threads[i];
    t->state = UNUSED;
    thread_index += 1;
  }
  
  // Allocate a trapframe page.
  if ((p->trapframe= (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  

  for(int i = 0; i < MAX_BSEM;i++){
    
    if ((p->sem[i] = (struct semaphore *)kalloc()) == 0)
    {
      freeproc(p);
      release(&p->lock);
      return 0;
    }
    initlock(&(p->sem[i]->lock),"s lock");
    p->sem[i]->state = SUNUSED;
    p->sem[i]->value = 1;
    p->sem[i]->chan = 0;
  }
  
  //change
  p->pending_signals = 0;
  p->signal_mask = 0;
  p->frozen = 0;
  p->handler_flag = -1;
  for (int i = 0; i <= MAX_SIG; i++)
  {
    p->signal_handler[i] = (void *)SIG_DFL;
    p->signals_mask[i] = 40;
  }

  if ((p->trapframe_backup = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
  }

  //An empty user page table.
  p->pagetable = proc_pagetable(p);
  // printf("allocproc: thread: %d\n",p); 
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  
  p->threads[0] = allocthread(p);
  // release(&p->lock);
  
  
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  
  if (p->trapframe)
      kfree((void *)p->trapframe);
  
  if (p->trapframe_backup)
    kfree((void *)p->trapframe_backup);
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;

  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->signal_mask = 0;
  p->pending_signals = 0;
  p->frozen = 0;
  p->handler_flag = -1;
  for (int i = 0; i <= MAX_SIG; i++)
  {
    p->signal_handler[i] = 0;
    p->signals_mask[i] = 40;
  }
  p->trapframe_backup = 0;
  for (int i = 0; i < NTHREADS; i++)
  {
   freethread(p->threads[i],p);
   kfree(p->threads[i]);
  }
  for(int i=0; i < MAX_BSEM; i++){
    if (p->sem[i])
      kfree((void *)p->sem[i]);
  }
  
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  // todo: maybe add for loop on
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
  struct proc *p;
  // printf("userinit: start\n");
  p = allocproc();
  // printf("userinit: allocproc\n");
  initproc = p;

  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;     // user program counter
  p->trapframe->sp = PGSIZE; // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  p->threads[0]->state = RUNNABLE;

  release(&p->lock);
  // printf("userinit: end\n");
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    printf("fork: p=%d\n",np);
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  // for (int i = 0; i < NTHREADS; i++)
  // {
  //   if((p->threads[i])){
      
  //   }
  // }
  safestrcpy(np->threads[0]->name, p->threads[0]->name, sizeof(p->threads[0]->name));
  *(np->trapframe) = *(p->trapframe);
  (np->trapframe)->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;
  //changed
  np->signal_mask = p->signal_mask;
  // for(int i=0; i < MAX_BSEM; i++){
  //   if (p->sem[i])
  //     kfree((void *)p->sem[i]);
  // }
  // for(int i =0; i < MAX_BSEM; i++){

  //   np->sem[i] = p->sem[i];
  //   np->sem[i]->chan = p->sem[i]->chan; 
  // }
  for (int i = 0; i <= MAX_SIG; i++)
  {
    np->signal_handler[i] = p->signal_handler[i];
    np->signals_mask[i] = p->signals_mask[i];
  }
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  np->threads[0]->state = RUNNABLE;
  release(&np->lock);
  // printf("fork\n");
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;
  
  acquire(&wait_lock);
  
  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  
  
  if(mythread()->state != UNUSED){
    release(&wait_lock);
    
    killthreads(p);
    p->xstate = status;
    p->state = ZOMBIE;
  }else{
    // printf("free pid:%d\n",p->pid);
    release(&wait_lock);
    freeproc(p);
  }
  
  acquire(&p->lock);
  
  // Jump into the scheduler, never to return.
  sched();
  // printf("tid:%d state: %d\n",mythread()->tid,mythread()->state);
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++)
    {
      if (np->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if (np->state == ZOMBIE)
        {
          // Found one.
          pid = np->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                   sizeof(np->xstate)) < 0)
          {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || p->killed)
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  // printf("scheduler: start\n");
  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for (p = proc; p < &proc[NPROC]; p++)
    {
      
      acquire(&p->lock);
      if (p->state != UNUSED)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        for (int i = 0; i < NTHREADS; i++)
        {
          if (p->threads[i]->state == RUNNABLE)
          {
            // if(p->pid == 3 )
              // printf("tid:%d running\n",i);
            //   if(i == 1)
            //   printf("tid: %d state:%d\n",i,p->threads[i]->state);
            // printf("pid: %d thread: %d running: %p\n",p->pid,p->threads[i]->tid,&(p->threads[i]->context));
            p->state = RUNNING;
            p->threads[i]->state = RUNNING;
            c->proc = p;
            c->thread = p->threads[i];
            // printf("scheduler: swtch to: %d\n",i);
            swtch(&c->context, &(p->threads[i]->context));

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
            c->thread = 0;
            
          }
        }
      }
      // if(p->pid == 3)
        // printf("tid: empty release lock scheduler\n");
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();
  struct thread *t = mythread();
  
  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  //changed p to t.
  if (t->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;

  //changed p to t.
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  struct thread *t = mythread();
  // printf("tid: %d acquire lock yield\n",t->tid);
  acquire(&p->lock);
  if(t->state != ZOMBIE)
    t->state = RUNNABLE;
  // check if proc have running thread on other cpu.
  // if (non_running_threads(p))
  //   p->state = RUNNABLE;
  sched();
  // printf("tid: %d release lock yield\n",t->tid);
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  static int first = 1;
  // printf("forkret: start\n");
  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first)
  {
    // printf("forkret: first: start\n");
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
    // printf("forkret: first: end\n");
  }
  // printf("forkret: usertrapret\n");
  usertrapret();
  // printf("forkret: end\n");
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct thread *t = mythread();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  // printf("tid: %d acquire lock sleep\n",t->tid);
  acquire(&p->lock); //DOC: sleeplock1
  
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = SLEEPING;
  // check if all proc threads are sleeping meaning the proc is sleeping
  if (check_threads_state(p, SLEEPING))
    p->state = SLEEPING;
  
  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  // printf("tid: %d release lock sleep\n",t->tid);
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
  struct proc *p;
  struct thread *t=0;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    // if ((p != myproc()) && t = mythread())
    acquire(&p->lock);
    for (int i = 0; i < NTHREADS; i++)
    {
      // if(t != 0)
        // printf("tid: %d acquire lock wakeup\n",t->tid);
      t = p->threads[i];
      if(t != 0 && t != mythread())
        if(t->state != UNUSED){
        if (t->state == SLEEPING && t->chan == chan)
        {
          // printf("pid:%d tid:%d\n",p->pid,t->tid);
          t->state = RUNNABLE;
        }
      }
    }
    // change the proc state according to his threads
    if ((p->state == SLEEPING) && !(check_threads_state(p, SLEEPING)))
      p->state = RUNNABLE;
    // if(t != 0)
      // printf("tid: %d release lock wakeup\n",t->tid);
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid, int signum)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      if ((signum < 0) | (signum > MAX_SIG) | ((p->pending_signals & (1 << signum)) == (1 << signum)))
        return -1;
      p->pending_signals = p->pending_signals | (1 << signum);

      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runable",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("proc:%d %s %s\n", p->pid, state, p->name);

    //print threads
    for (int i = 0; i < NTHREADS; i++)
    {
      struct thread *t = p->threads[i];

      if (t->state == UNUSED)
        continue;
      if (t->state >= 0 && t->state < NELEM(states) && states[t->state])
        state = states[t->state];
      else
        state = "???";
      printf("  thread:%d %s %s\n", t->tid, state, t->name);
    }
  }
}

void
copy_trapframe(struct trapframe *trapframe, struct trapframe *trapframe_backup)
{

  struct proc *p = myproc();
  struct thread *t = mythread();
  // printf("pid: %d, tid: %d\n",p->pid,t->tid);
  if(t == 0)
    panic("thread is not exist");
  int t_index = thread_index(p,t);
  if(t_index == -1)
    panic("thread is not exist");

  memmove(trapframe_backup+t_index, trapframe+t_index, sizeof(struct trapframe));
}

int
thread_index(struct proc *p,struct thread *t){
  for(int i=0;i < NTHREADS;i++){
    if(p->threads[i]==t)
      return i;
  }
  return -1;
}
void kthread_exit(int status){
  
  call_thread_exit(status,wait_lock);
}

int kthread_join(int thread_id,uint64 status){
  return call_thread_join(thread_id,status,wait_lock);
}

