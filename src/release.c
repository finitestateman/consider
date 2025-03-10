/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Every time the Sider Git SHA1 or Dirty status changes only this small
 * file is recompiled, as we access this information in all the other
 * files using this functions. */

#include <string.h>
#include <stdio.h>

#include "release.h"
#include "crc64.h"

char *siderGitSHA1(void) {
    return REDIS_GIT_SHA1;
}

char *siderGitDirty(void) {
    return REDIS_GIT_DIRTY;
}

const char *siderBuildIdRaw(void) {
    return REDIS_BUILD_ID_RAW;
}

uint64_t siderBuildId(void) {
    char *buildid = REDIS_BUILD_ID_RAW;

    return crc64(0,(unsigned char*)buildid,strlen(buildid));
}

/* Return a cached value of the build string in order to avoid recomputing
 * and converting it in hex every time: this string is shown in the INFO
 * output that should be fast. */
char *siderBuildIdString(void) {
    static char buf[32];
    static int cached = 0;
    if (!cached) {
        snprintf(buf,sizeof(buf),"%llx",(unsigned long long) siderBuildId());
        cached = 1;
    }
    return buf;
}
