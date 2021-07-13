#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"


void stam(){
    printf("STAM\n");
}
void func(int signum)
{
    printf("func\n");
}
void func2(){
    for(int i=0;i< 5;i++){
        printf("thread %d\n",kthread_id());
        sleep(5);
    }
    kthread_exit(1);
    while(1){
        printf("thread %d*\n",kthread_id());
        sleep(5);
    }
}
void func3(){
    for(int i=0;i< 5;i++){
        printf("thread %d^\n",kthread_id());
        sleep(5);
    }
    kthread_exit(1);
    while(1){
        printf("thread %d*^\n",kthread_id());
        sleep(5);
    }
}

void
func4(){
    
    bsem_down(0);
    printf("thread: %d is printing\n",kthread_id());
    bsem_up(0);
    kthread_exit(1);
}

void
user_signal_test(){
    printf("func: %x\n",&func);
    printf("stam: %x\n",&stam);
    struct sigaction *test = malloc(sizeof(struct sigaction));
    struct sigaction *old = malloc(sizeof(struct sigaction));
    test->sa_handler=(void*)&func;
    test->sigmask = 0;
    sigaction(2, test, old);
    kill(3,2);
    printf("done test\n");
    
}

void
kernel_stop_test(){
     if(fork() == 0){
        printf("child getting stop\n");
        kill(getpid(),SIGSTOP);
        sleep(10);
        printf("child getting sig\n");
        exit(1);
    }else{
        int status;
        sleep(1);
        printf("father is going to sleep\n");
        sleep(50);
        kill(4,SIGCONT);
        printf("father sent sig to child\n");
        wait(&status);
        
    }
}
void
threads(){
    void *stack1 = (void*)malloc(4000);
    void *stack2 = (void*)malloc(4000);
    void *stack3 = (void*)malloc(4000);
    void *stack4 = (void*)malloc(4000);
    void *stack5 = (void*)malloc(4000);
    void *stack6 = (void*)malloc(4000);

    // printf("user stack add is: %p\n",stack);
    int status;
    kthread_create((void*)&func2,stack1);
    kthread_create((void*)&func2,stack2);
    kthread_create((void*)&func2,stack3);
    kthread_create((void*)&func2,stack4);
    kthread_create((void*)&func2,stack5);
    kthread_create((void*)&func2,stack6);
    
    for(int i=0;i< 1;i++){
        printf("thread 0\n");
        sleep(5);
    }
    // exit(0);
    // printf("thread 0 exit\n");
    // kthread_exit(0);
    printf("join: %d\n",kthread_join(6,&status));
    kthread_exit(0);
}

void double_fork(){
    if(fork() == 0){
        if(fork() == 0){
            int status;
            void *stack1 = (void*)malloc(4000);
            printf("join thread:%d\n",kthread_join(kthread_create((void*)&func2,stack1),&status));
            printf("im the son of the son\n");
            
        }
        else{
            int status;
            void *stack1 = (void*)malloc(4000);
            printf("join thread:%d\n",kthread_join(kthread_create((void*)&func3,stack1),&status));
            printf("im the son \n");
        }
    }else{
        if(fork() == 0)
            printf("second son\n");
        else{
        wait(0);
        wait(0);
        printf("im the father\n");
        }
    }
}

void
twochildren(char *s)
{
  for(int i = 0; i < 1000; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid1 == 0){
      exit(0);
    } else {
      int pid2 = fork();
      if(pid2 < 0){
        printf("%s: fork failed\n", s);
        exit(1);
      }
      if(pid2 == 0){
        exit(0);
      } else {
        wait(0);
        wait(0);
      }
    }
  }
}

void
sem_test(){
    void *stack1 = (void*)malloc(MAX_STACK_SIZE);
    void *stack2 = (void*)malloc(MAX_STACK_SIZE);
    bsem_alloc();

    int t1 = kthread_create((void*)&func4,stack1);
    int t2 = kthread_create((void*)&func4,stack2);
    kthread_join(t1,0);
    kthread_join(t2,0);
    
}

int main(int argc, char *argv[])
{
    // kernel_stop_test();
    // threads();
    // double_fork();
    // twochildren("");
    sem_test();
    // printf("Ok");
    exit(0);
}