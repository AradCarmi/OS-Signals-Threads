#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "threads.h"

extern void start_sigret(void);
extern void end_sigret(void);

int
checksigmask(uint mask,int signum){
  if(((mask) & (1<<signum)) == (1<<signum)){
    return 1;
  }
  return 0;
}

uint
sigprocmask(uint mask)
{
  struct proc *p;
  p = myproc();
  uint oldmask;
  if (p == 0)
    return -1;
  acquire(&p->lock);
  oldmask = p->signal_mask;
  p->signal_mask = mask;
  release(&p->lock);
  return oldmask;
}

// return 1 on fail.
int
check_arguments(int signum, uint64 act, uint64 oldact){
  if ((signum) < 0 || (signum) > MAX_SIG || act == 0 || oldact == 0)
    return 1;
  if (signum == SIGKILL || signum == SIGSTOP)
    return 1;
  return 0;
}

// return 1 on fail.
int
copy_to_kernel(struct proc *p,uint64 act,int signum){
  if ((copyin(p->pagetable, (char *)(&p->signal_handler[signum]),act, sizeof(void*)) < 0) ||
        (copyin(p->pagetable, (char *)(&p->signals_mask[signum]),(act+sizeof(void*)), sizeof(uint)) < 0))
        return 1;
  return 0;
}

int 
sigaction(int signum, uint64 act, uint64 oldact)
{
  if (check_arguments(signum,act,oldact))
    return -1;
  struct proc *p;
  p = myproc();
  if (p == 0)
    return -1;

  acquire(&p->lock);
    if(p->signals_mask[signum] != 40){
      if ((copyout(p->pagetable, oldact, (char *)&p->signal_handler[signum], sizeof(void*)) < 0) ||
        (copyout(p->pagetable, oldact+sizeof(void*), (char *)&p->signals_mask[signum], sizeof(uint64)) < 0))
          return -1;
    }
    else{
          if (copyout(p->pagetable, oldact, (char *)&p->signal_handler[signum], sizeof(void*)) < 0)
            return -1;
    }
    if (copy_to_kernel(p,act,signum))
    return -1;
    
    // printf("sigaction: signum: %d defined as: %x\n",signum,p->signal_handler[signum]);
  release(&p->lock);

  return 0;
}

void 
sigkill(){
  struct proc *p = myproc();
  acquire(&p->lock);
    p->killed = 1;
  release(&p->lock);
}

void
turnoff_bit(struct proc *p,int signum){
  p->pending_signals = p->pending_signals ^ (1<<signum);
}
void 
sigcont(){
  struct proc *p = myproc();
  acquire(&p->lock);
  if(checksigmask(p->pending_signals,SIGCONT)){
    turnoff_bit(p,SIGCONT);
    p->frozen=0;
  }
  release(&p->lock);
}

void
sigstop(){
  struct proc *p = myproc();
  
  acquire(&p->lock);
    p->frozen=1;
    turnoff_bit(p,SIGSTOP);
  release(&p->lock);
}

void
callsigret(){
  sigret();
}

int
sigret(){
  // printf("start sigret syscall\n");
  struct proc *p = myproc();
  struct thread *t = mythread();
  if(t == 0)
    panic("thread is not exist");
  int t_index = thread_index(p,t);
  if(t_index == -1)
    panic("thread is not exist");

  acquire(&p->lock);    
    p->handler_flag = -1;
    copy_trapframe(p->trapframe_backup,p->trapframe);
  release(&p->lock);
  // printf("finish sigret syscall\n");
  
  return 0;
}
void
call_kernel_signal_handler(struct proc *p,int signum){
  switch (signum)
  {
  case SIGSTOP:
    sigstop();
    break;
  case SIGCONT:
    sigcont();
    break;
  default:
    sigkill();
    break;
  }
}

void
call_handle_func(struct proc *p,int signum){
  struct thread *t = mythread();
  if(t == 0)
    panic("thread is not exist");
  int t_index = thread_index(p,t);
  if(t_index == -1)
    panic("thread is not exist");
    
  //set handler to epc
  uint64 func = (uint64)(p->signal_handler[signum]);

  
  //backup mask
  // uint64 backup_signal_mask = p->signal_mask;
  //change to handler mask
  p->signal_mask = (p->signals_mask[signum]);

  p->handler_flag = signum;
  turnoff_bit(p,signum);

  copy_trapframe(p->trapframe,p->trapframe_backup);
    // printf("copied trapframe\n");

  (p->trapframe+t_index)->epc = func;
    
  // inject sigret system call
  uint sigsize = end_sigret-start_sigret;
  (p->trapframe+t_index)->sp = (p->trapframe+t_index)->sp - sigsize;
  copyout(p->pagetable,(p->trapframe+t_index)->sp,(char*)&start_sigret,sigsize);
  
  // signum which been called
  (p->trapframe+t_index)->a0 = signum;
  (p->trapframe+t_index)->ra = (p->trapframe+t_index)->sp;
  // printf("jumping to user\n");
  return;
  
}

char
is_kernel_signal(int signum){
  if( signum == SIGKILL || signum ==  SIGSTOP || signum == SIGCONT || signum == SIG_DFL)
    return 1;
  return 0;
}

void
call_user_signal_handler(struct proc *p,int signum){
    // printf("start call_user_signal\n");
    call_handle_func(p,signum);
    // printf("end of call_user_signal\n");
    return;
}

void
check_pending_signals(struct proc *p){
  for (int signum = 0; signum <= MAX_SIG; signum++)
  {
    if((checksigmask(p->pending_signals,signum)) && !(checksigmask(p->signal_mask,signum))){
      acquire(&p->lock);
      if(p->handler_flag == -1){
        release(&p->lock);
        if(is_kernel_signal(signum))
          call_kernel_signal_handler(p,signum);
        else{
          call_user_signal_handler(p,signum);
          return;
          }
      }
      else{
        if(!checksigmask(p->signals_mask[p->handler_flag],signum)){
          release(&p->lock);
        if(is_kernel_signal(signum))
          call_kernel_signal_handler(p,signum);
        else{
          call_user_signal_handler(p,signum);
          return;
          }      
        }
      }
    }
  }
}

void
handle_signals(struct proc *p){
  check_pending_signals(p);
  while(p->frozen && !p->killed){
    check_pending_signals(p);
    yield();
  }
}