#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <unistd.h>

static void* (*real_malloc)(size_t) = NULL;
static void (*real_free)(void* p) = NULL;

static _Atomic unsigned long mallocCount = 0;
static _Atomic unsigned long freeCount = 0;
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
    if (insideHook || !p) {
        return real_free(p);
    }

    if (!real_free) {
        real_free = dlsym(RTLD_NEXT, "free");
    }

    insideHook = 1;

    real_free(p);
    unsigned long n = atomic_fetch_add(&freeCount, 1) + 1;
    printf("free: %lu %p\n", n, p);

    insideHook = 0;

    return;
}