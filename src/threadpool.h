/**
 * Copyright (c) 2025 npc1054657282 <ly1054657282 at gmail dot com>
 * Copyright (c) 2016 Johan Hanssen Seferidis
 *
 * This file is part of C-Thread-Pool-Refined, distributed under the MIT License.
 * For full terms see the included LICENSE file.
 *
 * @file threadpool.h
 * @brief A C thread pool library based on Pithikos/C-Thread-Pool.
 *
 * This library implements a thread pool in C, with modifications
 * based on personal project requirements.
 * See the comments in thpool.c for details on added and removed features.
 * 
 * 参考<https://github.com/Pithikos/C-Thread-Pool>实现的线程池。
 * 针对个人项目需求，补充了一些功能，并削减了一些功能。
 * 参见thread_pool.c的注释。
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============================== STRUCTURES ==================================== */

/**
 * @brief An opaque handle for the thread pool.
 *
 * threadpool is an opaque type; users should not access its internal members directly.
 * 
 * threadpool是一个不透明类型，使用者完全不应了解它的一切内部结构，因此它被定义为一个指针类型，而不是一个结构体。
 */
typedef struct thpool_* threadpool;

typedef struct thread_* threadpool_thread;

/**
 * @brief Flexible argument carrier for task and callback functions.
 *
 * This union allows passing either a small value (up to the size of long long)
 * or a pointer (void*) as a single parameter.
 *
 * @note When passing a value that fits within 'val', use the .val member.
 * When passing a pointer to data, use the .ptr member.
 * See @ref thpool_add_work for detailed usage examples.
 */
typedef union threadpool_arg{
    long long val;
    void* ptr;
} threadpool_arg;

#ifdef THPOOL_ENABLE_DEBUG_CONC_API
/**
 * @brief An opaque handle for the debug concurrency passport.
 *
 * In complex concurrent scenarios, incorrect thread pool API usage, particularly
 * related to the thread pool's lifecycle (e.g., using a pool after it's destroyed),
 * can lead to hard-to-debug Use-After-Free (UAF) issues.
 *
 * This library provides the debug concurrency passport and associated APIs
 * as a tool for **debugging and diagnosing** such lifecycle-related concurrency problems.
 *
 * Users enable this debug feature by creating a debug concurrency passport using @ref thpool_debug_conc_passport_init
 * and providing a pointer to it during thread pool initialization via the @ref threadpool_config structure.
 *
 * The debug concurrency passport tracks the thread pool's lifecycle state and counts ongoing API calls.
 * When using the debug concurrency APIs (@ref thpool_add_work_debug_conc, etc.), the user
 * must provide both the thread pool handle and its bound passport.
 *
 * **Memory Management Convention:** The user MUST manage the passport's memory.
 * The user MUST ensure the passport's lifetime is **strictly longer** than the
 * associated thread pool's lifetime, and covers all API calls made using that passport.
 *
 * If this convention is followed, the debug concurrency APIs can help detect
 * erroneous API calls made at incorrect points in the pool's lifecycle,
 * logging warnings or errors based on the passport's state,
 * potentially preventing the `thpool` object itself from being used after free.
 *
 * @note The debug concurrency APIs are intended for diagnosis during development
 * and debugging, not necessarily for general use in production where
 * the performance overhead and the burden of passport management might be undesirable.
 * 
 * 在复杂并发场景下，如果错误地使用了线程池API，可能导致出现uaf的现象。uaf发生的崩溃是非常难以调试的。
 * 针对这种场景，提供了调试并发性通行证与相关api，作为调试和诊断此类生命周期相关并发性问题的工具。
 * 用户可以使用`thpool_debug_conc_passport_init`创建一个调试并发通行证，
 * 并在线程池初始化期间通过`threadpool_config`结构提供一个指向它的指针来启用这个调试特性。
 * 使用调试并发api替换旧有的api时，用户必须同时提供线程池句柄及其绑定通行证。
 * 
 * **内存管理约定：**用户必须管理护照的内存。
 * 用户必须确保通行证的生存期严格长于相关线程池的生存期，并且覆盖使用该通行证的所有API调用。
 * 用户在为线程池提供调试并发passport以后，利用该passport和调试并发api替换旧有的api。
 * 如果遵循这个约定，调试并发API可以帮助检测在线程池池生命周期中不正确的时机进行的API调用，
 * 根据护照状态记录警告或错误，潜在地阻止池对象本身在释放后被使用。
 */
