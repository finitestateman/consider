/* Helloworld cluster -- A ping/pong cluster API example.
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

#define MSGTYPE_PING 1
#define MSGTYPE_PONG 2

/* HELLOCLUSTER.PINGALL */
int PingallCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_SendClusterMessage(ctx,NULL,MSGTYPE_PING,"Hey",3);
    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* HELLOCLUSTER.LIST */
int ListCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    size_t numnodes;
    char **ids = SiderModule_GetClusterNodesList(ctx,&numnodes);
    if (ids == NULL) {
        return SiderModule_ReplyWithError(ctx,"Cluster not enabled");
    }

    SiderModule_ReplyWithArray(ctx,numnodes);
    for (size_t j = 0; j < numnodes; j++) {
        int port;
        SiderModule_GetClusterNodeInfo(ctx,ids[j],NULL,NULL,&port,NULL);
        SiderModule_ReplyWithArray(ctx,2);
        SiderModule_ReplyWithStringBuffer(ctx,ids[j],REDISMODULE_NODE_ID_LEN);
        SiderModule_ReplyWithLongLong(ctx,port);
    }
    SiderModule_FreeClusterNodesList(ids);
    return REDISMODULE_OK;
}

/* Callback for message MSGTYPE_PING */
void PingReceiver(SiderModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    SiderModule_Log(ctx,"notice","PING (type %d) RECEIVED from %.*s: '%.*s'",
        type,REDISMODULE_NODE_ID_LEN,sender_id,(int)len, payload);
    SiderModule_SendClusterMessage(ctx,NULL,MSGTYPE_PONG,"Ohi!",4);
    SiderModuleCallReply *reply = SiderModule_Call(ctx, "INCR", "c", "pings_received");
    SiderModule_FreeCallReply(reply);
}

/* Callback for message MSGTYPE_PONG. */
void PongReceiver(SiderModuleCtx *ctx, const char *sender_id, uint8_t type, const unsigned char *payload, uint32_t len) {
    SiderModule_Log(ctx,"notice","PONG (type %d) RECEIVED from %.*s: '%.*s'",
        type,REDISMODULE_NODE_ID_LEN,sender_id,(int)len, payload);
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"hellocluster",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellocluster.pingall",
        PingallCommand_SiderCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"hellocluster.list",
        ListCommand_SiderCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Disable Sider Cluster sharding and redirections. This way every node
     * will be able to access every possible key, regardless of the hash slot.
     * This way the PING message handler will be able to increment a specific
     * variable. Normally you do that in order for the distributed system
     * you create as a module to have total freedom in the keyspace
     * manipulation. */
    SiderModule_SetClusterFlags(ctx,REDISMODULE_CLUSTER_FLAG_NO_REDIRECTION);

    /* Register our handlers for different message types. */
    SiderModule_RegisterClusterMessageReceiver(ctx,MSGTYPE_PING,PingReceiver);
    SiderModule_RegisterClusterMessageReceiver(ctx,MSGTYPE_PONG,PongReceiver);
    return REDISMODULE_OK;
}
