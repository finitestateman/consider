/*
 * Copyright (c) 2020, Michael Grunder <michael dot grunder at gmail dot com>
 *
 * All rights reserved.
 *
 * Sidertribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Sidertributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Sidertributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Sider nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include "alloc.h"
#include <string.h>
#include <stdlib.h>

hisiderAllocFuncs hisiderAllocFns = {
    .mallocFn = malloc,
    .callocFn = calloc,
    .reallocFn = realloc,
    .strdupFn = strdup,
    .freeFn = free,
};

/* Override hisider' allocators with ones supplied by the user */
hisiderAllocFuncs hisiderSetAllocators(hisiderAllocFuncs *override) {
    hisiderAllocFuncs orig = hisiderAllocFns;

    hisiderAllocFns = *override;

    return orig;
}

/* Reset allocators to use libc defaults */
void hisiderResetAllocators(void) {
    hisiderAllocFns = (hisiderAllocFuncs) {
        .mallocFn = malloc,
        .callocFn = calloc,
        .reallocFn = realloc,
        .strdupFn = strdup,
        .freeFn = free,
    };
}

#ifdef _WIN32

void *hi_malloc(size_t size) {
    return hisiderAllocFns.mallocFn(size);
}

void *hi_calloc(size_t nmemb, size_t size) {
    /* Overflow check as the user can specify any arbitrary allocator */
    if (SIZE_MAX / size < nmemb)
        return NULL;

    return hisiderAllocFns.callocFn(nmemb, size);
}

void *hi_realloc(void *ptr, size_t size) {
    return hisiderAllocFns.reallocFn(ptr, size);
}

char *hi_strdup(const char *str) {
    return hisiderAllocFns.strdupFn(str);
}

void hi_free(void *ptr) {
    hisiderAllocFns.freeFn(ptr);
}

#endif
