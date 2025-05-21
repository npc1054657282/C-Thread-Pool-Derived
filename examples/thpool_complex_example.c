#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "threadpool.h"
#include "utils/log.h"

typedef struct task_thread_ctx {
    time_t start;
    pthread_mutex_t *log_mutex_ref;
} task_thread_ctx;

void start_cb(void *arg, threadpool_thread current_thrd)
{

    pthread_mutex_t *log_mutex = arg;
    task_thread_ctx *tctx = thpool_thread_get_context(current_thrd);
    if (!tctx) {
        tctx = malloc(sizeof(task_thread_ctx));
        if (tctx == NULL) {
            // 分配失败处理，例如打印错误日志
            log_error("start_cb: Failed to allocate memory for task_thread_ctx");
            // *ctx_out 将保持为 NULL，后续任务和 end_cb 需要能处理 NULL 上下文
            return;
        }
        memset(tctx, 0, sizeof(task_thread_ctx));
        thpool_thread_set_context(current_thrd, tctx);
    }
    time(&tctx->start);
    tctx->log_mutex_ref = log_mutex;
}

void end_cb(threadpool_thread current_thrd)
{
    task_thread_ctx *tctx = (task_thread_ctx *)thpool_thread_get_context(current_thrd);
    if (!tctx) {
        return;
    }
    free(tctx);//上下文对互斥锁只是引用，并非持有，无需在此销毁。
    thpool_thread_unset_context(current_thrd);
    thpool_thread_unref_callback_arg(current_thrd);
}

void mutex_destructor_wrap(void *arg)
{
    pthread_mutex_t * mutex = arg;
    pthread_mutex_destroy(mutex);
    free(mutex);
}

typedef struct {
    int job_id;
    time_t add_work_time;
} task_args;

void task(void *arg, threadpool_thread current_thrd)
{
    task_args *args = arg;
    task_thread_ctx *tctx = thpool_thread_get_context(current_thrd);
    if (!tctx) {
        return;
    }
    time_t add_work_time = (time_t)args->add_work_time;
    const char *name = thpool_thread_get_name(current_thrd);
    int id = thpool_thread_get_id(current_thrd);
    time_t now;
    time(&now);
    double timepassthread = difftime(now, tctx->start);
    double timepassjob = difftime(now, add_work_time);
    pthread_mutex_lock(tctx->log_mutex_ref);
    printf("#####Job %d:Thread %d #%u %s\n", args->job_id, id, (unsigned)pthread_self(), name);
    printf("thread pass %f second after thread created\n", timepassthread);
    printf("thread pass %f second after work starting to be added\n", timepassjob);
    pthread_mutex_unlock(tctx->log_mutex_ref);
    sleep(5);
    free(args);
}

int main()
{
    /* 为所有printf创建互斥锁，提供给线程开始的回调，以保存到各线程的上下文。 */
    pthread_mutex_t *log_mutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(log_mutex, NULL);
    /* 未被指定初始化的参数会自动初始化为0。    */
    threadpool_config conf = {
        .num_threads = 4,
        .work_num_max = 8,
        .thread_name_prefix = "cplx",
        .thread_start_cb = start_cb,
        .callback_arg = log_mutex,
        .callback_arg_destructor = mutex_destructor_wrap,
        .thread_end_cb = end_cb,
    };
    printf("Making threadpool with 4 threads\n");
    threadpool thpool = thpool_init(&conf);

    printf("Adding 40 tasks to threadpool\n");
    int i;
    for (i=0; i<40; i++) {
        time_t now;
        time(&now);
        pthread_mutex_lock(log_mutex);
        printf("start to add job %d at %s\n", i, ctime(&now));
        pthread_mutex_unlock(log_mutex);
        void *arg = (task_args *)malloc(sizeof(task_args));
        ((task_args*)arg)->add_work_time = now;
        ((task_args*)arg)->job_id = i;
        thpool_add_work(thpool, task, arg);
        time(&now);
        pthread_mutex_lock(log_mutex);
        printf("end to add job %d at %s\n", i, ctime(&now));
        pthread_mutex_unlock(log_mutex);
    }

    thpool_wait(thpool);
    thpool_reactivate(thpool);
    for (i=0; i<40; i++) {
        time_t now;
        time(&now);
        pthread_mutex_lock(log_mutex);
        printf("start to add job %d at %s\n", i, ctime(&now));
        pthread_mutex_unlock(log_mutex);
        void *arg = (task_args *)malloc(sizeof(task_args));
        ((task_args*)arg)->add_work_time = now;
        ((task_args*)arg)->job_id = i;
        thpool_add_work(thpool, task, arg);
        time(&now);
        pthread_mutex_lock(log_mutex);
        printf("end to add job %d at %s\n", i, ctime(&now));
        pthread_mutex_unlock(log_mutex);
    }
    thpool_wait(thpool);
    printf("Killing threadpool\n");
    thpool_shutdown(thpool);
    printf("Destroy threadpool\n");
    thpool_destroy(thpool);

    return 0;
}