typedef conc_state_block_* thpool_debug_conc_passport;
#endif

/**
 * @brief Configuration structure for initializing the thread pool.
 *
 * Callers use this structure to provide configuration options when creating a thread pool.
 * 
 * 对外开放的线程池配置结构，调用者使用该结构对线程池添加配置。
 */
typedef struct threadpool_config {
    /**
     * @brief Prefix for naming worker threads.
     *
     * Worker threads will be named in the format "prefix-ID".
     * Limited to 6 characters plus null terminator.
     */
    char    thread_name_prefix[7];
    /**
     * @brief Number of worker threads to create.
     * If negative, treated as 0. A value of 0 means the job queue can be used
     * for task storage, but no jobs will be executed by threads.
     * Supplying a non-positive value for `num_threads` is generally discouraged
     * if the pool is intended to execute tasks.
     */
    int     num_threads;
    /**
     * @brief Maximum number of jobs allowed in the queue.
     *
     * If greater than 0, limits the queue size. Adding work when the queue is full
     * will block until space is available or the thread pool is destroyed/made inactive.
     * If 0 or negative, the queue size is unlimited (limited only by memory).
     */
    int     work_num_max;
    /**
     * @brief Callback function executed when a thread starts.
     * 在线程启动时执行的回调。
     *
     * This function is called by a worker thread after it has been created
     * and initialized, but before it starts processing jobs.
     *
     * @param arg               The shared callback argument provided in @ref callback_arg.
     * @param threadpool_thread Current thread handle.
     * Users can get access to thread-specific data and thread-metadata with current thread handle.
     */
    void    (*thread_start_cb)(threadpool_arg arg, threadpool_thread);
    /**
     * @brief Callback function executed when a thread is about to exit.
     * 在线程结束时执行的回调。
     *
     * This function is called by a worker thread just before it terminates.
     *
     * @param threadpool_thread Current thread handle.
     * Users can get access to thread-specific data and thread-metadata with current thread handle.
     */
    void    (*thread_end_cb)(threadpool_thread);
    /**
     * @brief Shared argument passed to thread start callbacks.
     * 线程启动的回调参数。
     *
     * This argument is passed to the @ref thread_start_cb.
     * If stored in thread context, it can be potentially passed to @ref thread_end_cb.
     * It should contain configuration or shared data needed by the threads.
     * 用户可以通过把它寄存在线程上下文中，以使得`thread_end_cb`使用此参数。
     *
     * @note If passing a pointer via the `ptr` member, the lifetime of the data
     * pointed to MUST exceed the lifetime of the thread pool. If passing a simple
     * value via the `val` member, there are no lifetime concerns for the value itself.
     * See @ref callback_arg_destructor for cleanup of pointed-to data.
     */
    threadpool_arg  callback_arg;
    /**
     * @brief Destructor function for @ref callback_arg.
     *
     * If the data pointed to by callback_arg.ptr requires cleanup (e.g., was malloc'ed),
     * provide a destructor function here.
     *
     * @param arg   The callback argument value (a copy of @ref callback_arg) to be destructed.
     * Only the `ptr` member is typically relevant for destruction.
     *
     * @note Set to NULL if callback_arg requires no cleanup (e.g., is just a value,
     * points to static/global memory, or points to stack/compound literal
     * whose scope outlives the thread pool's destruction).
     * This function is called during @ref thpool_destroy.
     * callback_arg的析构函数，若它在栈上或是值传递，或者没有callback_arg，则应设置为NULL。
     * 它直到`thpool_destroy`才会被触发。
     * 因此，即使不是设计本意，用户可以通过把`callback_arg.ptr`保存在各个线程的`thread_ctx`中，
     * 以实现将线程启动回调参数当作一种跨线程共享资源来使用。
     */
    void    (*callback_arg_destructor)(void *);
#ifdef THPOOL_ENABLE_DEBUG_CONC_API
    /**
     * @brief Optional debug concurrency passport.
     *
     * If the user wants to use the debug concurrency APIs for debugging and diagnosis,
     * they must create a passport using @ref thpool_debug_conc_passport_init
     * and provide a pointer to it here.
     * A single passport can only be bound to one thread pool at a time.
     * If set to NULL, the library will internally manage a passport (without exposing it),
     * and the debug concurrency APIs cannot be used with this thread pool instance.
     * 
     * 调试并发通行证。若用户想要使用调试并发api功能，
     * 需要使用`thpool_debug_conc_passport_init`创建调试并发通行证并在创建线程池时传入。
     * 一个调试并发通行证仅能绑定一个线程池，使用调试并发api功能时，同时传入线程池与它所绑定的通行证为参数。
     *
     * @note If providing a passport, the user is responsible for its lifetime.
     * The passport's lifetime MUST be **strictly longer** than the associated thread pool's lifetime
     * and cover all API calls made using that passport.
     * 
     * 如果提供通行证，则用户对其生命周期负责。
     * 通行证的生命周期**必须**严格长于相关线程池的生命周期，并且覆盖使用该通行证的所有API调用。
     */
    thpool_debug_conc_passport *passport;
#endif
} threadpool_config;

