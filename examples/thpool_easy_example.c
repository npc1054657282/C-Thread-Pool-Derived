/* 
 * WHAT THIS EXAMPLE DOES
 * 
 * We create a pool of 4 threads and then add 40 tasks to the pool
 * Tasks simply print which thread is running them.
 * 
 * As soon as we add the tasks to the pool, the threads will run them. It can happen that 
 * you see a single thread running all the tasks (highly unlikely). It is up the OS to
 * decide which thread will run what. So it is not an error of the thread pool but rather
 * a decision of the OS.
 * 
 * */

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "threadpool.h"

void task(threadpool_arg arg, threadpool_thread _){
    printf("Thread #%u working on %d\n", (unsigned)pthread_self(), (int) arg.val);
}

int main(){
    /* 未被指定初始化的参数会自动初始化为0。    */
    threadpool_config conf = {
        .num_threads = 4,
        .thread_name_prefix = "easy",
    };
    printf("Making threadpool with 4 threads\n");
    threadpool thpool = thpool_init(&conf);

    printf("Adding 40 tasks to threadpool\n");
    int i;
    for (i=0; i<40; i++){
        threadpool_arg arg = {.val = i};
        thpool_add_work(thpool, task, arg);
    };

    thpool_wait(thpool);
    printf("Killing threadpool\n");
    thpool_shutdown(thpool);
    thpool_destroy(thpool);

    return 0;
}