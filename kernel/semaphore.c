#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "threads.h"
#include "semaphore.h"

int nextid = 1;

int
allocsid(){
    int i = nextid;
    nextid++;
    return i;
}

int bsem_alloc(){
    struct proc *p = myproc();
    struct thread *t = mythread();
    struct semaphore *s;
    int desc = -1;

    acquire(&p->lock);
    for(int i = 0; i< MAX_BSEM;i++){
        s = p->sem[i];
        desc = i;
        
        if(s->state == SUNUSED)
            goto found;
    }
    release(&p->lock);
    return -1;
found:
    s->state = SUSED;
    s->sid = allocsid();
    s->value = 1;
    s->curr = t;
    s->chan = (void*)&s->sid;
    release(&p->lock);
    return desc;
}

void
bsem_free(int i){
    if(i < 0 || i > MAX_BSEM)
        return;

    struct proc *p = myproc();
    struct semaphore *s = p->sem[i];
    
    
    s->state = SUNUSED;
    s->sid = 0;
    s->value = 0;
    s->curr = 0;
}

void
bsem_down(int i){
    if(i < 0 || i > MAX_BSEM)
        return;
    // printf("i: %d , tid: %d",i,kthread_id());
    struct proc *p = myproc();
    struct semaphore *s = p->sem[i];
    
    // printf("s:%d curr:%d val:%d \n",s->sid,s->curr->tproc->pid,s->value);
        while(1){
            
            acquire(&s->lock);
            // printf("s:%d\n",s->sid);
            if(s->value == 1){
                break;
            }
            // printf("pid: %d\n",p->pid);
            sleep(s->chan,&s->lock);
            release(&s->lock);
        }
        release(&s->lock);
        // printf("pid: %d down %d\n",p->pid,s->sid);
        s->value = 0;
        s->curr = mythread();
}

void
bsem_up(int i){
    if(i < 0 || i > MAX_BSEM)
        return;
    struct proc *p = myproc();
    struct semaphore *s = p->sem[i];

    if(s->value == 0){
        // printf("pid: %d up %d\n",p->pid,s->sid);
        s->value = 1;
        wakeup(s->chan);
    }
}
