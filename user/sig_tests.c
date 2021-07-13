#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

//
// Tests xv6 system calls.  usertests without arguments runs them all
// and usertests <name> runs <name> test. The test runner creates for
// each test a process and based on the exit status of the process,
// the test runner reports "OK" or "FAILED".  Some tests result in
// kernel printing usertrap messages, which can be ignored if test
// prints "OK".
//

#define BUFSZ  ((MAXOPBLOCKS+2)*BSIZE)

char buf[BUFSZ];

void
func(){
  printf("user_sig_test succsess\n");
}

void
func2(){
  for(;;){printf("");}
}
void
func3(){
  printf("intterupt\n");
}


void
user_sig_test_failsignum(char *s){
  struct sigaction new;
  struct sigaction old;
  new.sa_handler=(void*)&func;
  new.sigmask = 0;
  if(sigaction(50,&new,&old) >= 0){
    printf("sigaction faild\n");
    exit(1);
  }

}

void
user_sig_test_kill(char *s){
  struct sigaction new;
  struct sigaction old;
  new.sa_handler=(void*)&func;
  new.sigmask = 0;

  if (sigaction(2,&new,&old) < 0){
    printf("sigaction faild\n");
    exit(1);
  }
  if(kill(getpid(),2) < 0){
    printf("kill faild\n");
    exit(1);
  }
}

void
user_sig_test_multiprocsignals(char *s){
  int pid= fork();
  if(pid == 0){
    struct sigaction new;
    struct sigaction old;
    new.sa_handler=(void*)&func;
    new.sigmask = 0;

    if (sigaction(2,&new,&old) < 0){
      printf("sigaction faild\n");
      exit(1);
    }

  }else{
    if(kill(pid,2) < 0){
    printf("kill faild\n");
    exit(1);
    }
    wait(0);
  }
}

void
user_sig_test_restore(char *s){
  struct sigaction new;
  struct sigaction old;
  struct sigaction old2;
  new.sa_handler=(void*)&func;
  new.sigmask = 0;

  if (sigaction(2,&new,&old) < 0){
    printf("sigaction faild\n");
    exit(1);
  }
  if(sigaction(2,&old,&old2) < 0){
    printf("kill faild\n");
    exit(1);
  }
  if(old2.sa_handler != new.sa_handler){
    printf("restore faild\n");
    exit(1);
  }
}

// if stuck here the test fails.
void
kernel_sig_test_ignore(char *s){
  sigprocmask((1<<SIGSTOP));
  if(kill(getpid(),SIGSTOP) < 0){
    printf("kill faild\n");
    exit(1);
  }
}
void
kernel_sig_test_stop_cont(char *s){
  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
        kill(getpid(),SIGSTOP);
        sleep(10);
        exit(1);
    }else{
        int status;
        sleep(50);
        kill(pid,SIGCONT);
        wait(&status);
    }
}

void
kernel_sig_test_failignorekill(char *s){
  struct sigaction new;
  struct sigaction old;
  new.sa_handler=(void*)&func;
  new.sigmask=0;
  if(sigaction(SIGKILL,&new,&old) >= 0){
      printf("sigaction failed\n");
      exit(1);
  }
  if(sigaction(SIGKILL,&new,0) >= 0){
      printf("sigaction failed\n");
      exit(1);
  }
  if(sigaction(SIGKILL,0,&old) >= 0){
      printf("sigaction failed\n");
      exit(1);
  }
}
void
final_sig_test(char *s){
  struct sigaction new;
  struct sigaction old;
  new.sa_handler=(void*)&func2;
  new.sigmask=0;
  struct sigaction new2;
  struct sigaction old2;
  new2.sa_handler=(void*)&func3;
  new2.sigmask=0;

  int pid = fork();
  if (pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
    if(sigaction(2,&new,&old) < 0)
    {
      printf("sigaction failed\n");
      exit(1);
    }
    if(sigaction(7,&new2,&old2) < 0)
    {
      printf("sigaction failed\n");
      exit(1);
    }
    if(kill(getpid(),2) < 0){
      printf("kill failed\n");
      exit(1);
    }
  }else{
    sleep(10);
    if(kill(pid,7) < 0){
      printf("kill failed\n");
      exit(1);
    }
    sleep(10);
    kill(pid,SIGKILL);

  }
}

