/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: 2025 npc1054657282 <ly1054657282 at gmail.com> */
/* SPDX-FileCopyrightText: 2016 Johan Hanssen Seferidis */

/**
 * @file threadpool.h
 * @brief A C thread pool library based on Pithikos/C-Thread-Pool.
 * 
 * This file is part of C-Thread-Pool-Derived, distributed under the MIT License.
 * For full terms see the included LICENSE file.
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
typedef struct thpool *threadpool;

/**
 * @brief An opaque handle for a worker thread within the thread pool.
 *
 * This handle is passed to task functions and thread lifecycle callbacks
 * (`thread_start_cb`, `thread_end_cb`). It allows accessing thread-specific
 * information and managing thread-local context data. Users should not
 * access its internal members directly.
 *
 * `threadpool_thread`是线程池中工作线程的不透明句柄。
 * 该句柄会被传递给任务函数和线程生命周期回调（`thread_start_cb`, `thread_end_cb`）。
 * 它允许访问线程特定信息和管理线程本地上下文数据。使用者不应直接访问其内部成员。
 */
typedef struct thread *threadpool_thread;

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
 * **内存管理约定：**用户必须管理通行证的内存。
 * 用户必须确保通行证的生存期严格长于相关线程池的生存期，并且覆盖使用该通行证的所有API调用。
 * 用户在为线程池提供调试并发passport以后，利用该passport和调试并发api替换旧有的api。
 * 如果遵循这个约定，调试并发API可以帮助检测在线程池池生命周期中不正确的时机进行的API调用，
 * 根据通行证状态记录警告或错误，潜在地阻止池对象本身在释放后被使用。
 */
