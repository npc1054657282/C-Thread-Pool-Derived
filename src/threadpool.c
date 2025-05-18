/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2025 npc1054657282 <ly1054657282 at gmail.com> */
/* SPDX-FileCopyrightText: 2016 Johan Hanssen Seferidis */

/**
 * threadpool.c
 * 
 * This file is part of C-Thread-Pool-Derived, distributed under the MIT License.
 * For full terms see the included LICENSE file.
 * 
 * Referencing <https://github.com/Pithikos/C-Thread-Pool> for thread pool implementation.
 * Extended and reduced features based on personal project requirements.
 * Added features:
 * 1. Global variables are encapsulated within a struct, allowing multiple thread pools to exist simultaneously.
 * 2. Added callbacks for thread start/end, and pre-task/post-task routines (though not fully implemented in this version's job execution loop).
 * Allows tasks and threads to share metadata via a thread context slot, useful for scenarios like database connection reuse.
 * 3. Enhanced configuration, including a maximum job queue size and waiting/notification mechanisms when the queue is full to prevent excessive memory usage.
 * 4. Integrated with `rxi/log.c` library for logging.
 * 5. Introduced a debug concurrency passport and associated API variants to help diagnose thread pool lifecycle-related concurrency issues (like potential Use-After-Free).
 * Removed features:
 * Removed semaphore features like pause/resume.
 * 
 * 参考<https://github.com/Pithikos/C-Thread-Pool>实现的线程池。
 * 针对个人项目需求，补充了一些功能，并削减了一些功能。
 * 补充功能：
 * 1.将全局变量收归结构体，允许同时创建多个线程池。
 * 2.增加了新功能，允许线程池的一个线程在线程启动时、线程结束时、任务启动前、任务结束后插入例程，
 * 并允许任务与线程共享元数据。这一方案可以将线程池改造为特殊用途，如数据库复用连接。
 * 与连接池相比，线程池的同线程复用连接的方案在本项目实现更简单，足够满足需求。
 * 3.更丰富的配置，如可设置任务队列上限，任务队列达到上限时，添加任务的等待与通知机制，以避免内存爆炸。
 * 4.接入`rxi/log.c`库进行日志。日志可配置。
 * 5.引入并发调试通行证相关API，帮助诊断复杂并发情景下由线程池生存期引发的uaf问题。
 * 删除功能：
 * 削除了线程池的暂停继续等个人项目用不上的信号量功能，且它们的实现方式我不满意。
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <errno.h>

/**
 * The header files below are only used for naming threads, for easier debugging.
 * Since this is not a POSIX standard, definitions vary across operating systems.
 */
/* 下面的头文件仅用于给线程命名，为了调试方便。因为不是POSIX标准，不同操作系统的定义有所不同。 */
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>
#endif

#include "threadpool_log_config.h"
#include "threadpool.h"

#if defined(__GNUC__) || defined(__clang__)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)
#else
#define unlikely(x) (x)
#define likely(x) (x)
#endif

/* ========================== STRUCTURES ============================ */

/**
 * @brief States of the thread pool lifecycle.
 * Used to track the operational status of the thread pool and its associated passport.
 */
#define THPOOL_STATE_LIST(X) \
    X(THPOOL_UNBIND,        = 0)    /* Passport is created but not bound to a pool          */ \
    X(THPOOL_ALIVE,         )       /* Thread pool is operational and accepting normal apis */ \
    X(THPOOL_SHUTTING_DOWN, )       /* Shutdown initiated, threads finishing current jobs, no new jobs accepted     */ \
    X(THPOOL_SHUTDOWN,      )       /* All jobs finished, threads have exited their loop, resources *not* yet freed */ \
    X(THPOOL_DESTROYING,    )       /* Destroy initiated, resources are being deallocated   */ \
    X(THPOOL_DESTROYED,     )       /* Thread pool resources are freed, passport is unbound */ \
    /* Counter for the number of states, useful for array sizing and boundary checks    */ \
    /* 添加一个计数器，方便定义数组大小和边界检查。 */ \
    X(THPOOL_STATE_COUNT,   )

/* Generate enum definition using the state list.   */
/* 使用主列表宏来生成 enum 定义。   */
#define X(name, value) name value,
enum thpool_state {
    THPOOL_STATE_LIST(X)
};
#undef X

/* Generate string array for state names using the state list.  */
/* 使用主列表宏来生成字符串数组。   */
#define X(name, value) #name,
static const char *thpool_state_strings[] = {
    THPOOL_STATE_LIST(X)
};
#undef X

/**
 * @brief Converts a thread pool state enum value to its string representation.
 * Provides bounds checking for safety.
 *
 * @param state The thread pool state enum value.
 * @return const char* The string representation of the state, or "UNKNOWN_THPOOL_STATE" for invalid values.
 */
static inline const char *thpool_state_to_string(enum thpool_state state) {
    // Perform boundary checking to prevent enum values from exceeding array bounds
    // 进行边界检查，防止枚举值超出数组范围
    // Assuming THPOOL_UNBIND is 0 and state values are contiguous
    // 假设 THPOOL_UNBIND 是 0，且状态值是连续递增的
    // Use explicit cast to int for comparison to avoid potential warnings
    if ((int)state >= (int)THPOOL_UNBIND && (int)state < (int)THPOOL_STATE_COUNT) {
        return thpool_state_strings[state];
    }
    // For invalid enum values, return an error string to avoid out-of-bounds access
    // 对于无效的枚举值，返回一个错误指示字符串，避免越界访问
    return "UNKNOWN_THPOOL_STATE";
}
/**
 * @brief Concurrency state block (Passport) for managing thread pool API concurrent access and lifecycle state.
 * 
 * This block tracks the operational state of the thread pool and counts API calls.
 * In debug concurrency API mode, this block can be managed by the user to enable
 * no-uaf API usage and state checks.
 * 
 * 并发状态块，管理api调用的并发状态。
 * 为了在复杂并发状态下便于用户调试，也可以由用户管理。
 *
 * @note When managed by the user (via thpool_init config), the user is
 * responsible for its allocation and deallocation using
 * thpool_debug_conc_passport_init/destroy.
 * The user MUST ensure the passport's lifetime exceeds all API calls
 * that use it, including calls potentially made after the pool is destroyed.
 * 
 * 用户管理时叫做debug concurrency passport，并发通行证，用户并发使用debug conc API时出示。
 * 如果thpool已过生存期，这些API将执行失败，但只要用户管理的passport未过生存期，不会发生uaf。
 */
typedef struct conc_state_block {
#ifdef THPOOL_ENABLE_DEBUG_CONC_API // Members included only if debug API is enabled
    struct thpool *bind_pool;  /* Pointer to the thread pool this passport is bound to, used for validation. 用于校验。    */
    char name_copy[7];  /* Copy of the thread pool name prefix, primarily for logging in debug messages. 仅仅用于打印错误日志。 */
#endif
    atomic_int  num_api_use;    /* Atomic counter for the number of API calls currently using this passport.    */
    _Atomic enum thpool_state state;    /* Atomic state of the thread pool lifecycle.   */
}conc_state_block;

/* 根据是否启用了调试并发API，决定日志是否包含相关信息。    */
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
#define THPOOL_PASSPORT_STATUS_REPORTER(PASSPORT, STATUS) \
"threadpool %p:%s at state %d:%s", (PASSPORT)->bind_pool, (PASSPORT)->name_copy, STATUS, thpool_state_to_string(STATUS)
#else
#define THPOOL_PASSPORT_STATUS_REPORTER(PASSPORT, STATUS) \
"threadpool at state %d:%s", STATUS, thpool_state_to_string(STATUS)
#endif

