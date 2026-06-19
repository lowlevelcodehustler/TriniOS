/*
 * libc_stubs.c - Minimal libc stubs for freestanding kernel builds
 *
 * Provides strdup, strtok_r, and other commonly needed functions
 * that are normally part of libc but unavailable with -nostdlib.
 */

#include <stdint.h>
#include <stdbool.h>

/* Freestanding types and forward declarations */
typedef unsigned long size_t;
#ifndef NULL
#define NULL ((void *)0)
#endif

size_t my_strlen(const char *s);
void *my_memcpy(void *dst, const void *src, size_t n);
size_t my_strspn(const char *s, const char *accept);
char *my_strpbrk(const char *s, const char *accept);

/* Simple malloc/free stubs using a static heap */
#define KERNEL_HEAP_SIZE (256 * 1024)
static uint8_t  kernel_heap[KERNEL_HEAP_SIZE];
static size_t   kernel_heap_used = 0;

void *malloc(size_t size)
{
    /* Align to 16 bytes */
    size = (size + 15) & ~15;
    if (kernel_heap_used + size > KERNEL_HEAP_SIZE)
        return NULL;
    void *ptr = &kernel_heap[kernel_heap_used];
    kernel_heap_used += size;
    return ptr;
}

void free(void *ptr)
{
    /* Stub: we never actually free in early boot */
    (void)ptr;
}

/* Minimal freestanding string functions */
size_t my_strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

void *my_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

char *strdup(const char *s)
{
    size_t len = my_strlen(s) + 1;
    char *dup = malloc(len);
    if (dup)
        my_memcpy(dup, s, len);
    return dup;
}

char *strtok_r(char *s, const char *sep, char **last)
{
    if (s == NULL)
        s = *last;
    s += my_strspn(s, sep);
    if (*s == '\0')
        return NULL;
    char *start = s;
    s = my_strpbrk(s, sep);
    if (s) {
        *s = '\0';
        *last = s + 1;
    } else {
        *last = s;
    }
    return start;
}

/* Helper functions used by strtok_r */
size_t my_strspn(const char *s, const char *accept)
{
    const char *p;
    size_t count = 0;
    for (p = s; *p != '\0'; p++, count++) {
        const char *q = accept;
        int found = 0;
        while (*q) {
            if (*q == *p) {
                found = 1;
                break;
            }
            q++;
        }
        if (!found)
            break;
    }
    return count;
}

char *my_strpbrk(const char *s, const char *accept)
{
    const char *p;
    for (p = s; *p != '\0'; p++) {
        const char *q = accept;
        while (*q) {
            if (*q == *p)
                return (char *)p;
            q++;
        }
    }
    return NULL;
}

/* Also provide strlen/memcpy as aliases for libc_stubs.c callers */
#define strlen my_strlen
#define memcpy my_memcpy