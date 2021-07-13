

struct thread {

  enum state state;          // thread state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int tid;		                 // thread ID
  int xstatus;                  // thread exit status

  struct proc *tproc;	         // connected proc
//   struct trapframe *trapframe;        // Trap frame for current syscall
  struct context context;     // swtch() here to run thread
  char name[16];               // thread name (debugging)

  
};

struct thread* allocthread(struct proc*);
void freethread(struct thread *t,struct proc *p);
int killthreads(struct proc *p);
struct thread* mythread(void);
int non_running_threads(struct proc *p);
int check_threads_state(struct proc *p,int state);