typedef conc_state_block *thpool_debug_conc_passport;
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
     * 
     * 工作线程名称前缀。工作线程将命名为“prefix-ID”格式。
     * 限制为6个字符加上空终止符，共7个字节。
     */
    char    thread_name_prefix[7];
    /**
     * @brief Number of worker threads to create.
     *
     * Must be a positive integer. If a non-positive value (0 or negative) is provided,
     * `thpool_init` will fail and return null pointer.
     *
     * 要创建的工作线程数量。
     * 必须是正整数。如果提供非正值（0 或负数），`thpool_init` 将失败并返回空指针。
     */
    int     num_threads;
    /**
     * @brief Maximum number of jobs allowed in the queue.
     *
     * If greater than 0, limits the queue size. Adding work when the queue is full
     * will block until space is available or the thread pool is shutdown.
     * If 0 or negative, the queue size is unlimited (limited only by memory).
     * 
     * 队列中允许的最大任务数量。
     * 如果大于0，则限制任务队列大小。当队列已满时添加任务将阻塞，直到任务队列有空间可用或线程池被shutdown。
     * 如果为0或负数，则任务队列大小无限制（仅受内存限制）。
     */
    int     work_num_max;
    /**
     * @brief Callback function executed when a thread starts.
     *
     * This function is called by a worker thread after it has been created
     * and initialized, but before it starts processing jobs.
     * 
     * 在线程启动时执行的回调。
     *
     * @param arg               The shared callback argument provided in @ref callback_arg.
     * 在`callback_arg`中提供的共享回调参数。
     * @param current_thrd      Current thread handle.
     * Users can get access to thread-specific data and thread-metadata with current thread handle.
     * 用户可以使用当前线程句柄访问线程特定数据和线程元数据。
     */
    void    (*thread_start_cb)(void *, threadpool_thread);
    /**
     * @brief Callback function executed when a thread is about to exit.
     *
     * This function is called by a worker thread just before it terminates.
     * 
     * 在线程结束时执行的回调。
     *
     * @param current_thrd      Current thread handle.
     * Users can get access to thread-specific data and thread-metadata with current thread handle.
     * 用户可以使用当前线程句柄访问线程特定数据和线程元数据。
     */
    void    (*thread_end_cb)(threadpool_thread);
    /**
     * @brief Shared argument passed to thread start callbacks.
     *
     * This argument is passed to the @ref thread_start_cb.
     * If stored in thread context, it can be potentially passed to @ref thread_end_cb  and task functions.
     * It should contain configuration or shared data needed by the threads.
     * 
     * 传递给线程启动回调的共享参数。
     * 这个参数会被传递给`thread_start_cb`。
     * 如果用户将它存储在线程上下文中，`thread_end_cb`和任务函数也可以访问它。
     * 它应包含所有线程共同需求的配置或共享数据。
     *
     * @note If a `callback_arg_destructor` is provided, the lifetime of the data
     * pointed to is managed by a reference count . Each worker thread and the thread pool
     * initialization initially hold a reference. References are released when the
     * thread metadata is destroyed (during the `thpool_destroy` process) or when
     * `thpool_thread_unref_callback_arg` is called from within a thread callback.
     * The destructor is called when the last reference is released. If no destructor
     * is provided, the user is solely responsible for managing the lifetime of
     * the pointed-to data.
     * 
     * 如果提供了`callback_arg_destructor`，则指向的数据的生命周期由引用计数管理。
     * 每个工作线程和线程池初始化最初都持有一个引用。当线程元数据被销毁
     * （`thpool_destroy`过程中），或线程回调内部调用`thpool_thread_unref_callback_arg`时，
     * 引用会被释放。当最后一个引用被释放时，会调用析构函数。
     * 如果没有主动干预，`callback_arg`的生存期与线程池本身一样长。
     * 如果没有提供析构函数，则用户完全负责管理指针指向的数据的生命周期。
     * 
     * **Ownership Transfer:** If `thpool_init` returns successfully and a `callback_arg_destructor`
     * is provided, the ownership and lifetime management of the data pointed to by
     * `callback_arg` is transferred to the thread pool. If `thpool_init` fails,
     * the ownership remains with the caller, and the caller is responsible for cleanup.
     * 
     * **所有权转移：** 如果提供了`callback_arg_destructor`且`thpool_init`成功返回，
     * 则`callback_arg`指向的数据的所有权和生命周期管理将转移给线程池托管。
     * 如果`thpool_init`失败，所有权仍归调用者所有，调用者负责清理。
     * 
     * @ref callback_arg_destructor for cleanup of pointed-to data.
     * 参见`callback_arg_destructor`以了解指向数据的清理。
     */
    void    *callback_arg;
    /**
     * @brief Destructor function for @ref callback_arg.
     *
     * If the data pointed to by callback_arg requires cleanup (e.g., was malloc'ed),
     * provide a destructor function here. This function will be called when the
     * reference count for `callback_arg` drops to zero.
     * 
     * `callback_arg` 的析构函数。
     * 如果`callback_arg`指向的数据需要清理（例如，使用`malloc`分配），
     * 在此处提供一个析构函数。当`callback_arg`的引用计数降至零时，将调用此函数。
     *
     * @param arg   The callback argument value (a copy of @ref callback_arg) to be destructed.
     * Only the `ptr` member is relevant for destruction.For proper resource management,
     * non-pointer resources intended to be destructed (e.g., file descriptor) 
     * should be wrapped in a struct and passed via `ptr`.
     * 要析构的回调参数值（`callback_arg`的副本）。
     * 只有 `ptr` 成员与析构相关。为了正确的资源管理，如果用户想要自动析构一个非指针资源（如文件描述符），
     * 应将它包装在结构体中并通过 `ptr` 传递。
     *
     * @note Set to null pointer if callback_arg requires no cleanup (e.g., is just a value,
     * points to static/global memory, or points to stack/compound literal
     * whose scope outlives the thread pool's destruction).
     * 
     * 若没有或不需要清理`callback_arg`，比如说参数是值传递，或者指针指向全局/静态资源，
     * 或是指向作用域超出线程池销毁范围的栈/复合字面量，则应设置为空指针。
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
     * If set to null pointer, the library will internally manage a passport (without exposing it),
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
    thpool_debug_conc_passport  *passport;
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
 * Must not be null pointer. The memory pointed to by @p conf is only needed during the call to thpool_init.
 * `conf`的生存周期仅需要维护到`thpool_init`调用结束为止。
 * 
 * @return threadpool A handle to the created thread pool on success, or null pointer on error.
 * Returns null pointer if memory allocation fails or thread creation fails, or passport binding/initialization fails.
 *
 * @example
 * // Example 1: Basic initialization with stack-allocated config
 * // Note: Ensure callback_arg's lifetime is handled correctly!
 * int main()
 * {
 *     // Define configuration on the stack
 *     threadpool_config conf = {
 *         .thread_name_prefix = "worker",
 *         .num_threads = 4,
 *         .work_num_max = 100,
 *         .thread_start_cb = my_thread_start_callback,
 *         .thread_end_cb = my_thread_end_callback,
 *         .callback_arg = (void *)(uintptr_t)0, // Example: Pass a simple value (This conversion is implementation-defined. Do not dereference the resulting pointer.)
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
 * int main()
 * {
 *     threadpool pool = thpool_init( &( (threadpool_config) {
 *         .thread_name_prefix = "task",
 *         .num_threads = 2,
 *         .work_num_max = 50,
 *         .thread_start_cb = my_thread_start_using_config,
 *         .thread_end_cb = NULL,
 *         .callback_arg = &((MyConfigStruct){ .setting1 = 123, .setting2 = "abc" }), // Pointer to compound literal
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
 * // void my_thread_start_using_config(void *arg, threadpool_thread current_thrd)
 * // {
 * //     MyConfigStruct *config = arg;
 * //     // ... use config->setting1 etc. ...
 * // }
 *
 * @example
 * // Example 3: Initialization with callback_arg pointing to heap-allocated data + destructor
 * typedef struct { int resource_id; } ResourceData;
 * void cleanup_resource_data(void *arg); // User-defined cleanup function
 * // ... main function ...
 * { // Create a new scope to manage memory
 *     ResourceData *res_data = malloc(sizeof(ResourceData));
 *     if (res_data == NULL) { 
 *         // handle error 
 *     }
 *     res_data->resource_id = 42;
 *
 *     threadpool pool = thpool_init( &( (threadpool_config) {
 *         // ... other config ...
 *         .thread_start_cb = my_thread_start_using_resource,
 *         .thread_end_cb = NULL,
 *         .callback_arg = res_data, // Pointer to heap data
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
 * // void cleanup_resource_data(void *arg)
 * // {
 * //     // arg is a copy of the callback_arg passed during init
 * //     ResourceData *data_to_free = arg;
 * //     // ... perform any nested cleanup if needed ...
 * //     free(data_to_free); // Free the top-level block
 * // }
 */