/* Job */
typedef struct job {
    struct job *prev;                               /* pointer to previous job      */
    /**
     * @brief Task function pointer.
     *
     * The task function signature includes an argument and a handle
     * to current thread. The task function can get access to thread-specific data
     * and thread-metadata with current thread handle.
     * 
     * 函数指针在原有参数基础上，增加了当前线程的句柄，利用该句柄用户可以访问包括线程上下文在内的线程元数据。
     * thread_ctx结构体本身由job函数自由地创建与销毁处置，线程元数据给它提供一个存放的锚点。
     * 需要job与回调函数自己维护thread_ctx的内存安全，线程池本身不予管理。
     * 用户自己也可以使用POSIX提供的pthread_getspecific来实现线程上下文功能。
     * 但是，本方案基于回调的实现，析构过程与时机完全由用户掌握，更透明，更加利于调试。
     */
    void (*function)(void *arg, threadpool_thread); /* function pointer    */
    void *arg;                                      /* function's argument          */
} job;

/* 将锁结构移出jobqueue，jobqueue结构体仅仅关心自己内部的任务，不关心与外部的同步。 */
/* Job queue */
typedef struct jobqueue {
    job     *front;                         /* pointer to front of queue    */
    job     *rear;                          /* pointer to rear  of queue    */
    /**
     * @brief Number of jobs currently in the queue.
     * Accessed under jobqueue_rwmutex protection.
     * 
     * 注意：jobqueue的len成员有以下并发访问场景：
     * 1.`thpool_put_job`（原`jobqueue_push`）；2.`thpool_get_job`（原`jobqueue_pull`)；3.`thpool_wait`
     * 这里，1和2场景是对整个jobqueue的写，因此len和整个jobqueue都在rwmutex的锁内。
     * 但是，3场景，原作者略有不规范，对于len的成员读取未保证一致性，尽管这大概率没什么问题，但是仍然称不上规范。
     * 这里有几个方案解决，a.将len改为原子变量；b.给3场景也使用rwmutex；c.把rwmutex改成读写锁，3场景用读锁，其他场景用写锁。
     * 考虑到，此处3场景实际访问量远远小于1和2场景，读写锁本身开销比互斥锁大，因此使用rwmutex即可。
     */
    int     len;
    /**
     * @brief Maximum number of jobs allowed in the queue.
     * 0 or negative means unlimited.
     * 添加任务队列上限功能。
     */
    int     max_len;
} jobqueue;

/* Thread */
typedef struct thread {
    int         id;                         /* friendly id                  */
    pthread_t   pthread;                    /* pointer to actual thread     */
    struct thpool   *thpool_p;              /* access to thpool             */
    /**
     * @brief Slot for thread-specific context data.
     *
     * This slot is managed by the user via thread start/end callbacks and task functions.
     * It allows sharing data between different tasks executed by the same thread.
     * The memory pointed to by `thread_ctx_slot` is entirely user-managed.
     * 
     * thread_ctx保存线程上下文，允许在线程与任务之间共享数据。
     * 它本质仍由使用者在回调与工作函数中保存、管理与销毁，此处仅用于提供一个保存的位置。
     * 因此命名为“slot”。
     */
    void        *thread_ctx_slot;
    char        thread_name[16];            /* Thread name for debugging/profiling. 线程名，原作者在thread_do中临时创建，这里在thread_init中先创建。    */
    bool        callback_arg_ref_holding;   /* 如果用户提供了回调参数的析构函数，该布尔位指示是否本线程是否对回调参数持有引用。 */
} thread;

/* Threadpool */
/* 相关的一些变量被我修改为atomic_int类型。 */
typedef struct thpool {
    thread      **threads;                  /* pointer to threads           */
    /**
     * Records the number of threads successfully created during initialization.
     * 记录创建时的thread数量，这依然重要，以避免在num_threads_alive不准确时错误统计导致销毁失误。
     */
    int         num_threads;
    atomic_int  num_threads_alive;          /* threads currently alive      */
    /**
     * @brief Atomic counter for threads currently executing a job.
     * Updated outside of job queue lock. Used for signaling when all threads are idle.
     * 
     * 原作者把num_threads_working设置为volitile，此处是没有意义的，原代码中所有操作都在thcount_lock内部。
     * 这里，将num_threads_working修改为atomic_int，并将相关代码修改为彻底与thcount_lock无关。
     * 因此，thcount_lock更名为threads_all_idle_mutex。
     * 在实际实现中，逻辑发生了轻微变化，例如在判断是否发送threads_all_idle时使用num_threads_working的缓存值而非最新值。
     * 但新逻辑并不会造成不良后果，threads_all_idle的发送量不会变化，只是把原本无必要的阻塞判断部分去除了，加大吞吐。
     */
    atomic_int  num_threads_working;        /* threads currently working    */
    pthread_mutex_t threads_all_idle_mutex; /* used for threads_all_idle cond signal    */
    pthread_cond_t  threads_all_idle;       /* signal to thpool_wait        */
    jobqueue    jobqueue;                   /* job queue                    */
    /**
     * Job queue synchronization primitives
     * 把jobqueue的相关同步功能全部上移到thpool中，jobqueue仅关注它自身结构，不再关注信号量同步问题。
     */
    pthread_mutex_t jobqueue_rwmutex;       /* used for queue r/w access    */
    pthread_cond_t  get_job_unblock;        /* 删除原二元信号量has_jobs，变更为条件信号     */
    pthread_cond_t  put_job_unblock;        /* 用于队列已满时的信号量等待功能。             */

    /**
     * 启用TSD机制，检查当前线程是否属于此线程池。
     * 利用此机制阻止用户在本线程池内的线程中，对其所属线程池使用`thpool_wait`、`thpool_shutdown`、`thpool_destroy`等危险操作。
     */
    pthread_key_t   key;

    atomic_bool threads_keepalive;          /* 将全局变量改为移植入内部，允许多个线程池同时存在。     */
    /**
     * @brief Atomic flag: true while thpool_put_job and thpool_get_job are active.
     * Set to false by thpool_wait, set to true by thpool_reactivate.
     * 
     * 添加一个量，设置thpool的等待目标。thpool_wait等待到队列空且无活动线程时，设置它为false，阻塞thpool_get_job与thpool_put_job。
     */
    atomic_bool threads_active;
    /**
     * @brief Prefix for naming worker threads (max 6 chars + null).
     * 
     * 用于调试时给线程池各个线程命名前缀。线程编号是32位正整数，用十六进制表示最大可能为8个字符。
     * prctl最多能接收16字节，这16字节包括"\0"，实际只有15字节。
     * 由于我们的线程编号要预留8字节，连字符需要1字节，能够留给前缀的只有6字节。
     * 加上一个末尾必须的"\0"，前缀命名提供7字节。
     */
    char    thread_name_prefix[7];
    void    (*thread_start_cb)(void *, threadpool_thread current_thrd);
    void    (*thread_end_cb)(threadpool_thread current_thrd);
    void    *callback_arg;
    void    (*callback_arg_destructor)(void *);
    atomic_int  callback_arg_refcount;  /* 如果用户提供了回调参数的析构函数，则进行引用计数。   */
    /**
     * @brief Pointer to the associated concurrency passport.
     *
     * This pointer links the thread pool to its state tracking and debug features.
     * If the user provides a passport in thpool_init config, this points to the user's passport.
     * Otherwise, it points to a passport allocated by the library.
     * 
     * 可由用户传入用于调试的接口，记录了thpool自身状态与api使用情况。
     * 用户必须保证该接口的生存期比该thpool长。用户不传入则自己创建。
     * 把内部名起成与外部开放名相同，以提醒开发者在内部尽量慎用此结构，因为它的所有权可能在用户手里。
     */
    conc_state_block   *debug_conc_passport;
    /**
     * @brief Flag indicating if the associated passport was provided by the user.
     * True if user provided passport in config, false if library allocated it.
     */
    bool    passport_user_owned;
} thpool;