/* =================================== API ======================================= */

/**
 * @brief Initializes a thread pool.
 *
 * Creates and initializes a thread pool with the specified configuration.
 * This function blocks until all worker threads have been successfully created and are ready.
 *
 * @param conf Pointer to a threadpool_config structure containing initialization options.
 * Must not be NULL. The memory pointed to by @p conf is only needed during the call to thpool_init.
 * conf的生存周期仅需要维护到thpool_init调用结束为止。
 * 
 * @return threadpool A handle to the created thread pool on success, or NULL on error.
 * Returns NULL if memory allocation fails or thread creation fails, or passport binding/initialization fails.
 *
 * @example
 * // Example 1: Basic initialization with stack-allocated config
 * // Note: Ensure callback_arg's lifetime is handled correctly!
 * int main() {
 *     // Define configuration on the stack
 *     threadpool_config conf = {
 *         .thread_name_prefix = "worker",
 *         .num_threads = 4,
 *         .work_num_max = 100,
 *         .thread_start_cb = my_thread_start_callback,
 *         .thread_end_cb = my_thread_end_callback,
 *         .callback_arg = {.val = 0}, // Example: Pass a simple value
 *         .callback_arg_destructor = NULL // No cleanup needed for a simple value
 *     };
 *     threadpool pool = thpool_init(&conf);
 *     if (pool == NULL) { 
 *         // handle error 
 *     }
 *
 *     // ... add work ...
 *
 *     thpool_wait(pool);
 *     thpool_shutdown(pool);
 *     thpool_destroy(pool); // Ensure destroy happens before 'conf' goes out of scope
 *     return 0;
 * }
 *
 * @example
 * // Example 2: Initialization with callback_arg pointing to a compound literal
 * // Note: This is safe ONLY if thpool_destroy is called within the lifetime of the compound literal!
 * int main() {
 *     threadpool pool = thpool_init( &( (threadpool_config){
 *         .thread_name_prefix = "task",
 *         .num_threads = 2,
 *         .work_num_max = 50,
 *         .thread_start_cb = my_thread_start_using_config,
 *         .thread_end_cb = NULL,
 *         .callback_arg = { .ptr = (void*)&((MyConfigStruct){ .setting1 = 123, .setting2 = "abc" }) }, // Pointer to compound literal
 *         .callback_arg_destructor = NULL // Compound literal on stack, no free needed
 *     } ) );
 *     if (pool == NULL) { 
 *         // handle error 
 *     }
 *
 *     // ... add work ...
 *
 *     thpool_wait(pool);
 *     thpool_shutdown(pool);
 *     thpool_destroy(pool); // <-- REQUIRED to be here (or earlier) for lifetime safety!
 *     return 0;
 * }
 * // Assuming MyConfigStruct is defined elsewhere
 * // void my_thread_start_using_config(threadpool_arg arg, threadpool_thread current_thrd) {
 * //     MyConfigStruct *config = (MyConfigStruct*)arg.ptr;
 * //     // ... use config->setting1 etc. ...
 * // }
 *
 * @example
 * // Example 3: Initialization with callback_arg pointing to heap-allocated data + destructor
 * typedef struct { int resource_id; } ResourceData;
 * void cleanup_resource_data(threadpool_arg arg); // User-defined cleanup function
 * // ... main function ...
 * { // Create a new scope to manage memory
 *     ResourceData *res_data = malloc(sizeof(ResourceData));
 *     if (res_data == NULL) { 
 *         // handle error 
 *     }
 *     res_data->resource_id = 42;
 *
 *     threadpool pool = thpool_init( &( (threadpool_config){
 *         // ... other config ...
 *         .thread_start_cb = my_thread_start_using_resource,
 *         .thread_end_cb = NULL,
 *         .callback_arg = { .ptr = res_data }, // Pointer to heap data
 *         .callback_arg_destructor = cleanup_resource_data // Provide destructor
 *     } ) );
 *     if (pool == NULL) { 
 *         free(res_data); 
 *         // handle error  
 *     } // Clean up if pool init fails
 *
 *     // ... add work ...
 *
 *     thpool_wait(pool);
 *     thpool_shutdown(pool);
 *     thpool_destroy(pool); // Destructor will be called here
 *
 *     // Note: No need to free(res_data) here in main; the destructor does it.
 * } // Scope ends
 *
 * // User-defined cleanup function
 * // void cleanup_resource_data(threadpool_arg arg) {
 * //     // arg is a copy of the threadpool_arg passed during init
 * //     ResourceData *data_to_free = (ResourceData*)arg.ptr;
 * //     // ... perform any nested cleanup if needed ...
 * //     free(data_to_free); // Free the top-level block
 * // }
 */