threadpool thpool_init(threadpool_config *conf);

/**
 * @brief Initiates the shutdown process for the thread pool.
 *
 * Signals all worker threads to exit their job processing loop after finishing
 * their current job. Waits for all threads to become not alive and the job queue
 * to be processed or cleared. The thread pool transitions to the SHUTDOWN state.
 * Resources (threads metadata, job queue memory, mutexes, etc.) are NOT freed by this function.
 * Use @ref thpool_destroy to free resources after shutdown is complete.
 * 
 * 通知所有工作线程在完成当前任务后退出其任务处理循环。等待所有线程不再存活，
 * 并且任务队列被处理或清空。线程池转换为`SHUTDOWN`状态。
 * 此函数不会释放资源（线程元数据、任务队列内存、互斥锁等）。
 * 在关闭完成后，使用`thpool_destroy`释放资源。
 * 
 * @note This function blocks until all threads run `thread_end_cb`(if exist).
 * 此函数会阻塞，直到所有线程执行完退出回调（如果有的话）。
 *
 * @param threadpool The thread pool handle. Must not be null pointer.
 * 线程池句柄。不能为空指针。
 * @return int     0 on success, -1 on error (e.g., thpool_p is null pointer, or cannot initiate shutdown from the current state,
 * or called from within a thread pool worker thread).
 * 成功时返回 0，错误时返回 -1（例如，`thpool_p`为空指针，或处于无法被shutdown的状态，
 * 或从线程池工作线程内部调用）。
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
 * 销毁线程池并释放其资源。
 * 此函数释放与线程池关联的所有资源（任务队列内存、互斥锁、条件变量、线程元数据结构体）。
 * 它要求线程池处于`SHUTDOWN`状态（在调用`thpool_shutdown`之后）。
 * 如果线程池处于`ALIVE`状态，它将尝试自动调用`shutdown`（并发出警告）。
 *
 * @param threadpool The thread pool handle to destroy. Must not be null pointer.
 * 要销毁的线程池句柄。不能为空指针。
 * @return int     0 on success, -1 on error (e.g., thpool_p is null pointer, or cannot complete destruction from the current state,
 * or called from within a thread pool worker thread).
 * 成功时返回 0，错误时返回 -1（例如，`thpool_p`为空指针，或处于无法被销毁的状态，
 * 或从线程池工作线程内部调用）。
 */