/* ========================== PROTOTYPES ============================ */

// 删除了原作者的thread_hold，以及删除了所有二元信号量相关代码。
// Helper function to initialize a single thread
static int          thread_init(thpool *thpool_p, struct thread **thread_pout, int id);
// The main function executed by each worker thread
static void        *thread_do(void *thread_p_arg);
// Helper function to free thread resources
static void         thread_destroy(struct thread* thread_p);

// 把jobqueue的push和pull均改为无锁保护版本，jobqueue操作仅仅关心自己作为一个结构体该做的事，不去关心与信号同步有关的事。
// Job queue internal helper functions (unsafe - require external synchronization)
static int          jobqueue_init(jobqueue* jobqueue_p, int max_len);
static void         jobqueue_clear_unsafe(jobqueue* jobqueue_p);
static void         jobqueue_push_unsafe(jobqueue* jobqueue_p, struct job* newjob_p);
static struct job  *jobqueue_pull_unsafe(jobqueue* jobqueue_p);
static void         jobqueue_destroy_unsafe(jobqueue* jobqueue_p);

// 新增的非api函数，相当于原作者的jobqueue_push和jobqueue_pull，提供了更复杂的信号同步功能。
// Thread pool internal job handling functions (with synchronization)
static int          thpool_put_job(thpool* thpool_p, struct job* newjob_p);
static struct job  *thpool_get_job(thpool* thpool_p);
// 由部分API使用，判定调用的线程是否属于线程池内。以禁止一些不应由属于线程池的线程进行的操作。
static inline bool  thpool_is_current_thread_owner(thpool* thpool_p);
// 原有api改名为inner。inner的api不涉及conc_state_block
// Inner API functions (do not involve passport checks or use counting)
static int          thpool_wait_inner(thpool* thpool_p);
static int          thpool_add_work_inner(thpool* thpool_p, void (*function_p)(void *, threadpool_thread), void *arg_p);
static int          thpool_reactivate_inner(thpool* thpool_p);
static int          thpool_num_threads_working_inner(thpool* thpool_p);
// 在inner api的基础上增加了涉及conc_state_block的操作。
// 其他api直接在inner api基础上用宏扩充。shutdown和destroy比较特殊，因此从一开始就设计成safe inner api。
/**
 * Safe inner API functions (include passport checks and use counting)
 * These are called by the public API and debug_conc API variants.
 * @param thpool_p The thread pool handle.
 * @param passport The concurrency passport.
 * @return 0 on success, -1 on error.
 */
static int          thpool_shutdown_safe_inner(thpool *thpool_p, conc_state_block *passport);
static int          thpool_destroy_safe_inner(thpool *thpool_p, conc_state_block *passport);
static inline int   thpool_wait_safe_inner(thpool *thpool_p, conc_state_block *passport);
static inline int   thpool_add_work_safe_inner(thpool *thpool_p, conc_state_block *passport, void (*function_p)(void *, threadpool_thread), void *arg_p);
static inline int   thpool_reactivate_safe_inner(thpool *thpool_p, conc_state_block *passport);
static inline int   thpool_num_threads_working_safe_inner(thpool *thpool_p, conc_state_block *passport);

/* 如果启用调试并发API，在头文件中定义此初始化函数。    */
#ifndef THPOOL_ENABLE_DEBUG_CONC_API
conc_state_block   *thpool_debug_conc_passport_init();
#endif

/* ============================ THREAD ============================== */

/**
 * Initialize a thread in the thread pool
 *
 * @param thread_pout   address to the pointer of the thread to be created
 * @param id            id to be given to the thread
 * @return 0 on success, -1 otherwise.
 */
static int thread_init(thpool *thpool_p, struct thread **thread_pout, int id) {
    *thread_pout = malloc(sizeof(struct thread));
    if(unlikely(*thread_pout == NULL)){
        thpool_log_error("thread_init(): Could not allocate memory for thread");
        if(thpool_p->callback_arg_destructor != NULL){
            /* 线程 */
            atomic_fetch_sub_explicit(&thpool_p->callback_arg_refcount, 1, memory_order_acq_rel);
        }
        goto unref_callback_arg;
    }

    (*thread_pout)->thpool_p    = thpool_p;
    (*thread_pout)->id          = id;
    if(thpool_p->callback_arg_destructor != NULL){
        (*thread_pout)->callback_arg_ref_holding = true;
    }
    else{
        (*thread_pout)->callback_arg_ref_holding = false;
    }

    //原作者在thread_do中构造thread_name，这里提前到thread_init中。使用十六进制来表达id，压缩理论最大id的字符量。
    snprintf((*thread_pout)->thread_name, 16, "%s-%x", thpool_p->thread_name_prefix, id);

    /**
     * 线程的thread_context上下文初始化为NULL。
     * 用户可以在线程的回调函数与任意job函数中通过线程句柄获得该线程上下文槽位。
     */
    (*thread_pout)->thread_ctx_slot = NULL;

    int err = pthread_create(&(*thread_pout)->pthread, NULL, thread_do, (*thread_pout));
    if(unlikely(err != 0)){
        thpool_log_error("thread %d:pthread_create_failed, err=%d",id, err);
        errno = err;
        goto cleanup_thread;
    }
    pthread_detach((*thread_pout)->pthread);
    return 0;
cleanup_thread:
    free(*thread_pout);
    *thread_pout = NULL;
unref_callback_arg:
    if(thpool_p->callback_arg_destructor != NULL){
    /**
     * 线程创建失败时，解除对`callback_arg`在本线程未创建时就存在的默认持有。
     * 注意这里不需要也不能对资源进行释放，
     * 因为所有线程如果全部创建失败，这意味着`thpool_init`执行失败，
     * 按照约定，`callback_arg`的生存期管理权只有在`thpool_init`执行成功时才移交。
     * 所以如果所有线程全部创建失败，线程池没有释放权。
     * 究竟是否释放资源应当由拥有全局线程创建信息的`thpool_init`决定，`thpool_init`自己也持有一个引用计数。
     * 所以，此处本来就不可能将引用计数减至0。
     */
        atomic_fetch_sub_explicit(&thpool_p->callback_arg_refcount, 1, memory_order_acq_rel);
    }
    return -1;
}

/**
 * What each thread is doing
 *
 * In principle this is an endless loop. The only time this loop gets interrupted is once
 * thpool_shutdown() is invoked or the program exits.
 *
 * @param  thread_p_arg thread that will run this function
 * @return nothing
 */
