#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>

static void *(*real_malloc)(size_t) = NULL;
static void (*real_free)(void *p) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;
static void *(*real_memalign)(size_t, size_t) = NULL;
static int (*real_posix_memalign)(void **, size_t, size_t) = NULL;
static void *(*real_aligned_alloc)(size_t, size_t) = NULL;

static __thread int insideHook = 0;

static pthread_once_t fileOpen = PTHREAD_ONCE_INIT;
static int fileFD = -1;

static void init_file(void)
{
    fileFD = open("alloc_log.txt",
                  O_WRONLY | O_CREAT | O_APPEND,
                  0644);
}

static void open_file(void)
{
    pthread_once(&fileOpen, init_file);
}

__attribute__((constructor)) static void preload_init(void)
{
    open_file();
}

static uint64_t timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL +
           (uint64_t)ts.tv_nsec;
}

static void log_alloc(size_t size, void *ptr)
{
    char buf[128];
    long tid = syscall(SYS_gettid);
    int len = snprintf(
                  buf,
                  sizeof(buf),
                  "a %zu %p %ld %lu\n",
                  size,
                  ptr,
                  tid,
                  timestamp());
    if (len > 0)
        write(fileFD, buf, (size_t)len);
}

static void log_free(void *ptr)
{
    char buf[128];
    long tid = syscall(SYS_gettid);
    int len = snprintf(
                  buf,
                  sizeof(buf),
                  "f %p %ld %lu\n",
                  ptr,
                  tid,
                  timestamp());
    if (len > 0)
        write(fileFD, buf, (size_t)len);
}

static void log_realloc(size_t size, void *new_ptr, void *old_ptr)
{
    char buf[128];
    long tid = syscall(SYS_gettid);
    int len = snprintf(
                  buf,
                  sizeof(buf),
                  "r %zu %p %p %ld %lu\n",
                  size,
                  new_ptr,
                  old_ptr,
                  tid,
                  timestamp());
    if (len > 0)
        write(fileFD, buf, (size_t)len);
}

static void log_calloc(size_t size, size_t numElements, void *ptr)
{
    char buf[128];
    long tid = syscall(SYS_gettid);
    int len = snprintf(
                  buf,
                  sizeof(buf),
                  "c %zu %zu %p %ld %lu\n",
                  size,
                  numElements,
                  ptr,
                  tid,
                  timestamp());
    if (len > 0)
        write(fileFD, buf, (size_t)len);
}

void *malloc(size_t s)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE malloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_malloc(s);
    }
    if (!real_malloc)
    {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    }
    insideHook = 1;
    open_file();
    void *p = real_malloc(s);
    log_alloc(s, p);
    insideHook = 0;
    return p;
}

void free(void *p)
{
    if (!real_free)
    {
        real_free = dlsym(RTLD_NEXT, "free");
    }
    if (insideHook || !p)
    {
        return real_free(p);
    }
    insideHook = 1;
    open_file();
    real_free(p);
    log_free(p);
    insideHook = 0;
    return;
}

void *calloc(size_t numElements, size_t s)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE calloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_calloc(numElements, s);
    }
    if (!real_calloc)
    {
        real_calloc = dlsym(RTLD_NEXT, "calloc");
    }
    insideHook = 1;
    open_file();
    void *p = real_calloc(numElements, s);
    log_calloc(s, numElements, p);
    insideHook = 0;
    return p;
}

void *realloc(void *ptr, size_t s)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE realloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_realloc(ptr, s);
    }
    if (!real_realloc)
    {
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    }
    insideHook = 1;
    open_file();
    void *p = real_realloc(ptr, s);
    log_realloc(s, p, ptr);
    insideHook = 0;
    return p;
}

void *memalign(size_t alignment, size_t size)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE memalign\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_memalign(alignment, size);
    }
    if (!real_memalign)
    {
        real_memalign = dlsym(RTLD_NEXT, "memalign");
    }
    insideHook = 1;
    open_file();
    void *p = real_memalign(alignment, size);
    log_alloc(size, p);
    insideHook = 0;
    return p;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE posix_memalign\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_posix_memalign(memptr, alignment, size);
    }
    if (!real_posix_memalign)
    {
        real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
    }
    insideHook = 1;
    open_file();
    int ret = real_posix_memalign(memptr, alignment, size);
    if (ret == 0 && memptr && *memptr)
    {
        log_alloc(size, *memptr);
    }
    insideHook = 0;
    return ret;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    if (insideHook)
    {
        const char msg[] = "RECURSIVE aligned_alloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_aligned_alloc(alignment, size);
    }
    if (!real_aligned_alloc)
    {
        real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
    }
    insideHook = 1;
    open_file();
    void *p = real_aligned_alloc(alignment, size);
    log_alloc(size, p);
    insideHook = 0;
    return p;
}