#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <unistd.h>

static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void* p) = NULL;
static void* (*real_calloc)(size_t, size_t) = NULL;
static void* (*real_realloc)(void*, size_t) = NULL;

static _Atomic unsigned long mallocCount = 0;
static _Atomic unsigned long freeCount = 0;
static _Atomic unsigned long callocCount = 0;
static _Atomic unsigned long reallocCount = 0;

static __thread int insideHook = 0;

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

    void* p = real_malloc(s);
    unsigned long n = atomic_fetch_add(&mallocCount, 1) + 1;
    printf("malloc: %lu %zu %p\n", n, s, p);

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

    real_free(p);
    unsigned long n = atomic_fetch_add(&freeCount, 1) + 1;
    printf("free: %lu %p\n", n, p);

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
    unsigned long n = atomic_fetch_add(&callocCount, 1) + 1;
    printf("calloc: %lu %zu %p\n", n, s * numElements, p);

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
    unsigned long n = atomic_fetch_add(&reallocCount, 1) + 1;
    printf("realloc: %lu %zu %p %p\n", n, s, p, ptr);

    insideHook = 0;

    return p;
}