threadpool thpool_init(threadpool_config *conf);

/**
 * @brief Initiates the shutdown process for the thread pool.
 *
 * Signals all worker threads to exit their job processing loop after finishing
 * their current job. Waits for all threads to become idle and the job queue
 * to be processed or cleared. The thread pool transitions to the SHUTDOWN state.
 * Resources (threads, job queue memory, mutexes, etc.) are NOT freed by this function.
 * Use @ref thpool_destroy to free resources after shutdown is complete.
 *
 * @param threadpool The thread pool handle. Must not be NULL.
 * @return int     0 on success, -1 on error (e.g., thpool_p is NULL, or cannot initiate shutdown from the current state).
 */
int thpool_shutdown(threadpool);

/**
 * @brief Destroys the thread pool and frees its resources.
 *
 * This function frees all resources associated with the thread pool
 * (job queue memory, mutexes, condition variables, thread structs).
 * It requires the thread pool to be in the SHUTDOWN state (e.g., after calling @ref thpool_shutdown).
 * If the thread pool is in the ALIVE state, it will attempt to call shutdown automatically (with a warning).
 *
 * @note This function blocks until all threads have exited and all resources are freed.
 * @param threadpool The thread pool handle to destroy. Must not be NULL.
 * @return int     0 on success, -1 on error (e.g., thpool_p is NULL, or cannot complete destruction from the current state).
 */
int thpool_destroy(threadpool);

