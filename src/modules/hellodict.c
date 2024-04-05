/* Hellodict -- An example of modules dictionary API
 *
 * This module implements a volatile key-value store on top of the
 * dictionary exported by the Sider modules API.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2018, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "../sidermodule.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

static SiderModuleDict *Keyspace;

/* HELLODICT.SET <key> <value>
 *
 * Set the specified key to the specified value. */
int cmd_SET(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    SiderModule_DictSet(Keyspace,argv[1],argv[2]);
    /* We need to keep a reference to the value stored at the key, otherwise
     * it would be freed when this callback returns. */
    SiderModule_RetainString(NULL,argv[2]);
    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* HELLODICT.GET <key>
 *
 * Return the value of the specified key, or a null reply if the key
 * is not defined. */
int cmd_GET(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);
    SiderModuleString *val = SiderModule_DictGet(Keyspace,argv[1],NULL);
    if (val == NULL) {
        return SiderModule_ReplyWithNull(ctx);
    } else {
        return SiderModule_ReplyWithString(ctx, val);
    }
}

/* HELLODICT.KEYRANGE <startkey> <endkey> <count>
 *
 * Return a list of matching keys, lexicographically between startkey
 * and endkey. No more than 'count' items are emitted. */
int cmd_KEYRANGE(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) return SiderModule_WrongArity(ctx);

    /* Parse the count argument. */
    long long count;
    if (SiderModule_StringToLongLong(argv[3],&count) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx,"ERR invalid count");
    }

    /* Seek the iterator. */
    SiderModuleDictIter *iter = SiderModule_DictIteratorStart(
        Keyspace, ">=", argv[1]);

    /* Reply with the matching items. */
    char *key;
    size_t keylen;
    long long replylen = 0; /* Keep track of the emitted array len. */
    SiderModule_ReplyWithArray(ctx,REDISMODULE_POSTPONED_LEN);
    while((key = SiderModule_DictNextC(iter,&keylen,NULL)) != NULL) {
        if (replylen >= count) break;
        if (SiderModule_DictCompare(iter,"<=",argv[2]) == REDISMODULE_ERR)
            break;
        SiderModule_ReplyWithStringBuffer(ctx,key,keylen);
        replylen++;
    }
    SiderModule_ReplySetArrayLength(ctx,replylen);

    /* Cleanup. */
    SiderModule_DictIteratorStop(iter);
    return REDISMODULE_OK;
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"hellodict",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellodict.set",
        cmd_SET,"write deny-oom",1,1,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellodict.get",
        cmd_GET,"readonly",1,1,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellodict.keyrange",
        cmd_KEYRANGE,"readonly",1,1,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Create our global dictionary. Here we'll set our keys and values. */
    Keyspace = SiderModule_CreateDict(NULL);

    return REDISMODULE_OK;
}