int thpool_destroy(threadpool);

/**
 * @brief Waits for all queued jobs to finish.
 *
 * Blocks the calling thread until all jobs currently in the queue or being executed
 * by worker threads have finished. Once the job queue is empty and all worker threads
 * are idle, the function returns.
 *
 * After this function returns, the thread pool enters an 'inactive' state where
 * adding new work or retrieving jobs is blocked.
 * Use @ref thpool_reactivate to resume normal operation or @ref thpool_shutdown
 * to shut down the pool permanently.
 * 
 * 等待所有排队任务完成。
 * 阻塞调用线程，直到当前在队列中或正在由工作线程执行的所有任务完成。
 * 一旦任务队列为空且所有工作线程空闲，函数返回。
 * 添加了一些保证：在`thpool_wait`执行完时，线程池进入`inactive`状态。
 * 此状态下所有获得新工作与添加新工作的行为均被阻塞。
 * 使用`thpool_reactivate`让线程池恢复`active`状态，或者用`thpool_shutdown`彻底关闭线程池，
 * 方可解除以上行为的阻塞。
 *
 * @param threadpool The thread pool to wait for. Must not be null pointer.
 * 要等待的线程池。不能为空指针。
 * @return int     0 on success, -1 on error (e.g., thpool_p is null pointer, or pool is not in ALIVE state when called,
 * or called from within a thread pool worker thread).
 * 成功时返回 0，错误时返回 -1（例如，`thpool_p`为空指针，或调用时线程池不在`ALIVE`状态，
 * 或从线程池工作线程内部调用）。
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
 * @param threadpool The thread pool to reactivate. Must not be null pointer.
 * 要重新激活的线程池。不能为空指针。
 * @return int     0 on success, -1 on error (e.g., thpool_p is null pointer, or pool is not in ALIVE state when called).
 * 成功时返回 0，错误时返回 -1（例如，thpool_p 为空指针，或调用时线程池不在 ALIVE 状态。
 */
int thpool_reactivate(threadpool);

/**
 * @brief Gets the current number of threads working on a job.
 * 获取当前正在执行任务的线程数量。
 *
 * @param threadpool The thread pool handle. Must not be null pointer.
 * 线程池句柄。不能为空指针。
 * @return int     The number of threads currently executing a job (>= 0), or -1 on error (e.g., thpool_p is null pointer, or pool is not in ALIVE state when called).
 * 当前正在执行任务的线程数量（>= 0），或错误时返回-1（例如，`thpool_p`为空指针，或调用时线程池不在`ALIVE`状态）。
 */
int thpool_num_threads_working(threadpool);

