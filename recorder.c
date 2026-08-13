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

static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void* p) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;

static _Atomic unsigned long mallocCount = 0;
static _Atomic unsigned long freeCount = 0;
static _Atomic unsigned long callocCount = 0;
static _Atomic unsigned long reallocCount = 0;

static __thread int insideHook = 0;
static pthread_once_t fileOpen = PTHREAD_ONCE_INIT;
static int fileFD = -1;

static void init_file(void) {
    fileFD = open("alloc_log.txt",
                  O_WRONLY | O_CREAT | O_APPEND,
                  0644
                 );
}

static void open_file(void) {
    pthread_once(&fileOpen, init_file);
}

__attribute__((constructor))
static void preload_init(void) {
    open_file();
}

static uint64_t timestamp(void) {
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
                  "a %lu %ld %zu %p\n",
                  timestamp(),
                  tid,
                  size,
                  ptr
              );

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
                  "f %lu %ld %p\n",
                  timestamp(),
                  tid,
                  ptr
              );

    if (len > 0)
        write(fileFD, buf, (size_t)len);
}

void* malloc(size_t s) {
    if (insideHook) {
        const char msg[] = "RECURSIVE malloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_malloc(s);
    }

    if (!real_malloc) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");
    }

    insideHook = 1;

    open_file();
    void* p = real_malloc(s);
    mallocCount++;
    log_alloc(s, p);

    insideHook = 0;

    return p;
}

void free(void* p) {
    if (!real_free) {
        real_free = dlsym(RTLD_NEXT, "free");
    }

    if (insideHook || !p) {
        return real_free(p);
    }

    insideHook = 1;

    open_file();
    real_free(p);
    freeCount++;
    log_free(p);

    insideHook = 0;

    return;
}

void* calloc(size_t numElements, size_t s) {
    if (insideHook) {
        const char msg[] = "RECURSIVE calloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_calloc(numElements, s);
    }

    if (!real_calloc) {
        real_calloc = dlsym(RTLD_NEXT, "calloc");
    }

    insideHook = 1;

    void* p = real_calloc(numElements, s);
    callocCount++;
    //printf("calloc: %lu %zu %p\n", callocCount, s * numElements, p);

    insideHook = 0;

    return p;
}

void* realloc(void* ptr, size_t s) {
    if (insideHook) {
        const char msg[] = "RECURSIVE realloc\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return real_realloc(ptr, s);
    }

    if (!real_realloc) {
        real_realloc = dlsym(RTLD_NEXT, "realloc");
    }

    insideHook = 1;

    void* p = real_realloc(ptr, s);
    reallocCount++;
    //printf("realloc: %lu %zu %p %p\n", reallocCount , s, p, ptr);

    insideHook = 0;

    return p;
}