static void *thread_do(void *thread_p_arg) {
    struct thread *thread_p = thread_p_arg;

    /* Set thread name for profiling and debugging */
    /* 原作者在此处构造线程名，改为在init构造，仅需使用thread_p->thread_name的构造成品。 */
#if defined(__linux__)
    /* Use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration */
    prctl(PR_SET_NAME, thread_p->thread_name);
#elif defined(__APPLE__) && defined(__MACH__)
    pthread_setname_np(thread_p->thread_name);
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
    pthread_set_name_np(thread_p->pthread, thread_p->thread_name);
#else
    thpool_log_warn("thread_do(): pthread_setname_np is not supported on this system");
#endif

    /* Assure all threads have been created before starting serving */
    thpool *thpool_p = thread_p->thpool_p;

    pthread_setspecific(thpool_p->key, thpool_p);

    /* Mark thread as alive (initialized) */
    atomic_fetch_add(&thpool_p->num_threads_alive, 1);

    /* 执行开始任务回调，如果有的话。   */
    if(thpool_p->thread_start_cb) {
        thpool_p->thread_start_cb(thpool_p->callback_arg, thread_p);
    }

    while(atomic_load(&thpool_p->threads_keepalive)) {

        job *job_p = thpool_get_job(thread_p->thpool_p);

        //如果job_p为NULL，这基本意味着进程池正在被摧毁。
        if(job_p != NULL) {

            atomic_fetch_add(&thpool_p->num_threads_working, 1);

            /* Read job from queue and execute it */
            /* 略作修改，增加了当前线程句柄参数。使用的时候，添加任务只需要输入函数和参数，而定义任务函数的时候除了arg还有线程句柄参数。    */
            job_p->function(job_p->arg, thread_p);
            free(job_p);

            /**
             * 这里将thpool_p->num_threads_working设置为原子值以后，逻辑发生了些许变化。
             * 原作者代码里整个threads_all_idle信号都与num_threads_working的变化同步阻塞。
             * 这里修改为了使用num_threads_working的缓存值判断信号是否发送。
             * ABA等情况这里当然可能出现，但是我们的需求可以容忍，不会造成错误的影响。
             * 当有一个num_threads_working被修改至0时，一定会有一个信号被发送，且不会有两个信号被同时发送。
             * 使用了GNU的扩展__atomic_sub_fetch来获取原子变量的自减更新值。如果没有这个扩展，将使用旧值-1的方式获取缓存。
             * 这里哪些扩展支持是claude告诉我的，对于__GNUC__架构以外的其他编译器的支持情况，尤其是__INTEL_COMPILER的具体版本，我无法确认。
             */
#if defined(__GNUC__) || defined(__clang__) || (defined(__INTEL_COMPILER) && __INTEL_COMPILER >= 1600)
            int num_threads_working = __atomic_sub_fetch(&thpool_p->num_threads_working, 1, __ATOMIC_SEQ_CST);
#else
            int num_threads_working = atomic_fetch_sub(&thpool_p->num_threads_working, 1) - 1;
#endif
            if (!num_threads_working){
                pthread_mutex_lock(&thpool_p->threads_all_idle_mutex);
                /* 这里的signal修改为broadcast，允许支持多个线程都在等待任务队列与工作线程皆空的情形。  */
                pthread_cond_broadcast(&thpool_p->threads_all_idle);
                pthread_mutex_unlock(&thpool_p->threads_all_idle_mutex);
            }
        }
    }
    /* 如果存在任务执行回调，执行任务结束回调。 */
    if(thpool_p->thread_end_cb) {
        thpool_p->thread_end_cb(thread_p);
    }
    atomic_fetch_sub(&thpool_p->num_threads_alive, 1);

    return NULL;
}

/* Frees a thread  */
static void thread_destroy(thread *thread_p) {
    if(unlikely(thread_p == NULL)){
        return;
    }
    /**
     * 如果此时传入destructor的回调参数用户仍未手动解除引用，自动解除引用。
     * 设计上可能把默认回调参数的默认解引用时机放在`thread_destroy`此处，
     * 也可能把默认解引用时机放在`thread_do`即将退出，`end_cb`执行后。
     * 前者（本设计）意味着默认回调参数将在`thpool_destroy`时必定摧毁。
     * 后者（未采用设计）意味着默认回调参数将在`thpool_shutdown`时必定摧毁。
     * 之所以采用本设计，是因为如果用户不手动进行解除引用，则认为用户对于回调参数的生存期不敏感。
     * 若用户有在`thpool_shutdown`时尽早摧毁的需求，可以在`end_cb`中手动解除引用。
     * 而在`thread_destroy`中默认解除引用，符合`thread_destroy`用于销毁资源的定义。
     * 且相比在`thpool_shutdown`的约定，`thread_destroy`销毁回调参数的约定对于用户来说更加可控稳定。
     */
    if(thread_p->callback_arg_ref_holding){
        thpool_thread_unref_callback_arg(thread_p);
    }
    free(thread_p);
}

/* ====================== THREAD WORKER API ========================= */

int thpool_thread_get_id(threadpool_thread current_thrd) {
    return current_thrd->id;
}

const char* thpool_thread_get_name(threadpool_thread current_thrd) {
    return (const char*)current_thrd->thread_name;
}

void * thpool_thread_get_context(threadpool_thread current_thrd) {
    return current_thrd->thread_ctx_slot;
}

void thpool_thread_set_context(threadpool_thread current_thrd, void *ctx) {
    current_thrd->thread_ctx_slot = ctx;
}

void thpool_thread_unset_context(threadpool_thread current_thrd) {
    current_thrd->thread_ctx_slot = NULL;
}

void thpool_thread_unref_callback_arg(threadpool_thread current_thrd) {
    if(current_thrd->callback_arg_ref_holding && likely(current_thrd->thpool_p->callback_arg_destructor != NULL)){
        current_thrd->callback_arg_ref_holding = false;
        if(atomic_fetch_sub_explicit(&current_thrd->thpool_p->callback_arg_refcount, 1, memory_order_acq_rel) == 1){
            current_thrd->thpool_p->callback_arg_destructor(current_thrd->thpool_p->callback_arg);
            thpool_log_debug("callback_arg destructed.");
        }
    }
}

/* ============================ JOB QUEUE =========================== */

/* 新增参数最大任务数man_len。如果不是正整数，视为未设置上限。  */
/* Initialize queue */
static int jobqueue_init(jobqueue *jobqueue_p, int max_len) {
    jobqueue_p->len = 0;
    jobqueue_p->front = NULL;
    jobqueue_p->rear  = NULL;
    jobqueue_p->max_len = (max_len > 0)?max_len:0;

    return 0;
}

/* Clear the queue */
static void jobqueue_clear_unsafe(jobqueue *jobqueue_p) {

    while(jobqueue_p->len){
        free(jobqueue_pull_unsafe(jobqueue_p));
    }

    jobqueue_p->front = NULL;
    jobqueue_p->rear  = NULL;
    jobqueue_p->len = 0;

}

/* Free all queue resources back to the system */
static void jobqueue_destroy_unsafe(jobqueue *jobqueue_p) {
    jobqueue_clear_unsafe(jobqueue_p);
}


/* 修改为不加锁的版本，最简化push逻辑。使用该函数应在读写锁保护下。有保护的版本为thpool_put_job。 */
/* Add (allocated) job to queue
*/
static void jobqueue_push_unsafe(jobqueue *jobqueue_p, struct job *newjob) {

    newjob->prev = NULL;

    switch(jobqueue_p->len){

        case 0:  /* if no jobs in queue */
                    jobqueue_p->front = newjob;
                    jobqueue_p->rear  = newjob;
                    break;

        default: /* if jobs in queue */
                    jobqueue_p->rear->prev = newjob;
                    jobqueue_p->rear = newjob;

    }
    jobqueue_p->len++;
}

/* Get first job from queue(removes it from queue)
* Notice: Caller MUST hold a mutex
*/
static struct job *jobqueue_pull_unsafe(jobqueue *jobqueue_p) {