/**
 * @brief Add work to the job queue.
 *
 * Takes a task function and its argument and adds it to the thread pool's job queue.
 * 
 * 接受一个任务函数及其参数，并将其添加到线程池的任务队列。
 *
 * @param pool         The thread pool handle to which the work will be added.
 * @param function_p   Pointer to the task function to add as work.
 * The function should have the signature void (*)(void *arg, threadpoolthread current_thrd).
 * Must not be null pointer.
 * @param arg          The argument for the task function.
 *
 * @note If the data requires complex cleanup, the task function itself or a mechanism
 * within the task function (e.g., using thread context) must handle it.
 * A separate destructor for task arguments is NOT provided by the library.
 * A common pattern is to pass pointers to heap-allocated data and free the memory inside the task function.
 * Passing pointers to stack-allocated data is generally unsafe unless you can strictly guarantee the lifetime.
 * 如果传入一个结构体指针，建议你传入堆上的指针，并在工作函数中传递到其他位置或销毁。
 * 如果传入栈上的指针，有可能会因为其生命周期结束，导致工作函数使用时发生错误。
 *
 * @return int         0 on success (job added), -1 otherwise (e.g., thread pool is being destroyed).
 * 成功时返回0（任务已添加），否则返回-1（例如线程池正在销毁）。
 *
 * @example
 * // Example 1: Adding a task with a simple integer value
 * void task_process_int(void *args, threadpoolthread current_thrd);
 * // ... main function ...
 *     threadpool pool = thpool_init(&conf);
 *     // ...
 *     int value_to_process = 10;
 *     void *task_arg_int = (void *)(intptr_t)value_to_process; // Pass value (This conversion is implementation-defined. Do not dereference the resulting pointer.)
 *     thpool_add_work(pool, task_process_int, task_arg_int);
 * // ...
 * void task_process_int(void *args, threadpoolthread current_thrd)
 * {
 *     int num = (int)(intptr_t)args; // Access the value (be mindful of narrowing conversion)
 *     printf("Processing integer: %d\n", num);
 *     // No free needed for a value
 * }
 *
 * @example
 * // Example 2: Adding a task with a pointer to heap-allocated data
 * // User is responsible for freeing the data pointed to by the pointer!
 * typedef struct { double x, y; } Point;
 * void task_process_point(void *args, threadpoolthread current_thrd);
 * // ... main function ...
 *     threadpool pool = thpool_init(&conf);
 *     // ...
 *     Point *my_point = malloc(sizeof(Point));
 *     if (my_point == NULL) { 
 *         // handle error  
 *     }
 *     my_point->x = 1.0; my_point->y = 2.5;
 *     void *task_arg_ptr = my_point; // Pass pointer
 *     thpool_add_work(pool, task_process_point, task_arg_ptr);
 *
 * // ... Later in main or elsewhere, after submitting,
 * //     you must ensure my_point is eventually freed *after* the task finishes.
 * //     This requires coordination (e.g., task signals completion, or task frees itself).
 * //     A common pattern is for the task itself to free the memory it received via pointer.
 * // ...
 * void task_process_point(void *args, threadpoolthread current_thrd)
 * {
 *     Point *p = args; // Access the pointer
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
 * void task_use_stack_data(void *args, threadpoolthread current_thrd);
 * // ... inside some_function() { ...
 *     int local_value = 5;
 *     void *task_arg_stack = &local_value;
 *     thpool_add_work(pool, task_use_stack_data, task_arg_stack); // DANGEROUS! Pointer becomes invalid when some_function returns.
 * // ... some_function returns ...
 * // ... task_use_stack_data runs later and uses invalid pointer ...
 * // }
 * // void task_use_stack_data(void *args, threadpoolthread current_thrd)
 * // {
 * //     int *num_ptr = args;
 * //     printf("Processing number: %d\n", *num_ptr); // CRASH or UB!
 * // }
 * @note Passing pointers to data with automatic storage duration (local variables, compound literals)
 * to thpool_add_work is generally UNSAFE unless you can strictly guarantee the pointed-to
 * memory's lifetime covers the entire execution of the task function. This is hard to ensure.
 * Prefer passing values or pointers to dynamically allocated or static storage duration data.
 */
int thpool_add_work(threadpool, void (*function_p)(void *, threadpool_thread), void *arg_p);

/**
 * @brief Gets the ID of the current thread pool thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It retrieves the thread's internal ID from its thread metadata.
 * 
 * 获取当前线程池线程的 ID。
 * 
 * 它从线程元数据中检索线程的内部ID。
 *
 * @param current_thrd  Current thread handle,
 * as passed to the callback or task function.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为null pointer。
 * @return int The internal ID of the calling thread pool thread.
 * 返回调用线程池线程的内部ID，如果`current_thrd`为空指针则返回-1。
 */