//
// use sbrk() to count how many free physical memory pages there are.
// touches the pages to force allocation.
// because out of memory with lazy allocation results in the process
// taking a fault and being killed, fork and report back.
//
int
countfree()
{
  int fds[2];

  if(pipe(fds) < 0){
    printf("pipe() failed in countfree()\n");
    exit(1);
  }
  
  int pid = fork();

  if(pid < 0){
    printf("fork failed in countfree()\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    
    while(1){
      uint64 a = (uint64) sbrk(4096);
      if(a == 0xffffffffffffffff){
        break;
      }

      // modify the memory to make sure it's really allocated.
      *(char *)(a + 4096 - 1) = 1;

      // report back one more page.
      if(write(fds[1], "x", 1) != 1){
        printf("write() failed in countfree()\n");
        exit(1);
      }
    }

    exit(0);
  }

  close(fds[1]);

  int n = 0;
  while(1){
    char c;
    int cc = read(fds[0], &c, 1);
    if(cc < 0){
      printf("read() failed in countfree()\n");
      exit(1);
    }
    if(cc == 0)
      break;
    n += 1;
  }

  close(fds[0]);
  wait((int*)0);
  
  return n;
}

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s) {
  int pid;
  int xstatus;

  printf("%s: \n", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0) 
      printf("FAILED\n");
    else
      printf("OK\n");
    return xstatus == 0;
  }
}

int
main(int argc, char *argv[])
{
  int continuous = 0;
  char *justone = 0;

  if(argc == 2 && strcmp(argv[1], "-c") == 0){
    continuous = 1;
  } else if(argc == 2 && strcmp(argv[1], "-C") == 0){
    continuous = 2;
  } else if(argc == 2 && argv[1][0] != '-'){
    justone = argv[1];
  } else if(argc > 1){
    printf("Usage: usertests [-c] [testname]\n");
    exit(1);
  }
  
  struct test {
    void (*f)(char *);
    char *s;
  } tests[] = {
    {user_sig_test_failsignum, "user_sig_test_failsignum"},
    {user_sig_test_kill, "user_sig_test_kill"},
    {user_sig_test_multiprocsignals, "user_sig_test_multiprocsignals"},
    {user_sig_test_restore, "user_sig_test_restore"},
    {kernel_sig_test_ignore, "kernel_sig_test_ignore"},
    {kernel_sig_test_stop_cont, "kernel_sig_test_stop_cont"},
    {kernel_sig_test_failignorekill, "kernel_sig_test_failignorekill"},
    {final_sig_test, "final_sig_test"},
    { 0, 0},
  };

  if(continuous){
    printf("continuous usertests starting\n");
    while(1){
      int fail = 0;
      int free0 = countfree();
      for (struct test *t = tests; t->s != 0; t++) {
        if(!run(t->f, t->s)){
          fail = 1;
          break;
        }
      }
      if(fail){
        printf("SOME TESTS FAILED\n");
        if(continuous != 2)
          exit(1);
      }
      int free1 = countfree();
      if(free1 < free0){
        printf("FAILED -- lost %d free pages\n", free0 - free1);
        if(continuous != 2)
          exit(1);
      }
    }
  }

  printf("sig_usertests starting\n");
  int free0 = countfree();
  int free1 = 0;
  int fail = 0;
  for (struct test *t = tests; t->s != 0; t++) {
    if((justone == 0) || strcmp(t->s, justone) == 0) {
      if(!run(t->f, t->s))
        fail = 1;
    }
  }

  if(fail){
    printf("SOME TESTS FAILED\n");
    exit(1);
  } else if((free1 = countfree()) < free0){
    printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0);
    exit(1);
  } else {
    printf("ALL TESTS PASSED\n");
    exit(0);
  }
}
