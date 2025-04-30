/** @mainpage My Thread Pool Library Documentation

# C-Thread-Pool-Refined

This thread pool library is based on the excellent [Pithikos/C-Thread-Pool](https://github.com/Pithikos/C-Thread-Pool) project. We are deeply grateful to Pithikos for providing a solid and minimal foundation for a C thread pool.

The development of this version began out of a need for specific features not present in the original library, particularly the ability to manage multiple thread pool instances within a single application and the requirement for per-thread context and lifecycle callbacks (thread start/end). These features were crucial for integrating the thread pool into a larger project requiring stateful worker threads.

Implementing these features necessitated changes to the library's interface and internal structure, which we felt might diverge significantly from the original author's design philosophy and project goals. Given that our specific requirements led to these interface modifications, and acknowledging that the original repository appeared to have limited recent maintenance (for instance, [Issue #134](https://github.com/Pithikos/C-Thread-Pool/issues/134) regarding a potential issue in the binary semaphore implementation remained unaddressed at the time), we decided it would be more appropriate to develop and release this as a separate library.

During the modification process, several areas were refined or reimplemented:

* **Multiple Instances:** The original library's reliance on global variables for state management was refactored. All state is now encapsulated within the `threadpool` structure, allowing for the creation and management of multiple independent thread pools concurrently.
* **Thread Context and Callbacks:** Added support for thread-specific context (`thread_ctx_slot`) and callbacks executed when a thread starts (`thread_start_cb`) or finishes (`thread_end_cb`). This enables sharing resources or maintaining state across tasks executed by the same thread.
* **Enhanced configuration options, including control over the maximum job queue size:** This allows preventing excessive memory usage by the queue and provides waiting/notification mechanisms when the queue is full.
* **Semaphore Implementation:** The original project's custom binary semaphore implementation, which presented some unexpected behaviors and potential performance limitations in our testing, was replaced entirely. This version utilizes standard POSIX mutexes and condition variables for synchronization, aiming for more predictable and robust behavior.
* **Task Argument Handling:** To provide a more explicit and potentially safer way to pass arguments to task functions, the single `void*` argument was replaced with a `thpool_arg` union. This allows users to clearly indicate whether they are passing a small value (`val`) or a pointer (`ptr`).
* **Lifecycle Control:** The thread pool shutdown and resource destruction phases were explicitly separated (`thpool_shutdown` and `thpool_destroy`) to offer finer control over the pool's lifecycle.

While the original project aimed for strict ANSI C and POSIX compliance, this version, due to the practical need for accessing thread metadata from task contexts, utilizes the `container_of` macro. While widely used in practice (notably in the Linux kernel) and generally reliable on common platforms and compilers, it is worth noting that the pointer arithmetic involved can be considered theoretically outside the strictest interpretation of the C standard regarding pointer provenance. Users in highly constrained or non-standard environments should be mindful of this.

We hope these modifications make the library more flexible and suitable for a wider range of applications, while building upon the strong foundation laid by Pithikos's original work.

@section features Features

## Features

This library includes enhancements and modifications compared to the base Pithikos/C-Thread-Pool.

**Added Features:**

* Support for multiple thread pool instances simultaneously.
* Callbacks for thread start/end and support for thread-specific context data shared between tasks executing on the same thread, useful for scenarios like database connection reuse.
* Enhanced configuration options, including a maximum job queue size and waiting/notification mechanisms to prevent excessive memory usage when the queue is full.
* Integration with a logging functionality (configured via `threadpool_log_config.h`, with an optional default based on `rxi/log.c`).
* Introduction of a debug concurrency passport and associated API variants to help diagnose thread pool lifecycle-related concurrency issues (like potential Use-After-Free).

**Removed Features:**

* Pause/Resume functionality: The original implementation of thread pause (`thpool_pause`) and resume (`thpool_resume`) was based on sending signals (`SIGUSR1`) and utilizing a global flag for synchronization within signal handlers. **This approach, while aiming to control thread execution, has potential reliability concerns due to the complexities of using signals in this manner (including executing non-async-signal-safe functions like `sleep` within a signal handler) and relied on global state which would prevent per-threadpool control.** Given the goal of supporting multiple independent thread pool instances and preferring more robust synchronization primitives (like mutexes and condition variables) for core control flow, this feature was not carried over in this version.

本库包含了相对于基础版本 Pithikos/C-Thread-Pool 的增强和修改。

**新增特性：**

* 支持同时创建多个线程池实例。
* 支持线程启动/结束回调，并支持在同一线程上执行的任务之间共享线程特定的上下文数据，这对于数据库连接复用等场景非常有用。
* 增强的配置选项，包括控制最大任务队列大小。这有助于防止队列过度占用内存，并在队列满时提供等待/通知机制。
* 集成了日志功能（通过`threadpool_log_config.h`进行配置，并提供基于`rxi/log.c`的可选默认实现）。
* 引入了调试用并发通行证及其相关的API变种，用于帮助诊断线程池生命周期相关的并发问题（例如UAF）。这些可选的调试API通过在包含`threadpool.h`之前定义宏 `THPOOL_ENABLE_DEBUG_CONC_API`来启用。有关调试用并发特性的详细用法和API参考，请查阅`threadpool.h`头文件中的注释。
* 使用`thpool_arg` Union处理任务参数，方式灵活，可以清晰地传递值或指针。
* 将线程池的关闭 (`thpool_shutdown`) 与资源销毁 (`thpool_destroy`) 分离，提供了更精细的控制。

**移除特性：**

* 暂停/继续功能：原版本的线程暂停 (`thpool_pause`) 和继续 (`thpool_resume`) 功能依赖于发送信号 (`SIGUSR1`) 并在信号处理函数中使用全局标志进行同步。这种方法虽然旨在控制线程执行，但由于以这种方式使用信号（包括在信号处理函数中执行非异步信号安全的函数，如`sleep`）所带来的复杂性，可能存在可靠性问题，并且其依赖于全局状态，从而无法实现对每个线程池的独立控制。鉴于支持多个独立线程池实例的目标以及偏好使用更健壮的同步原语（如互斥锁和条件变量）进行核心控制流，此版本未沿用该功能。

@section getting_started Getting Started

## Getting Started

This library is designed for easy integration into your C/C++ projects by including the source files directly.

### 1. Include Source Files

The core source files of this library (`threadpool.h`, `threadpool.c`, `threadpool_log_config.h`) and the optional default logging implementation (`utils/` directory) are located within the `src` directory of this repository.

To include the library in your project, copy the following files and directory from the src directory into your project's source directory:

* `src/threadpool.h`
* `src/threadpool.c`
* `src/threadpool_log_config.h`
* The entire `src/utils` directory (if you intend to use the default logging).

Add these copied files (and the contents of the utils directory if using default logging) to your project's source file list in your build system (`Makefile`, `CMakeLists.txt`, `xmake.lua`, etc.). Ensure that your build system is configured to find the location of `threadpool.h`. If you place the files in a subdirectory within your project (e.g., `your_project_src/threadpool/`), make sure your compiler's include paths are adjusted accordingly.

本库的核心源文件（`threadpool.h`，`threadpool.c`，`threadpool_log_config.h`）以及可选的默认日志实现（`utils/`目录）位于本仓库的`src`目录下。

要将本库包含到您的项目中，请将以下文件和目录从`src`目录下复制到您的项目源代码目录中：

* `src/threadpool.h`
* `src/threadpool.c`
* `src/threadpool_log_config.h`
* 整个`src/utils`目录（如果您打算使用默认日志功能）。

将这些复制的文件（如果使用默认日志，则包括`utils`目录下的内容）添加到您的项目的构建系统（`Makefile`、`CMakeLists.txt`、`xmake.lua`等）的源文件列表中。确保您的构建系统配置了可以找到`threadpool.h`文件的位置。如果您将文件放置在您的项目内的子目录中（例如`your_project_src/threadpool/`），请确保相应调整编译器的头文件包含路径。

### 2. Configure Logging (Important!)

This library uses internal logging via abstract interfaces (`thpool_log_debug`, `thpool_log_info`, `thpool_log_warn`, `thpool_log_error`). These interfaces are defined in the `threadpool_log_config.h` file. You **need to configure this file** to define these interfaces and map them to your preferred logging system or the library's default logging implementation. The content of `threadpool_log_config.h` will determine where the library's log messages go.

本库通过抽象接口 (`thpool_log_debug`、`thpool_log_info`、`thpool_log_warn`、`thpool_log_error`) 使用内部日志功能。这些接口在`threadpool_log_config.h`文件中定义。您**需要配置此文件**，以定义这些杰康并将其映射到您偏好的日志系统，或使用本库提供的默认日志实现。`threadpool_log_config.h`文件的内容将决定本库的日志消息输出到何处。

* Locate the `threadpool_log_config.h` file in the library's source code.
* **This is the file you will modify** to control the library's logging output.

* 在本库的源代码中找到 `threadpool_log_config.h` 文件。
* **这是您将修改的文件**，用于控制本库的日志输出。

You have two main options for configuring logging:

您有两种主要的选择来配置日志：

#### Option 1: Use the Provided Default Logging (Recommended for Getting Started)

**选择一：使用库提供的默认日志 (推荐入门使用)**

If you wish to start quickly, you can use the default logging implementation provided with the library in `utils/log.h` and `utils/log.c`. This implementation is based on the simple logging library [rxi/log.c](https://github.com/rxi/log.c). We are thankful to the author for this clean and useful logging utility. To enable it, ensure `utils/log.h` and `utils/log.c` are included in your build process, and your compiler can find `utils/log.h`. Then, you use the default `threadpool_log_config.h` file, which includes `"utils/log.h"` and defines the `thpool_log_*` macros to map to the `log_*` functions.

如果您希望快速开始，可以使用本库在`utils/log.h`和`utils/log.c`中提供的默认日志实现。此实现基于简洁的日志库 [rxi/log.c](https://github.com/rxi/log.c)。我们感谢原作者提供了这个清晰且实用的日志工具。要启用它，请确保`utils/log.h`和`utils/log.c`被包含在您的构建过程中，并且您的编译器可以找到`utils/log.h`。然后，使用默认的`threadpool_log_config.h`文件，它包含了`"utils/log.h"`并定义了`thpool_log_*`宏，将其映射到`log_*`接口。

Ensure your build system correctly compiles `threadpool_log_config.h` (by compiling `threadpool.c` which includes it) and links `utils/log.c`.

请确保您的构建系统正确编译`threadpool_log_config.h`（通过编译包含它的`threadpool.c`）并链接`utils/log.c`。

#### Option 2: Use Your Own Logging System (Advanced Customization)

**选择二：使用您自己的日志系统 (高级定制)**

If you already have your own logging library or wish to use standard output functions (like printf/fprintf), you can modify the threadpool_log_config.h file to define the `thpool_log_*` interfaces, directing the log output to your system.
In this case, make sure you do not include "utils/log.h" or define macros mapping to `log_*` in `threadpool_log_config.h` (unless you intend to mix implementations).

如果您已经有自己的日志库，或者想使用标准输出函数（如 printf/fprintf），您可以修改 threadpool_log_config.h 文件，定义`thpool_log_*`接口，将日志输出导向您的日志系统。在这种情况下，请确保您没有在`threadpool_log_config.h`中包含 "utils/log.h" 或定义映射到`log_*`的宏（除非您打算混合使用实现）。

``` c
// Inside threadpool_log_config.h - MODIFY THIS FILE

// --- Enable Your Own Logging System ---
// If you want to use your own logging implementation, ensure "utils/log.h" is NOT included
// #include "utils/log.h" // Make sure this line is commented out or deleted

// Include headers needed by your logging system
#include <stdio.h> // Example: if mapping to stdio functions
// #include "path/to/your_logging_library.h" // If using an external logging library

// Define the thpool_log_* interfaces to map to your logging functions/macros.
// These interfaces should accept parameters in a printf-like format.
#define thpool_log_debug(fmt, ...) fprintf(stdout, "THPOOL_DEBUG: " fmt "\\n", ##__VA_ARGS__)
#define thpool_log_info(fmt, ...)  fprintf(stdout, "THPOOL_INFO: " fmt "\\n", ##__VA_ARGS__)
#define thpool_log_warn(fmt, ...)  fprintf(stderr, "THPOOL_WARN: " fmt "\\n", ##__VA_ARGS__)
#define thpool_log_error(fmt, ...) do { fprintf(stderr, "THPOOL_ERROR: " fmt "\\n", ##__VA_ARGS__); /* Handle fatal error, e.g., abort() */ abort(); } while(0)