int thpool_thread_get_id(threadpool_thread current_thrd);

/**
 * @brief Gets the name of the current thread pool thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It retrieves the thread's name from its thread metadata.
 * 
 * 获取当前线程池线程的名称。
 * 此函数旨在从工作线程的`thread_start_cb`、`thread_end_cb`或任务函数内部调用。
 * 它从线程元数据中检索线程的名称。
 *
 * @param current_thrd Current thread handle,
 * as passed to the callback or task function.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为空指针。
 * @return const char* A pointer to the null-terminated string containing the thread's name.
 * The returned pointer points to internal thread pool memory and must
 * not be modified or freed by the caller. The string is valid for
 * the lifetime of the thread.
 * 返回指向包含线程名称的以`'\0'`结尾的字符串的指针。
 * 返回的指针指向线程池内部内存，调用者不得修改或释放。
 * 字符串在线程的生命周期内有效。如果`current_thrd`为空指针则返回空指针。
 */
const char *thpool_thread_get_name(threadpool_thread current_thrd);

/**
 * @brief Gets the thread-specific context data for the current thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It retrieves the pointer to the user-managed thread context data
 * previously set using @ref thpool_thread_set_context.
 * 
 * 获取当前线程池线程的线程特定上下文数据。
 * 此函数旨在从工作线程的`thread_start_cb`、`thread_end_cb`或任务函数内部调用。
 * 它检索先前使用`thpool_thread_set_context`设置的用户管理的线程上下文数据的指针。
 *
 * @param current_thrd Current thread handle ( @ref threadpool_thread). Must not be null pointer.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为空指针。
 * @return void* A pointer to the thread-specific context data, or null pointer if no context
 * has been set or if current_thrd is null pointer.
 * 指向线程特定上下文数据的指针，如果未设置上下文或`current_thrd`为空指针则返回空指针。
 */
void *thpool_thread_get_context(threadpool_thread current_thrd);

/**
 * @brief Sets the thread-specific context data for the current thread.
 *
 * This function is intended to be called from within a worker thread's
 * @ref thread_start_cb, @ref thread_end_cb, or a task function.
 * It allows the user to associate arbitrary data with the current thread.
 * The lifetime and management of the memory pointed to by `ctx` is
 * the responsibility of the user.
 * 
 * 设置当前线程的线程特定上下文数据。
 * 此函数旨在从工作线程的`thread_start_cb`、`thread_end_cb`或任务函数内部调用。
 * 它允许用户将任意数据与当前线程关联。指向`ctx`的内存的生命周期和管理是用户的责任。
 *
 * @param current_thrd Current thread handle (@ref threadpool_thread). Must not be null pointer.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为空指针。
 * @param ctx          A pointer to the user-managed context data to associate with the thread.
 * Can be null pointer to clear the context.
 * 指向要与线程关联的用户管理的上下文数据的指针。
 * 可以为空指针以清除上下文。如果这么做，语义和`thpool_thread_unset_context`相同。
 */
void thpool_thread_set_context(threadpool_thread current_thrd, void *ctx);

/**
 * @brief Clears the thread-specific context data for the current thread.
 *
 * This is a convenience function equivalent to calling
 * `thpool_thread_set_context(current_thrd, NULL)`.
 * 
 * 清除当前线程的线程特定上下文数据。
 * 这是调用`thpool_thread_set_context(current_thrd, NULL)`的便捷函数。
 *
 * @param current_thrd Current thread handle (@ref threadpool_thread). Must not be null pointer.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为空指针。
 */
void thpool_thread_unset_context(threadpool_thread current_thrd);