    job *job_p = jobqueue_p->front;

    switch(jobqueue_p->len){

        case 0:  /* if no jobs in queue */
                    break;

        case 1:  /* if one job in queue */
                    jobqueue_p->front = NULL;
                    jobqueue_p->rear  = NULL;
                    jobqueue_p->len = 0;
                    break;

        default: /* if >1 jobs in queue */
                    jobqueue_p->front = job_p->prev;
                    jobqueue_p->len--;
    }

    return job_p;
}

/* ========================== THREADPOOL ============================ */

/**
 * @brief Initializes a thread pool.
 *
 * Creates and initializes a thread pool with the specified configuration.
 * This function blocks until all worker threads have been successfully created and are ready.
 * It also handles the creation or binding of the concurrency passport based on the config.
 *
 * @param conf Pointer to a threadpool_config structure containing initialization options.
 * Must not be NULL. The memory pointed to by @p conf is only needed during the call to thpool_init.
 * @return threadpool A handle to the created thread pool on success, or NULL on error.
 * Returns NULL if memory allocation fails, thread creation fails, or passport binding/initialization fails.
 */
struct thpool *thpool_init(threadpool_config *conf) {

    if(unlikely(conf == NULL)) {
        errno = EINVAL;
        return NULL;
    }

    int num_threads = (conf->num_threads < 0)?0:conf->num_threads;

    /* Make new thread pool */
    thpool *thpool_p;
    thpool_p = malloc(sizeof(struct thpool));
    if(unlikely(thpool_p == NULL)) {
        thpool_log_error("thpool_init(): Could not allocate memory for thread pool");
        return NULL;
    }

    /* 创建或使用用户提供的同步控制块。 */
    /* Create or use user-provided concurrency control block (passport). */
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
    if(conf->passport == NULL){
        thpool_p->passport_user_owned = false;
        thpool_p->debug_conc_passport = thpool_debug_conc_passport_init();
    }
    else{
        thpool_p->passport_user_owned = true;
        thpool_p->debug_conc_passport = conf->passport;
    }
#else
    thpool_p->passport_user_owned = false;
    thpool_p->debug_conc_passport = thpool_debug_conc_passport_init();
#endif
    if(thpool_p->debug_conc_passport == NULL) {
        thpool_log_error("thpool_init(): Could not allocate memory for conc state block");
        goto cleanup_pool;
    }

    /* 同步控制块绑定。 */
    /* Bind passport to this thread pool. */
    bool bind_success = false;
    enum thpool_state expected = THPOOL_UNBIND;
    while(!atomic_compare_exchange_weak(&thpool_p->debug_conc_passport->state, &expected, THPOOL_ALIVE)) {
        if(likely(expected == THPOOL_UNBIND)) {
            /* 应该是弱交换造成的伪错误，继续循环。 */
            continue;
        }
        /* 其他情况理应是将一个已经被绑定过的passport重复绑定了，因此报告上一个绑定的线程池信息。   */
        else {
            thpool_log_error("passport rebind! The old "THPOOL_PASSPORT_STATUS_REPORTER(thpool_p->debug_conc_passport, expected));
            errno = EINVAL;
            goto cleanup_passport;
        }
    }
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
    thpool_p->debug_conc_passport->bind_pool = thpool_p;
    snprintf(thpool_p->debug_conc_passport->name_copy, sizeof(thpool_p->debug_conc_passport->name_copy), "%s", conf->thread_name_prefix);
#endif
    bind_success = true;

    /* 配置继承。   */
    /* Inherit configuration.   */
    snprintf(thpool_p->thread_name_prefix, sizeof(thpool_p->thread_name_prefix), "%s", conf->thread_name_prefix);
    thpool_p->thread_start_cb = conf->thread_start_cb;
    thpool_p->thread_end_cb = conf->thread_end_cb;
    thpool_p->callback_arg = conf->callback_arg;
    thpool_p->callback_arg_destructor = conf->callback_arg_destructor;
    thpool_p->num_threads = num_threads;

    /* 原子量初始化。   */
    /* Initialize atomics.      */
    atomic_init(&thpool_p->threads_keepalive, true);
    atomic_init(&thpool_p->threads_active, true);
    atomic_init(&thpool_p->num_threads_alive, 0);
    atomic_init(&thpool_p->num_threads_working, 0);
    /**
     * 如果用户对callback_arg传入了析构函数，则各线程默认均持有引用。此外`thpool_init`自己也视为持有引用。
     * `thpool_init`的引用持续到所有线程的创建函数执行完成。
     */
    if(thpool_p->callback_arg_destructor != NULL) {
        atomic_init(&thpool_p->callback_arg_refcount, num_threads + 1);
    }
    else {
        atomic_init(&thpool_p->callback_arg_refcount, 0);
    }

    /* 在任务队列创建前，先创建相关锁。 */
    /* Job queue related locks and condition variables initialization. */
    int err = pthread_mutex_init(&(thpool_p->jobqueue_rwmutex), NULL);
    if(unlikely(err != 0)) { // Check init result
        thpool_log_error("thpool_init(): Could not initialize jobqueue_rwmutex");
        errno = err;
        goto cleanup_passport;
    }
    /* 创建任务队列。   */
    /* Initialise the job queue */
    if(unlikely(jobqueue_init(&thpool_p->jobqueue, conf->work_num_max) == -1)) {
        thpool_log_error("thpool_init(): Could not allocate memory for job queue");
        goto cleanup_jobqueue_rwmutex;
    }

    /* 任务队列条件量初始化。   */
    err = pthread_cond_init(&thpool_p->get_job_unblock, NULL);
    if(unlikely(err != 0)) { // Check init result
        thpool_log_error("thpool_init(): Could not initialize get_job_unblock");
        errno = err;
        goto cleanup_jobqueue;
    }
    err = pthread_cond_init(&thpool_p->put_job_unblock, NULL);
    if(unlikely(err != 0)) { // Check init result
        thpool_log_error("thpool_init(): Could not initialize put_job_unblock");
        errno = err;
        goto cleanup_get_job_unblock;
    }

    /* TSD key 初始化。         */
    err = pthread_key_create(&thpool_p->key, NULL);
    if(unlikely(err != 0)) {
        thpool_log_error("thpool_init(): Could not initialize TSD key");
        errno = err;
        goto cleanup_put_job_unblock;
    }

    /* Make threads in pool */
    thpool_p->threads = malloc(num_threads * sizeof(struct thread *));
    if(unlikely(thpool_p->threads == NULL)) {
        thpool_log_error("thpool_init(): Could not allocate memory for threads");
        goto cleanup_TSD_key;
    }

    /* 线程空闲锁与条件量初始化。 */
    /* Thread idle lock and condition variable initialization. */
    err = pthread_mutex_init(&(thpool_p->threads_all_idle_mutex), NULL);
    if (unlikely(err != 0)) { // Check init result
        thpool_log_error("thpool_init(): Could not initialize threads_all_idle_mutex");
        errno = err;
        goto cleanup_threads_array;
    }
    err = pthread_cond_init(&thpool_p->threads_all_idle, NULL);
    if (unlikely(err != 0)) { // Check init result
        thpool_log_error("thpool_init(): Could not initialize threads_all_idle");
        errno = err;
        goto cleanup_threads_all_idle_mutex;
    }