/**
 * @brief Waits for all queued jobs to finish.
 *
 * Blocks the calling thread until all jobs currently in the queue or being executed
 * by worker threads have finished. Once the job queue is empty and all worker threads
 * are idle, the function returns.
 *
 * Smart polling is used internally. The polling interval increases if jobs are slow
 * to finish, assuming heavy processing.
 *
 * After this function returns, the thread pool enters an 'inactive' state where
 * adding new work or retrieving jobs is blocked.
 * Use @ref thpool_reactivate to resume normal operation or @ref thpool_shutdown
 * to shut down the pool permanently.
 * 
 * 添加了一些保证：在thpool_wait执行完时，thpool进入inactive状态。
 * 此状态保证thpool的所有获得新工作与添加新工作的行为均被阻塞。
 * 使用thpool_reactivate让thpool恢复active状态，或者用thpool_shutdown彻底关闭thpool，
 * 方可解除以上行为的阻塞。
 *
 * @param threadpool The thread pool to wait for.
 * @return int     0 on success, -1 on error (e.g., thpool_p is NULL, or pool is not in ALIVE state when called).
 */
int thpool_wait(threadpool);

/**
 * @brief Reactivates the thread pool.
 *
 * Resumes thread pool activity after it was paused by @ref thpool_wait.
 * Unblocks tasks waiting to add jobs (@ref thpool_add_work) or threads waiting
 * to retrieve jobs.
 * 解除`thpool_wait`引发的线程不活跃状态，让线程的任务获取与添加解除阻塞。
 *
 * @param threadpool The thread pool to reactivate.
 * @return int     0 on success, -1 on error (e.g., thpool_p is NULL, or pool is not in ALIVE state when called).
 */
int thpool_reactivate(threadpool);

/**
 * @brief Gets the current number of threads working on a job.
 *
 * @param threadpool The thread pool handle. Must not be NULL.
 * @return int     The number of threads currently executing a job (>= 0), or -1 on error (e.g., thpool_p is NULL, or pool is not in ALIVE state when called).
 */
int thpool_num_threads_working(threadpool);