/**
 * @brief Decrements the reference count for the shared callback argument
 * associated with the current thread.
 * 递减与当前线程关联的共享回调参数的引用计数。
 *
 * If a `callback_arg_destructor` was provided during thread pool initialization,
 * each worker thread initially holds a reference to the `callback_arg`.
 * By default, this reference is released automatically when the thread metadata
 * is destroyed (during the `thpool_destroy` process). This function allows the user
 * to manually release the reference earlier, for example, after the `callback_arg`
 * is no longer needed within the thread (e.g., when the `thread_start_cb` finishes).
 * 
 * 如果在线程池初始化期间提供了`callback_arg_destructor`，
 * 每个工作线程最初都持有`callback_arg`的一个引用。
 * 默认情况下，此引用在线程元数据被销毁时（在`thpool_destroy`过程中）自动释放。
 * 此函数允许用户在线程中不再需要`callback_arg`时提前手动释放引用
 * （例如在`thread_start_cb`执行结束时）。
 * 如果用户这么做，则不得再在同一线程内使用回调参数，否则可能导致UAF。
 *
 * When the reference count for the `callback_arg` drops to zero across all
 * threads and the thread pool initialization process, the provided
 * `callback_arg_destructor` will be called.
 * 
 * 当`callback_arg`的引用计数在所有线程和线程池初始化过程中降至零时，
 * 将调用用户提供的`callback_arg_destructor`。
 *
 * Calling this function multiple times from the same thread, or calling it
 * when the thread does not hold a reference, has no effect.
 * 
 * 从同一线程多次调用此函数，或在线程不持有引用时调用它，没有效果。
 *
 * @param current_thrd Current thread handle (@ref threadpool_thread). Must not be null pointer.
 * 当前线程句柄，线程池将它作为参数传递给回调或任务函数中以获取。不能为空指针。
 */
void thpool_thread_unref_callback_arg(threadpool_thread current_thrd);

#ifdef THPOOL_ENABLE_DEBUG_CONC_API
/**
 * @brief Initializes a debug concurrency passport.
 *
 * Allocates and initializes a new concurrency passport. This passport can
 * then be provided to @ref thpool_init via the `passport` field in the
 * @ref threadpool_config structure to enable debug concurrency features
 * for a thread pool.
 * 
 * 初始化调试并发通行证。
 * 分配并初始化一个新的并发通行证。然后可以通过`threadpool_config`
 * 结构中的`passport`字段将其提供给`thpool_init`，以启用线程池的调试并发功能。
 *
 * @return thpool_debug_conc_passport A handle to the newly created passport on success, or null pointer on error.
 * 成功时返回新创建的通行证句柄，错误时返回空指针。
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
 * 释放与并发通行证关联的内存。
 * 提供通行证给`thpool_init`的用户负责在关联的线程池被销毁后（例如通过`thpool_destroy_debug_conc`）
 * 调用此函数。
 * 如果在线程池未处于终止状态（`UNBIND`或`DESTROYED`）时销毁通行证，会记录警告，
 * 这表明违反了通行证生命周期约定。
 *
 * @param passport The passport handle to destroy. Can be null pointer.
 * 要销毁的通行证句柄。可以为空指针。
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
 * 使用用户提供的通行证将任务添加到任务队列以进行诊断。
 * 此函数类似于`thpool_add_work`，但要求调用者提供关联的并发通行证。
 * 这使得能够在运行时进行状态检查和验证，以**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p   The thread pool handle. Must not be null pointer.
 * 线程池句柄。不能为空指针。
 * @param passport   The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @param function_p Pointer to the task function. Must not be null pointer.
 * 指向任务函数的指针。不能为空指针。
 * @param arg          The argument for the task function.
 * 任务函数的参数。
 * @return int         0 on success, -1 otherwise (e.g., null pointer handles, passport mismatch, or pool not in ALIVE state).
 * 成功时返回0，否则返回-1（例如句柄为空指针，通行证不匹配，或线程池不在`ALIVE`状态）。
 * @note User is responsible for managing lifetime of data pointed to by `arg`.
 * 用户负责管理`arg`指向的数据的生命周期。
 */
int thpool_add_work_debug_conc(threadpool, thpool_debug_conc_passport, void (*function_p)(void *, threadpool_thread), void *arg_p);