// Or define as empty to disable logging (error logging should typically still cause program termination)
// #define THPOOL_LOG_DEBUG(fmt, ...)
// #define THPOOL_LOG_INFO(fmt, ...)
// #define THPOOL_LOG_WARN(fmt, ...)
// #define THPOOL_LOG_ERROR(fmt, ...) do { abort(); } while(0)
// ... Define other levels similarly ...

// --- If using the provided default logging, see "Option 1" above ---
// -----------------------------------------------------------------
```

Ensure your build system correctly processes your modified `threadpool_log_config.h` file (by compiling `threadpool.c` which includes it), and that any headers or source files required by your logging system (if used) are also correctly handled.

请确保您的构建系统正确处理您修改过的`threadpool_log_config.h`文件（通过编译包含它的`threadpool.c`），并且您的日志系统需要的任何头文件或源文件也得到了正确处理。

### 3. Initialize and Use

Once the source files are included and logging is handled according to Option 1 or 2, you can initialize and use the thread pool. To use the `thpool_log_*` interfaces in your own application code, you must include `threadpool_log_config.h` in your application's source files as well.

一旦包含源文件并在 threadpool_log_config.h 中配置了日志，您就可以在应用程序代码中初始化和使用线程池。使用线程池时，只需要包含`threadpool.h`。若想在您自己的应用程序代码中使用`thpool_log_*`接口，您也必须在您的应用程序源文件中包含`threadpool_log_config.h`

```c
#include "threadpool.h"
#include "threadpool_log_config.h" // Include this to use thpool_log_* in this file