    /* Thread init */
    int n;
    for(n=0; n<num_threads; n++) {
        int thread_init_err = thread_init(thpool_p, &thpool_p->threads[n], n);
        thpool_log_debug("THPOOL_DEBUG: Created thread %d in pool", n);
        if (unlikely(thread_init_err != 0)) {
            thpool_log_error("init thread %d fail", n);
            num_threads -= 1;
        }
    }
    if(unlikely(num_threads <= 0)) {
        goto cleanup_threads_all_idle_cond; 
    }
    /**
     * 如果线程没有全部创建失败，就视为线程池创建成功。此时解除`thpool_init`自身的引用计数。
     * 因为此刻可以保证不会再发生线程池创建失败的可能性，因此如果引用计数降至0，可以放心地执行析构。
     */
    if(thpool_p->callback_arg_destructor != NULL) {
        if(atomic_fetch_sub_explicit(&thpool_p->callback_arg_refcount, 1, memory_order_acq_rel) == 1){
            thpool_p->callback_arg_destructor(thpool_p->callback_arg);
            thpool_log_debug("callback_arg destructed by thpool_init.");
        }
    }

    /* Wait for threads to initialize */
    while(atomic_load(&thpool_p->num_threads_alive) != num_threads) {
        /**
         * 为了让设计简单些，在创建与销毁等性能不那么敏感的地方，没有再添加条件变量。
         * 而相比原作者的自旋等待，采取了`usleep`等待的折中方案。
         */ 
        usleep(10);
    }

    return thpool_p;

cleanup_threads_all_idle_cond:
    pthread_cond_destroy(&thpool_p->threads_all_idle);
cleanup_threads_all_idle_mutex:
    pthread_mutex_destroy(&thpool_p->threads_all_idle_mutex);
cleanup_threads_array:
    free(thpool_p->threads);
cleanup_TSD_key:
    pthread_key_delete(thpool_p->key);
cleanup_put_job_unblock:
    pthread_cond_destroy(&thpool_p->put_job_unblock);
cleanup_get_job_unblock:
    pthread_cond_destroy(&thpool_p->get_job_unblock);
cleanup_jobqueue:
    /* 此处的锁逻辑上是无意义的，仅仅是为了遵循自己设计的API调用约定。    */
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    jobqueue_destroy_unsafe(&thpool_p->jobqueue);
    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
cleanup_jobqueue_rwmutex:
    pthread_mutex_destroy(&(thpool_p->jobqueue_rwmutex));
cleanup_passport:
    if(bind_success) {
        /* 如果最初绑定成功，passport状态块回退回THPOOL_UNBIND。    */
        expected = THPOOL_ALIVE;
        while(!atomic_compare_exchange_weak(&thpool_p->debug_conc_passport->state, &expected, THPOOL_UNBIND)) {
            if(expected == THPOOL_ALIVE) {
                /* 应该是弱交换造成的伪错误，继续循环。 */
                continue;
            }
            /* 不明原因解绑失败，那没办法了，反正都要错误退出了。   */
            else {
                thpool_log_error("passport unbind failed! The "THPOOL_PASSPORT_STATUS_REPORTER(thpool_p->debug_conc_passport, expected));
                // Unbind failed from ALIVE state. What should happen? Passport is likely still bound.
                // This indicates a serious error during initialization cleanup.
                // Maybe just log and continue cleanup, leaving passport in potentially bad state?
                // Or attempt to force the state? Forcing might hide errors. Log and continue is safer.
                break;
            }
        }
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
    thpool_p->debug_conc_passport->bind_pool = NULL;
#endif
    }
    if(!thpool_p->passport_user_owned) {
        /* 若passport非用户持有，无警告地简单清理掉passport对象。   */
        free(thpool_p->debug_conc_passport);
    }
cleanup_pool:
    free(thpool_p);
    return NULL;
}

/* 将thpool的threads_keepalive设置为false，并等待所有线程与正在执行中的thpool_add_work执行完毕。    */
static int thpool_shutdown_safe_inner(thpool *thpool_p, conc_state_block *passport) {
    /* 禁止线程池内的线程本身执行`thpool_shutdown`。    */
    if(thpool_is_current_thread_owner(thpool_p)) {
        errno = EINVAL;
        return -1;
    }
    enum thpool_state expected = THPOOL_ALIVE;
    /* 若交换失败，则发生了重复调用，错误退出。 */
    if(!atomic_compare_exchange_strong(&passport->state, &expected, THPOOL_SHUTTING_DOWN)) {
        thpool_log_error("cannot shutdown! The "THPOOL_PASSPORT_STATUS_REPORTER(passport, expected));
        errno = EINVAL;
        return -1;
    }

    /* End each thread 's infinite loop */
    atomic_store(&thpool_p->threads_keepalive, false);
    atomic_store(&thpool_p->threads_active, false);

    /* 大幅修正原作者基于二元信号做的不优雅摧毁逻辑，一次广播即可保证所有jobqueue的阻塞取消。   */
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    pthread_cond_broadcast(&thpool_p->get_job_unblock);
    pthread_cond_broadcast(&thpool_p->put_job_unblock);
    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);

    /* Poll remaining threads */
    while(atomic_load(&thpool_p->num_threads_alive) != 0) {
        sleep(1);
    }

    /* 等待所有正在使用中的api退出。 */
    while(atomic_load(&passport->num_api_use) != 0) {
        sleep(1);
    }

    /* Job queue cleanup */
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    jobqueue_destroy_unsafe(&thpool_p->jobqueue);
    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);

    expected = THPOOL_SHUTTING_DOWN;
    /* 若交换失败，不明原因，可能是弱交换的固有问题，继续等待   */
    while(!atomic_compare_exchange_weak(&passport->state, &expected, THPOOL_SHUTDOWN)) {
        if(expected == THPOOL_SHUTTING_DOWN) {
            /* 应该是弱交换造成的伪错误，继续循环。 */
            continue;
        }
        /* 其他情况理论不可达，abort。  */
        else {
            thpool_log_fatal("shutdown but status panic! The "THPOOL_PASSPORT_STATUS_REPORTER(passport, expected));
            abort();
        }
    }
    return 0;

}

/* Destroy the threadpool */
static int thpool_destroy_safe_inner(thpool *thpool_p, conc_state_block *passport) {
    /* 禁止线程池内的线程本身执行`thpool_destroy`。    */
    if(thpool_is_current_thread_owner(thpool_p)){
        errno = EINVAL;
        return -1;
    }

    enum thpool_state expected = THPOOL_SHUTDOWN;
    /* 若交换失败，情况可能有多种，分别讨论。   */
    while(!atomic_compare_exchange_weak(&passport->state, &expected, THPOOL_DESTROYING)) {
        /* 根据不同情形决定不同应对 */
        switch(expected){
            case THPOOL_ALIVE: {
                //还没有开始shutdown，尝试运行shutdown，并警告用户尽量先shutdown。
                thpool_log_warn("threadpool "
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
                    "%p:%s "
#endif
                    "has not shutdown yet, `thpool_shutdown` first is recommanded. Try auto shutdown..."
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
                    , passport->bind_pool, passport->name_copy
#endif
                );
                thpool_shutdown_safe_inner(thpool_p, passport);
                /* 不论结果如何，先continue根据新的expected再看。*/
                break;
            }
            case THPOOL_SHUTTING_DOWN: {
                thpool_log_warn("threadpool "
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
                    "%p:%s "
#endif
                    "is shutting down, waiting ..."
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
                    , passport->bind_pool, passport->name_copy
#endif
                );
                sleep(1);
                break;
            }
            case THPOOL_SHUTDOWN: {
                /* 假失败，重试。   */
                break;
            }
            default: {
                thpool_log_error("cannot destroy! The "THPOOL_PASSPORT_STATUS_REPORTER(passport, expected));
                errno = EINVAL;
                return -1;
            }
        }
        continue;
    }
    bool passport_user_owned = thpool_p->passport_user_owned;

    /* Deallocs */
    int n;
    for(n=0; n < thpool_p->num_threads; n++) {
        thread_destroy(thpool_p->threads[n]);
    }
    free(thpool_p->threads);

    pthread_mutex_destroy(&thpool_p->threads_all_idle_mutex);
    pthread_cond_destroy(&thpool_p->threads_all_idle);
    pthread_mutex_destroy(&(thpool_p->jobqueue_rwmutex));
    pthread_cond_destroy(&thpool_p->get_job_unblock);
    pthread_cond_destroy(&thpool_p->put_job_unblock);
    pthread_key_delete(thpool_p->key);

    free(thpool_p);
    expected = THPOOL_DESTROYING;
    /* 若交换失败，不明原因，可能是弱交换的固有问题，继续等待   */
    while(!atomic_compare_exchange_weak(&passport->state, &expected, THPOOL_DESTROYED)) {
        if(expected == THPOOL_DESTROYING) {
            /* 应该是弱交换造成的伪错误，继续循环。 */
            continue;
        }
        /* 理论不可达的情况，abort  */
        else {
            thpool_log_fatal("destroyed but status panic! The "THPOOL_PASSPORT_STATUS_REPORTER(passport, expected));
            abort();
        }
    }
    /* 若passport非用户持有，无警告地简单清理掉passport对象。   */
    if(!passport_user_owned) {
        free(passport);
    }
    return 0;
}

