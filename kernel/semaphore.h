

enum semstate {SUNUSED, SUSED};


struct semaphore{
    int sid;    // semaphore id
    int value;   // 1 free 0 locked
    struct thread *curr;    // current locked thread
    enum semstate state;    //sem state
    struct spinlock lock;   // spinlock
    void* chan;
};