// Define a task function matching the signature void (*function_p)(thpool_arg arg, void** thread_ctx_location)
void my_task(thpool_arg arg, void** thread_ctx_location) {
    // ... task logic ...
    // Use the abstract logging interfaces defined in threadpool_log_config.h
    thpool_log_debug("Task executed, arg value: %lld", arg.val);
    // ...
}

int main() {
    threadpool_config conf = {
        .num_threads = 4,
        .work_num_max = 10,
        // .thread_start_cb = your_start_callback, // Optional: set callbacks
        // .thread_end_cb = your_end_callback,
        // .callback_arg = { .ptr = your_callback_arg_creator_function() }, // Optional: argument passed to callbacks
        // .callback_arg_destructor = your_callback_arg_destructor_function, // Optional: destructor for callback_arg
        // ... other configurations ...
    };
    threadpool pool = thpool_init(&conf);

    if (!pool) {
        thpool_log_error("Failed to initialize thread pool"); // This will use the interface configured in threadpool_log_config.h
        return -1;
    }

    // Add work
    thpool_arg task_arg = {.val = 123};
    thpool_log_info("Adding first task");
    thpool_add_work(pool, my_task, task_arg);

    // Add more work in a loop, etc.
    thpool_wait(pool); // Wait for all tasks to finish

    thpool_log_info("Shutting down thread pool");
    thpool_shutdown(pool); // Stop accepting new tasks, wait for current tasks

    thpool_log_info("Destroying thread pool");
    thpool_destroy(pool); // Free resources

    thpool_log_info("Thread pool example finished");

    return 0;
}
```

@section examples Examples

## Examples

The "Getting Started" section provides a basic introduction to using the thread pool. However, to fully understand and utilize the library's features, including callbacks, context data, error handling, and specific work submission patterns, we highly recommend exploring the example programs located in the `examples` directory of this repository. Please note that these example programs are configured to use the default logging implementation based on `rxi/log.c`.

These examples demonstrate:

* Basic thread pool initialization and task submission.
* Using thread start and end callbacks.
* Passing and utilizing thread-specific context data.
* Handling task arguments with the `thpool_arg` union.
* Demonstrating thread pool shutdown and destruction.
* More complex scenarios involving waiting for tasks or managing queue full conditions.

Building and running these examples can provide practical insights into how to best integrate and use the library in your own projects.

“Getting Started” 部分提供了本线程池库的基础使用介绍。然而，为了充分理解和利用库的各项特性，包括回调、上下文数据、错误处理以及特定的任务提交模式，我们强烈建议您查阅本仓库`examples`目录下的示例程序。请注意，这些示例程序配置为使用基于`rxi/log.c`的默认日志实现。

这些示例展示了：

* 线程池的基础初始化和任务提交。
* 使用线程启动和结束回调。
* 传递和利用线程特定的上下文数据。
* 使用`thpool_arg` Union处理任务参数。
* 演示线程池的关闭和销毁流程。
* 涉及等待任务或处理队列满条件等更复杂的场景。

构建并运行这些示例可以为您如何在自己的项目中更好地集成和使用本库提供实用的参考。

### Building and Running Examples with Xmake

This repository includes an `xmake.lua` file at the project root that can be used to build the entire project, including all example programs.

本仓库在项目根目录包含一个`xmake.lua`文件，可用于构建整个项目，包括所有示例程序。

If you have Xmake installed, you can build all examples from the project root directory using the following command:

如果您已经安装了Xmake，您可以在项目根目录下使用以下命令构建所有示例：

``` Bash
xmake
```

This command will compile the library source code and all example programs. The compiled binaries will be placed in the default build output directory (usually `./build`).

此命令将编译库的源代码和所有示例程序。编译生成的二进制文件将放置在默认的构建输出目录中（通常是`./build`）。

To run a specific example (e.g., `thpool_easy_example`), use the `xmake run` command:

要运行特定的示例（例如`thpool_easy_example`），请使用`xmake run`命令：

``` Bash
xmake run thpool_easy_example
```

Replace `thpool_easy_example` with the name of the example program you wish to run.

将`thpool_easy_example`替换为您希望运行的示例程序名称。

@section api_overview API Overview

## API Overview

Here are some of the key functions provided by the library:

* **`threadpool thpool_init(threadpool_config *conf)`**: Initializes a thread pool with the specified configuration.
* **`int thpool_add_work(threadpool pool, void (*function_p)(thpool_arg, void**), thpool_arg arg_p)`**: Adds work (a task function and its argument) to the thread pool's job queue.
* **`int thpool_wait(threadpool)`**: Blocks the calling thread until all queued jobs and currently executing jobs have finished.
* **`int thpool_reactivate(threadpool)`**: Resumes thread pool activity after being paused by `thpool_wait`.
* **`int thpool_num_threads_working(threadpool)`**: Gets the current number of threads actively working on a job.
* **`int thpool_shutdown(threadpool)`**: Initiates the shutdown process, signaling threads to exit after finishing current jobs. Resources are not freed by this function.
* **`int thpool_destroy(threadpool)`**: Destroys the thread pool and frees all associated resources. Requires the pool to be in the SHUTDOWN state, or will attempt auto-shutdown.
* **`int thpool_thread_get_id(void **thread_ctx_location)`**: Gets the internal ID of the calling thread pool thread (intended for use within tasks or callbacks).
* **`const char* thpool_thread_get_name(void **thread_ctx_location)`**: Gets the name of the calling thread pool thread (intended for use within tasks or callbacks).
* **optional debug APIs:** These APIs are enabled by defining the macro `THPOOL_ENABLE_DEBUG_CONC_API` before including threadpool.h. For detailed usage and API reference of the debug concurrency features, please consult the comments within the `threadpool.h` header file.

@section structures Structures

## Structures

* **`threadpool`**: An opaque handle for the thread pool.
* **`thpool_arg`**: Flexible union to carry arguments for task and callback functions (value or pointer).
* **`threadpool_config`**: Structure used to configure the thread pool during initialization.
* **`thpool_debug_conc_passport`**: An opaque handle for the debug concurrency passport (used with `THPOOL_ENABLE_DEBUG_CONC_API`).

@section container_of_note Note on `container_of` Usage for Thread Info

## Note on `container_of` Usage for Thread Info

When retrieving thread-specific metadata using functions like `thpool_thread_get_id` and `thpool_thread_get_name`, this library internally utilizes a common C idiom based on the `container_of` macro (similar to the one used in the Linux kernel).

This technique allows the library to return a pointer to a member within an internal thread structure (`thread::thread_ctx_slot`) and then calculate the address of the containing `thread` structure from that member's address and its known offset. **The calculation involves casting the member pointer to a `uintptr_t` (an unsigned integer type guaranteed to hold a pointer value), performing integer subtraction of the member's offset, and then casting the resulting integer address back to a pointer to the containing structure.**

The primary motivation for this approach is to keep the internal `thread` structure opaque to the user, providing better encapsulation, while still allowing access to essential metadata associated with the executing thread.

It is important to be aware that, according to a strict interpretation of the standard C language rules, particularly concerning pointer provenance and the validity of pointers obtained through non-standard means (like arbitrary integer conversions and arithmetic), the pointer manipulation involved in `container_of` can still be considered theoretically "Undefined Behavior" (UB) when the resulting pointer is used to access the original object.

However, this pattern, specifically using `uintptr_t` for the address calculation, is widely used in practice in many robust C projects (including the Linux kernel) precisely because it aligns well with how hardware treats pointers as memory addresses. In conjunction with the `offsetof` macro (which is standard and guaranteed not to cause UB for standard-layout structs), its behavior is generally reliable on most common architectures and with mainstream compilers (like GCC and Clang) that developers are likely to use. The use of `uintptr_t` is often considered a more portable and less compiler-optimization-sensitive way to perform this address calculation compared to casting to `char*` or performing arithmetic directly on unrelated pointer types.

This implementation assumes that the compiler and target architecture behave in the typical manner where pointers can be safely round-tripped through `uintptr_t` for address calculations.

**If you are working in a highly constrained environment, using an unusual architecture, or a non-standard compiler where you cannot verify the behavior of such pointer arithmetic and conversions, you might need to exercise caution or avoid using the `get thread info` series of functions.** In typical development scenarios on standard platforms, this usage is generally considered safe and reliable in practice, despite the theoretical UB concern.

@section included_components_and_licensing Included Components and Licensing

## Included Components and Licensing

This library includes a copy of the source code for a simple logging utility (`utils/log.h` and `utils/log.c`) as an optional default logging backend (see [Getting Started](#getting-started) - Option 1). This logging utility is the [rxi/log.c](https://github.com/rxi/log.c) project.

We have included this source code for user convenience, allowing them to quickly get started with a working logging solution without external dependencies if they choose the default option.

Please be aware that `rxi/log.c` is distributed under a MIT open-source license. You **must** comply with the terms of its license if you choose to use the included `utils/log.h` and `utils/log.c` files in your project. The license for `rxi/log.c` can be found <https://github.com/rxi/log.c/blob/master/LICENSE> (or in `third_party_licenses` directory of this repository).

If you choose **not** to use the included default logging implementation (Option 2 in [Getting Started](#getting-started)) and instead map `thpool_log_*` to your own logging system, you are not bound by the `rxi/log.c` license.

我们包含了一份用于简单日志工具的源代码（`utils/log.h` 和 `utils/log.c`），作为可选的默认日志后端（参见 [Getting Started](#getting-started) - 选项一）。这个日志工具是 [rxi/log.c](https://github.com/rxi/log.c) 项目。

我们包含这些源代码是为了方便用户，如果他们选择默认选项，可以快速获得一个可工作的日志解决方案，无需额外的外部依赖。

请注意，`rxi/log.c`是在MIT开源许可证下发布的。如果您选择在您的项目中使用包含的`utils/log.h`和`utils/log.c`文件，您**必须**遵守其许可证的条款。`rxi/log.c`的许可证可以在<https://github.com/rxi/log.c/blob/master/LICENSE>找到（或者直接在本代码仓库的 `third_party_licenses` 目录下找到）。

如果您选择**不**使用包含的默认日志实现（[Getting Started](#getting-started) 中的选项二），而是将 `thpool_log_*` 映射到您自己的日志系统，则您不受 `rxi/log.c` 许可证的约束。
*/