/* 有一个返回值，通知结果是成功还是失败。0为成功，-1为失败。失败一般是因为已经thpool正在shutdown。  */
static int thpool_put_job(thpool *thpool_p, struct job *newjob) {
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    thpool_log_debug("thpool_put_job: Entering, jobqueue.len = %d", thpool_p->jobqueue.len);

    bool thpool_alive = atomic_load(&thpool_p->threads_keepalive);
    bool threads_active = atomic_load(&thpool_p->threads_active);

    /* 在不活跃状态，阻塞。此外，若开启队列最大长度且队列已满，阻塞。但若阻塞期间线程池shutdown，退出。 */
    while(thpool_alive && (!threads_active || (thpool_p->jobqueue.max_len && thpool_p->jobqueue.len >= thpool_p->jobqueue.max_len))) {
        thpool_log_debug("thpool_put_job: Blocking, threads_active = %d, jobqueue.len = %d", threads_active, thpool_p->jobqueue.len);
        pthread_cond_wait(&thpool_p->put_job_unblock, &thpool_p->jobqueue_rwmutex);
        /* 
        * 小心use after free风险！如果不能确保所有thpool_put_job执行完成后才销毁，这里无法保证thpool_p仍然存在！
        * 解决方案：将thpool_destroy拆分成thpool_shutdown和thpool_destroy。thpool_shutdown令所有线程终止，但不销毁资源。
        * thpool_destroy仅销毁资源，必须确保在所有对该thpool执行相关操作的线程全部终止运行，才允许调用，且必须在thpool_shutdown之后调用。
        */
        thpool_alive = atomic_load(&thpool_p->threads_keepalive);
        threads_active = atomic_load(&thpool_p->threads_active);
        thpool_log_debug("thpool_put_job: Woke up");
    }

    //在锁内仅需关心一次keealive情况。后续即使再遭遇thpool的销毁，在锁内也可以保护此流程安全。
    if(!thpool_alive) {
        pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
        errno = ECANCELED;
        return -1;
    }

    jobqueue_push_unsafe(&thpool_p->jobqueue, newjob);

    /* 如果此次行为将job从0变为1，发送一个信号告诉jobqueue非空。    */
    if(thpool_p->jobqueue.len == 1) {
        /* 注意，等待信号量的线程，在收到信号量后，在持有锁时并无优先权，仅仅只是重新放到了竞争锁的一个队列而已。
        * 这意味着，如果多个get_job任务阻塞时，有多个put_job发生，第一个收到信号的get_job不保证一定比其他put_job优先执行。
        * 于是，其他get_job就始终错过了信号。为此，这里需要用广播信号。
        */
        pthread_cond_broadcast(&thpool_p->get_job_unblock);
    }

    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
    
    return 0;
}

static struct job *thpool_get_job(thpool *thpool_p) {
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    bool thpool_alive = atomic_load(&thpool_p->threads_keepalive);
    struct job *ret;

    /**
     * 在不活跃状态，阻塞。此外，若队列空，阻塞。但若阻塞期间线程池摧毁，退出。 
     * 目前对于活跃状态的检查是冗余的，因为只有`thpool_wait`会修改active状态，而此时队列一定为空。
     * 但考虑到可扩展性，保留对`threads_active`的阻塞检查。
     */
    while(thpool_alive && (thpool_p->jobqueue.len == 0 || unlikely(!atomic_load(&thpool_p->threads_active)))) {
        pthread_cond_wait(&thpool_p->get_job_unblock, &thpool_p->jobqueue_rwmutex);
        thpool_alive = atomic_load(&thpool_p->threads_keepalive);
    }

    if(!thpool_alive) {
        pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
        errno = ECANCELED;
        return NULL;
    }

    ret = jobqueue_pull_unsafe(&thpool_p->jobqueue);

    /* 如果jobqueue设置了上限，且此次行为将job队列从满变为满-1，发送一个信号告诉jobqueue非满。    */
    if(thpool_p->jobqueue.max_len && (thpool_p->jobqueue.len == thpool_p->jobqueue.max_len - 1)) {
        /**
         * 注意，等待信号量的线程，在收到信号量后，在持有锁时并无优先权，仅仅只是重新放到了竞争锁的一个队列而已。
         * 这意味着，如果多个put_job任务阻塞时，有多个get_job发生，第一个收到信号的put_job不保证一定比其他get_job优先执行。
         * 于是，其他put_job就始终错过了信号。为此，这里需要用广播信号。
         */
        pthread_cond_broadcast(&thpool_p->put_job_unblock);
    }

    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);

    return ret;
}

static inline bool thpool_is_current_thread_owner(thpool *thpool_p) {
    return pthread_getspecific(thpool_p->key) == thpool_p;
}

/* Add work to the thread pool */
static int thpool_add_work_inner(thpool *thpool_p, void (*function_p)(void *, threadpool_thread), void *arg_p) {
    job *newjob;

    newjob=malloc(sizeof(struct job));
    if (unlikely(newjob == NULL)){
        thpool_log_error("thpool_add_work(): Could not allocate memory for new job");
        return -1;
    }

    /* add function and argument */
    newjob->function=function_p;
    newjob->arg=arg_p;

    /* add job to queue */
    int ret = thpool_put_job(thpool_p, newjob);

    return ret;
}