/**
 * @brief Waits for all queued jobs to finish using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_wait but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 * 
 * 使用用户提供的通行证等待所有排队任务完成以进行诊断。
 * 此函数类似于`thpool_wait`，但要求调用者提供关联的并发通行证，以启用**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p The thread pool to wait for. Must not be null pointer.
 * 要等待的线程池句柄。不能为空指针。
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @return int     0 on success, -1 on error (e.g., null pointer handles, passport mismatch, or pool not in ALIVE state when called),
 * or called from within a thread pool worker thread).
 * 成功时返回0，否则返回-1（例如句柄为空指针，通行证不匹配，或线程池不在`ALIVE`状态，或从线程池内部调用）。
 */
int thpool_wait_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Reactivates the thread pool using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_reactivate but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 * 
 * 使用用户提供的通行证重新激活线程池以进行诊断。
 * 此函数类似于`thpool_reactivate`，但要求调用者提供关联的并发通行证，以启用**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p The thread pool to reactivate. Must not be null pointer.
 * 要重新激活的线程池。不能为空指针。
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @return int     0 on success, -1 on error (e.g., null pointer handles, passport mismatch, or pool not in ALIVE state when called).
 * 成功时返回0，否则返回-1（例如句柄为空指针，通行证不匹配，或线程池不在`ALIVE`状态）。
 */
int thpool_reactivate_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Gets the current number of threads working on a job using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_num_threads_working but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 * 
 * 使用用户提供的通行证获取当前正在执行任务的线程数量以进行诊断。
 * 此函数类似于`thpool_num_threads_working`，但要求调用者提供关联的并发通行证，以启用**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p The thread pool handle. Must not be null pointer.
 * 线程池句柄。不能为空指针。
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @return int     The number of threads currently executing a job (>= 0), or -1 on error (e.g., null pointer handles, passport mismatch, or pool not in ALIVE state when called).
 * 返回当前正在执行任务的线程数量（>= 0），或错误时返回-1（例如句柄为空指针，通行证不匹配，或调用时线程池不在`ALIVE`状态）。
 */
int thpool_num_threads_working_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Initiates the shutdown process using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_shutdown but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 * 
 * 使用用户提供的通行证关闭线程池以进行诊断。
 * 此函数类似于`thpool_shutdown`，但要求调用者提供关联的并发通行证，以启用**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p The thread pool handle. Must not be null pointer.
 * 线程池句柄。不能为空指针。
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @return int     0 on success, -1 on error (e.g., null pointer handles, passport mismatch, or cannot initiate shutdown from the current state),
 * or called from within a thread pool worker thread).
 * 成功时返回0，错误时返回-1（例如句柄为空指针，通行证不匹配，或无法从当前状态shutdown，或从线程池工作线程内部调用）。
 */
int thpool_shutdown_debug_conc(threadpool, thpool_debug_conc_passport);

/**
 * @brief Destroys the thread pool and frees its resources using a user-provided passport for diagnosis.
 *
 * This function is similar to @ref thpool_destroy but requires the caller
 * to provide the associated concurrency passport. Enables **debugging and diagnosing**
 * lifecycle-related API misuse.
 * 
 * 使用用户提供的通行证销毁线程池并释放其资源以进行诊断。
 * 此函数类似于`thpool_destroy`，但要求调用者提供关联的并发通行证，以启用**调试和诊断**生命周期相关的API误用。
 *
 * @param thpool_p The thread pool handle to destroy. Must not be null pointer.
 * 要销毁的线程池句柄。不能为空指针。
 * @param passport The user-provided concurrency passport. Must be bound to thpool_p and not be null pointer.
 * The passport's lifetime MUST be strictly longer than the thread pool's.
 * 用户提供的并发通行证。必须绑定到`thpool_p`且不能为空指针。通行证的生命周期必须严格长于线程池的生命周期。
 * @return int     0 on success, -1 on error (e.g., null pointer handles, passport mismatch, or cannot complete destruction from the current state).
 * 成功时返回0，错误时返回-1（例如句柄为空指针，通行证不匹配，或无法从当前状态完成销毁，或从线程池工作线程内部调用）。
 */
int thpool_destroy_debug_conc(threadpool, thpool_debug_conc_passport);
#endif

#ifdef __cplusplus
}
#endif

#endif