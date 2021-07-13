#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "threads.h"


extern void forkret(void);

int nexttid = 1;

struct spinlock tid_lock;

// return the current thread
struct thread *
mythread(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

// free thread
void freethread(struct thread *t, struct proc *p)
{
  // t->tid = 0;
  t->tproc = 0;
  t->name[0] = 0;
  t->chan = 0;
  t->killed = 0;
  t->state = UNUSED;
  t->xstatus = 0;
}
// alloct new thread in the thread table
struct thread *
allocthread(struct proc *p)
{
  struct thread *t;
  int i;
  for (i = 0; i < NTHREADS; i++)
  {
    if (p->threads[i]->state == UNUSED)
    {
      goto found;
    }
  }
  return 0;

found:
  t = p->threads[i];
  t->tid = nexttid;
  nexttid++;
  t->state = USED;
  t->tproc = p;
  t->killed = 0;
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = p->kstack[i] + PGSIZE;
  
  return t;
}
// kill all threads
int killthreads(struct proc *p)
{
  struct thread *t;
  
  for (int i = 0; i < NTHREADS; i++)
  {
    
    t = p->threads[i];
    if(t != 0 && t->tid != kthread_id() &&  t->state != UNUSED){
      t->killed = 1;
      if(t->state == SLEEPING)
        t->state = RUNNABLE;
      
      kthread_join(t->tid,0);
    }
  }
  
  mythread()->state = ZOMBIE;
  // printf("tid:%d state: %d\n",mythread()->tid,mythread()->state);
  return 1;
}
//check if there is running threads.
int non_running_threads(struct proc *p)
{
  for (int i = 0; i < NTHREADS; i++)
  {
    if (p->threads[i]->state == RUNNING)
      return 0;
  }
  return 1;
}
// check if the porc threads have same state
int check_threads_state(struct proc *p,int state)
{
  for (int i = 0; i < NTHREADS; i++)
  {
    if ((p->threads[i]->state != UNUSED) && (p->threads[i]->state != state))
      return 0;
  }
  return 1;
}

int kthread_create(uint64 start_func,uint64 stack){
  struct proc *p = myproc();
  struct thread *t = allocthread(p);
  
  int i = thread_index(p,t);
  // copyin(p->pagetable,(char*)(&((p->trapframe+i)->epc)),start_func,sizeof((p->trapframe+i)->epc));
  (p->trapframe+i)->epc = start_func;
  (p->trapframe+i)->sp = stack;
  // printf("!!!!!!! tid: %d !!!!!!\n",t->tid);
  t->state = RUNNABLE;
  return t->tid;
}

int last_runable_thread(struct proc *p){
  int counter = 0;
  for(int i=0;i < NTHREADS;i++){
    if(p->threads[i] != 0)
      if(p->threads[i]->state != UNUSED && p->threads[i]->state != ZOMBIE)
        counter++;
  }
  if(counter == 1)
    return 1;
  return 0;
}

void call_thread_exit(int status,struct spinlock wait_lock){
  struct proc *p = myproc();
  struct thread *t = mythread();

  acquire(&wait_lock);
  acquire(&p->lock);
  
  if(last_runable_thread(p)){
    printf("Last thread\n");
    // freethread(t,p);
    release(&p->lock);
    release(&wait_lock);
    exit(status);
  }
  else{
  t->xstatus = status;
  t->state = ZOMBIE;
  p->state = RUNNABLE;
  release(&p->lock);
  release(&wait_lock);
  wakeup(t);
  // printf("wakeup %p\n",t);
  }
  // Jump into the scheduler, never to return.
  acquire(&p->lock);
  sched();
  panic("zombie exit");
}

int call_thread_join(int thread_id,uint64 status,struct spinlock wait_lock)
{
  struct proc *p = myproc();
  int t_index = -1;
  if(thread_id == kthread_id())
    return -1;
  acquire(&wait_lock);
  
  for(;;){
    // printf("thread: %d joining on:%d\n",kthread_id(),thread_id);
    for(int i=0; i < NTHREADS;i++){
      if(p->threads[i])
        if(thread_id == p->threads[i]->tid){
          // acquire(&p->lock);
          t_index = i;
          if(p->threads[i]->state == ZOMBIE){
            if (status != 0 && copyout(p->pagetable, status, (char *)&p->threads[i]->xstatus,
                                          sizeof(p->threads[i]->xstatus)) < 0){
                  release(&p->lock);
                  release(&wait_lock);
                  return -1;
              }
          freethread(p->threads[i],p);
          // release(&p->lock);
          release(&wait_lock);
          return p->threads[i]->tid;
          }
        }
    }
    if(t_index != -1){
    // printf("thread %d sleeps on: %p\n",kthread_id(),p->threads[t_index]);
    
    sleep(p->threads[t_index], &wait_lock); //DOC: wait-sleep
    
    }
  }
  release(&wait_lock);
  return -1;
}
int kthread_id(){
  return mythread()->tid;
}