/* Wait until all jobs have finished */
static int thpool_wait_inner(thpool* thpool_p) {
    /* 禁止线程池内的线程本身执行`thpool_wait`。    */
    if(thpool_is_current_thread_owner(thpool_p)) {
        errno = EINVAL;
        return -1;
    }

    /**
     * 原作者此处用了一个很优雅的while，但是因此这里对thpool_p->jobqueue.len的读取放在了一个可能发生数据竞争的位置。
     * 考虑到thpool_wait此处需要读的次数非常少，因此没有必要考虑其他锁或者高效方案，在这里使用互斥锁判定即可。
     * 宁可丑陋一些，也不能接受潜在的数据竞争问题。
     * 但此处使用了两个锁，因此必须小心死锁的情形，所幸threads_all_idle其他使用的地方均不需要考虑jobqueue_rwmutex。
     */
    pthread_mutex_lock(&thpool_p->threads_all_idle_mutex);
    for(;;) {
        pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
        int jobqueuelen = thpool_p->jobqueue.len;
        int working_threads = atomic_load(&thpool_p->num_threads_working);
        if (jobqueuelen || working_threads != 0) {
            pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
            pthread_cond_wait(&thpool_p->threads_all_idle, &thpool_p->threads_all_idle_mutex);
        }
        else {
            atomic_store(&thpool_p->threads_active, false);
            thpool_log_debug("thpool_wait_inner: jobqueue.len = %d, num_threads_working = %d", jobqueuelen, working_threads);
            pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
            break;
        }
    }
    pthread_mutex_unlock(&thpool_p->threads_all_idle_mutex);
    return 0;
}

/* 解除thpool_put_job的阻塞 */
static int thpool_reactivate_inner(thpool* thpool_p) {
    pthread_mutex_lock(&thpool_p->jobqueue_rwmutex);
    atomic_store(&thpool_p->threads_active, true);
    thpool_log_debug("thpool_reactivate_inner: threads_active successfully set to %d", atomic_load(&thpool_p->threads_active));
    pthread_cond_broadcast(&thpool_p->get_job_unblock);
    pthread_cond_broadcast(&thpool_p->put_job_unblock);
    pthread_mutex_unlock(&thpool_p->jobqueue_rwmutex);
    return 0;
}

static int thpool_num_threads_working_inner(thpool *thpool_p) {
    return atomic_load(&thpool_p->num_threads_working);
}

#define DEFINE_THPOOL_EASY_API_SAFE_INNER(API) \
static inline int thpool_##API##_safe_inner(thpool *thpool_p, conc_state_block *passport) { \
    int ret; \
    atomic_fetch_add(&passport->num_api_use, 1); \
    enum thpool_state state = atomic_load(&passport->state); \
    if(likely(state == THPOOL_ALIVE)) { \
        ret = thpool_##API##_inner(thpool_p); \
    } \
    else { \
        thpool_log_error("use thpool api in bad state! The" THPOOL_PASSPORT_STATUS_REPORTER(passport, state)); \
        errno = EINVAL; \
        ret = -1; \
    } \
    atomic_fetch_sub(&passport->num_api_use, 1); \
    return ret; \
}

DEFINE_THPOOL_EASY_API_SAFE_INNER(wait)
DEFINE_THPOOL_EASY_API_SAFE_INNER(reactivate)
DEFINE_THPOOL_EASY_API_SAFE_INNER(num_threads_working)

static inline int thpool_add_work_safe_inner(thpool *thpool_p, conc_state_block *passport, void (*function_p)(void *, threadpool_thread), void *arg_p) {
    int ret;
    atomic_fetch_add(&passport->num_api_use, 1);
    enum thpool_state state = atomic_load(&passport->state);
    if(likely(state == THPOOL_ALIVE)) {
        ret = thpool_add_work_inner(thpool_p, function_p, arg_p);
    }
    else {
        thpool_log_error("use thpool api in bad state! The" THPOOL_PASSPORT_STATUS_REPORTER(passport, state));
        errno = EINVAL;
        ret = -1;
    }
    atomic_fetch_sub(&passport->num_api_use, 1);
    return ret;
}

/* ============================== API =============================== */

#define DEFINE_THPOOL_EASY_API(API) \
int thpool_##API(thpool *thpool_p) { \
    if(unlikely(thpool_p == NULL)) { \
        errno = EINVAL; \
        return -1; \
    } \
    return thpool_##API##_safe_inner(thpool_p, thpool_p->debug_conc_passport); \
}

DEFINE_THPOOL_EASY_API(wait)
DEFINE_THPOOL_EASY_API(reactivate)
DEFINE_THPOOL_EASY_API(shutdown)
DEFINE_THPOOL_EASY_API(destroy)
DEFINE_THPOOL_EASY_API(num_threads_working)


int thpool_add_work(thpool *thpool_p, void (*function_p)(void *, threadpool_thread), void *arg_p) {
    if(unlikely(thpool_p == NULL)){
        errno = EINVAL;
        return -1;
    }
    return thpool_add_work_safe_inner(thpool_p, thpool_p->debug_conc_passport, function_p, arg_p);
}

/* ===================== DEBUG CONC PASSPORT ======================== */

conc_state_block *thpool_debug_conc_passport_init() {
    conc_state_block *passport = malloc(sizeof(conc_state_block));
    if (unlikely(passport == NULL)){
        thpool_log_error("Could not allocate memory for debug concurrency passport");
        return NULL;
    }
    atomic_init(&passport->num_api_use, 0);
    atomic_init(&passport->state, THPOOL_UNBIND);
#ifdef THPOOL_ENABLE_DEBUG_CONC_API 
    memset(passport->name_copy, 0, sizeof(passport->name_copy));
#endif
    return passport;
}

#ifdef THPOOL_ENABLE_DEBUG_CONC_API 
void thpool_debug_conc_passport_destroy(conc_state_block *passport) {
    if(unlikely(passport == NULL)) {
        return;
    }
    /* 检查当前状态。不论哪种状态都应有警告提示。   */
    switch(atomic_load(&passport->state)) {
        case THPOOL_UNBIND: {
            thpool_log_warn("destroy a unbind passport. Don't bind it to other threadepool any more.");
            break;
        }
        case THPOOL_DESTROYED: {
            thpool_log_warn("destroy a passport whose threadpool %s is destroyed. Don't use debug conc apis with it any more.", passport->name_copy);
            break;
        }
        default: {
            thpool_log_error("destroy a passport whose threadpool %s is at state %d:%s. UAF will happen!", passport->name_copy, passport->state, thpool_state_to_string(passport->state));
        }
    }
    free(passport);
}

#define DEFINE_THPOOL_EASY_DEBUG_CONC_API(API) \
int thpool_##API##_debug_conc(thpool *thpool_p, conc_state_block *passport) { \
    if(unlikely(thpool_p == NULL) || unlikely(passport == NULL)) { \
        errno = EINVAL; \
        return -1; \
    } \
    if(unlikely(passport->bind_pool != thpool_p)) { \
        thpool_log_error("passport bind thpool %p:%s, match failed!", passport->bind_pool, passport->name_copy); \
        errno = EINVAL; \
        return -1; \
    } \
    return thpool_##API##_safe_inner(thpool_p, passport); \
}

DEFINE_THPOOL_EASY_DEBUG_CONC_API(wait)
DEFINE_THPOOL_EASY_DEBUG_CONC_API(reactivate)
DEFINE_THPOOL_EASY_DEBUG_CONC_API(shutdown)
DEFINE_THPOOL_EASY_DEBUG_CONC_API(destroy)
DEFINE_THPOOL_EASY_DEBUG_CONC_API(num_threads_working)

int thpool_add_work_debug_conc(thpool *thpool_p, conc_state_block *passport, void (*function_p)(void *, threadpool_thread), void *arg_p) {
    if(unlikely(thpool_p == NULL) || unlikely(passport == NULL)) {
        errno = EINVAL;
        return -1;
    }
    if(unlikely(passport->bind_pool != thpool_p)) {
        thpool_log_error("passport bind thpool %p:%s, match failed!", passport->bind_pool, passport->name_copy);
        errno = EINVAL;
        return -1;
    }
    return thpool_add_work_safe_inner(thpool_p, passport, function_p, arg_p);
}

#endif