/* Server hooks API example
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2019, Salvatore Sanfilippo <antirez at gmail dot com>
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

/* Client state change callback. */
void clientChangeCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(e);

    SiderModuleClientInfo *ci = data;
    printf("Client %s event for client #%llu %s:%d\n",
        (sub == REDISMODULE_SUBEVENT_CLIENT_CHANGE_CONNECTED) ?
            "connection" : "disconnection",
        (unsigned long long)ci->id,ci->addr,ci->port);
}

void flushdbCallback(SiderModuleCtx *ctx, SiderModuleEvent e, uint64_t sub, void *data)
{
    REDISMODULE_NOT_USED(ctx);
    REDISMODULE_NOT_USED(e);

    SiderModuleFlushInfo *fi = data;
    if (sub == REDISMODULE_SUBEVENT_FLUSHDB_START) {
        if (fi->dbnum != -1) {
            SiderModuleCallReply *reply;
            reply = SiderModule_Call(ctx,"DBSIZE","");
            long long numkeys = SiderModule_CallReplyInteger(reply);
            printf("FLUSHDB event of database %d started (%lld keys in DB)\n",
                fi->dbnum, numkeys);
            SiderModule_FreeCallReply(reply);
        } else {
            printf("FLUSHALL event started\n");
        }
    } else {
        if (fi->dbnum != -1) {
            printf("FLUSHDB event of database %d ended\n",fi->dbnum);
        } else {
            printf("FLUSHALL event ended\n");
        }
    }
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"hellohook",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_ClientChange, clientChangeCallback);
    SiderModule_SubscribeToServerEvent(ctx,
        SiderModuleEvent_FlushDB, flushdbCallback);
    return REDISMODULE_OK;
}