/**
 * @brief Add work to the job queue.
 *
 * Takes a task function and its argument and adds it to the thread pool's job queue.
 *
 * @param pool         The thread pool handle to which the work will be added.
 * @param function_p   Pointer to the task function to add as work.
 * The function should have the signature void (*)(threadpool_arg arg, threadpoolthread current_thrd).
 * Must not be NULL.
 * @param arg          The argument for the task function, encapsulated in a threadpool_arg union.
 * This argument is passed by value.
 * 使用一个联合体threadpool_arg来定义你的参数。你的工作函数仅允许将该参数解释为val或ptr其一。
 * 你在传参时，应严格根据工作函数的解析方式约定你的threadpool_arg的参数传递方式是val还是ptr。
 *
 * @note The user is responsible for ensuring that any data pointed to by arg.ptr
 * remains valid until the task function finishes execution.
 * If the data requires complex cleanup, the task function itself or a mechanism
 * within the task function (e.g., using thread context) must handle it.
 * A separate destructor for task arguments is NOT provided by the library.
 * A common pattern is to pass pointers to heap-allocated data and free the memory inside the task function.
 * Passing pointers to stack-allocated data is generally unsafe unless you can strictly guarantee the lifetime.
 * 禁止在传参时以一种方式定义threadpool_arg，而在工作函数中以另一种方式解析的情况发生。
 * 如果你的传入方式是ptr，即一个结构体指针，建议你传入堆上的指针，并在工作函数中传递到其他位置或销毁。
 * 如果传入栈上的指针，有可能会因为其生命周期结束，导致工作函数使用时发生错误。
 *
 * @return int         0 on success (job added), -1 otherwise (e.g., thread pool is being destroyed).
 *
 * @example
 * // Example 1: Adding a task with a simple integer value
 * void task_process_int(threadpool_arg args, threadpoolthread current_thrd);
 * // ... main function ...
 *     threadpool pool = thpool_init(&conf);
 *     // ...
 *     int value_to_process = 10;
 *     threadpool_arg task_arg_int = {.val = value_to_process}; // Pass value by embedding in union
 *     thpool_add_work(pool, task_process_int, task_arg_int); // Pass union value
 * // ...
 * void task_process_int(threadpool_arg args, threadpoolthread current_thrd) {
 *     int num = (int)args.val; // Access the value (be mindful of narrowing conversion)
 *     printf("Processing integer: %d\n", num);
 *     // No free needed for a value
 * }
 *
 * @example
 * // Example 2: Adding a task with a pointer to heap-allocated data
 * // User is responsible for freeing the data pointed to by the pointer!
 * typedef struct { double x, y; } Point;
 * void task_process_point(threadpool_arg args, threadpoolthread current_thrd);
 * // ... main function ...
 *     threadpool pool = thpool_init(&conf);
 *     // ...
 *     Point *my_point = malloc(sizeof(Point));
 *     if (my_point == NULL) { 
 *         // handle error  
 *     }
 *     my_point->x = 1.0; my_point->y = 2.5;
 *     threadpool_arg task_arg_ptr = {.ptr = my_point}; // Pass pointer by embedding in union
 *     thpool_add_work(pool, task_process_point, task_arg_ptr); // Pass union value containing pointer
 *
 * // ... Later in main or elsewhere, after submitting,
 * //     you must ensure my_point is eventually freed *after* the task finishes.
 * //     This requires coordination (e.g., task signals completion, or task frees itself).
 * //     A common pattern is for the task itself to free the memory it received via pointer.
 * // ...
 * void task_process_point(threadpool_arg args, threadpoolthread current_thrd) {
 *     Point *p = (Point*)args.ptr; // Access the pointer
 *     printf("Processing point: (%f, %f)\n", p->x, p->y);
 *     free(p); // <-- Task takes responsibility for freeing heap data
 * }
 * // ... main continues ...
 * // Note: If task frees, DO NOT free(my_point) again in main.
 * // If task doesn't free, main or another entity must do it *after* the task is known to be complete.
 *
 * @example
 * // Example 3: Adding a task with a pointer to stack-allocated data (DANGEROUS!)
 * // This will likely lead to a Use-After-Free error if the task runs after the calling function returns.
 * void task_use_stack_data(threadpool_arg args, threadpoolthread current_thrd);
 * // ... inside some_function() { ...
 *     int local_value = 5;
 *     threadpool_arg task_arg_stack = {.ptr = &local_value};
 *     thpool_add_work(pool, task_use_stack_data, task_arg_stack); // DANGEROUS! Pointer becomes invalid when some_function returns.
 * // ... some_function returns ...
 * // ... task_use_stack_data runs later and uses invalid pointer ...
 * // }
 * // void task_use_stack_data(threadpool_arg args, threadpoolthread current_thrd) {
 * //     int *num_ptr = (int*)args.ptr;
 * //     printf("Processing number: %d\n", *num_ptr); // CRASH or UB!
 * // }
 * @note Passing pointers to data with automatic storage duration (local variables, compound literals)
 * to thpool_add_work is generally UNSAFE unless you can strictly guarantee the pointed-to
 * memory's lifetime covers the entire execution of the task function. This is hard to ensure.
 * Prefer passing values or pointers to dynamically allocated or static storage duration data.
 */
int thpool_add_work(threadpool, void (*function_p)(threadpool_arg, threadpool_thread), threadpool_arg arg_p);

/**
 * @brief Gets the ID of the current thread pool thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It retrieves the thread's internal ID from its thread context.
 *
 * @param current_thrd  Current thread handle,
 * as passed to the callback or task function.
 * @return int The internal ID of the calling thread pool thread.
 */
int thpool_thread_get_id(threadpool_thread current_thrd);

/**
 * @brief Gets the name of the current thread pool thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It retrieves the thread's name from its thread context.
 *
 * @param current_thrd Current thread handle,
 * as passed to the callback or task function.
 * @return const char* A pointer to the null-terminated string containing the thread's name.
 * The returned pointer points to internal thread pool memory and must
 * not be modified or freed by the caller. The string is valid for
 * the lifetime of the thread.
 */
const char* thpool_thread_get_name(threadpool_thread current_thrd);

void * thpool_thread_get_context(threadpool_thread current_thrd);

void thpool_thread_set_context(threadpool_thread current_thrd, void *ctx);

void thpool_thread_unset_context(threadpool_thread current_thrd);

#ifdef THPOOL_ENABLE_DEBUG_CONC_API
/**
 * @brief Initializes a debug concurrency passport.
 *
 * Allocates and initializes a new concurrency passport. This passport can
 * then be provided to @ref thpool_init via the `passport` field in the
 * @ref threadpool_config structure to enable debug concurrency features
 * for a thread pool.
 *
 * @return thpool_debug_conc_passport A handle to the newly created passport on success, or NULL on error.
 */
thpool_debug_conc_passport thpool_debug_conc_passport_init();

/**
 * @brief Destroys a debug concurrency passport.
 *
 * Frees the memory associated with a concurrency passport.
 * Users who provided a passport to @ref thpool_init are responsible for calling
 * this function after the associated thread pool has been destroyed
 * (e.g., via @ref thpool_destroy_debug_conc).
 * Logs warnings if the passport is destroyed while the thread pool is
 * not in a terminal state (UNBIND or DESTROYED), which indicates
 * a violation of the passport lifetime convention.
 *
 * @param passport The passport handle to destroy. Can be NULL.
 */
void thpool_debug_conc_passport_destroy(thpool_debug_conc_passport);

/**
 * @brief Adds work to the job queue using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_add_work but requires the caller
 * to provide the associated concurrency passport. This enables runtime
 * state checks and validation for **debugging and diagnosing** lifecycle-related
 * API misuse.
 *
 * @param thpool_p   The thread pool handle. Must not be NULL.
 * @param passport   The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @param function_p Pointer to the task function. Must not be NULL.
 * @param arg          The argument for the task function.
 * @return int         0 on success, -1 otherwise (e.g., NULL handles, passport mismatch, or pool not in ALIVE state).
 * @note User is responsible for managing lifetime of data pointed to by `arg.ptr`.
 */
int thpool_add_work_debug_conc(threadpool, thpool_debug_conc_passport, void (*function_p)(threadpool_arg, threadpool_thread), threadpool_arg arg_p);

/**
 * @brief Waits for all queued jobs to finish using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_wait but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 *
 * @param thpool_p The thread pool to wait for. Must not be NULL.
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @return int     0 on success, -1 on error (e.g., NULL handles, passport mismatch, or pool not in ALIVE state when called).
 */
int thpool_wait_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Reactivates the thread pool using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_reactivate but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 *
 * @param thpool_p The thread pool to reactivate. Must not be NULL.
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @return int     0 on success, -1 on error (e.g., NULL handles, passport mismatch, or pool not in ALIVE state when called).
 */
int thpool_reactivate_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Gets the current number of threads working on a job using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_num_threads_working but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 *
 * @param thpool_p The thread pool handle. Must not be NULL.
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @return int     The number of threads currently executing a job (>= 0), or -1 on error (e.g., NULL handles, passport mismatch, or pool not in ALIVE state when called).
 */
int thpool_num_threads_working_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Initiates the shutdown process using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_shutdown but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 *
 * @param thpool_p The thread pool handle. Must not be NULL.
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @return int     0 on success, -1 on error (e.g., NULL handles, passport mismatch, or cannot initiate shutdown from the current state).
 */
int thpool_shutdown_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Destroys the thread pool and frees its resources using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_destroy but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 *
 * @param thpool_p The thread pool handle to destroy. Must not be NULL.
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be NULL.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * @return int     0 on success, -1 on error (e.g., NULL handles, passport mismatch, or cannot complete destruction from the current state).
 */
int thpool_destroy_debug_conc(threadpool, thpool_debug_conc_passport);
#endif

#ifdef __cplusplus
}
#endif

#endif