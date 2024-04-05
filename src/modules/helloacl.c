/* ACL API example - An example for performing custom synchronous and
 * asynchronous password authentication.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
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
#include <pthread.h>
#include <unistd.h>

// A simple global user
static SiderModuleUser *global;
static uint64_t global_auth_client_id = 0;

/* HELLOACL.REVOKE 
 * Synchronously revoke access from a user. */
int RevokeCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (global_auth_client_id) {
        SiderModule_DeauthenticateAndCloseClient(ctx, global_auth_client_id);
        return SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        return SiderModule_ReplyWithError(ctx, "Global user currently not used");    
    }
}

/* HELLOACL.RESET 
 * Synchronously delete and re-create a module user. */
int ResetCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_FreeModuleUser(global);
    global = SiderModule_CreateModuleUser("global");
    SiderModule_SetModuleUserACL(global, "allcommands");
    SiderModule_SetModuleUserACL(global, "allkeys");
    SiderModule_SetModuleUserACL(global, "on");

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* Callback handler for user changes, use this to notify a module of 
 * changes to users authenticated by the module */
void HelloACL_UserChanged(uint64_t client_id, void *privdata) {
    REDISMODULE_NOT_USED(privdata);
    REDISMODULE_NOT_USED(client_id);
    global_auth_client_id = 0;
}

/* HELLOACL.AUTHGLOBAL 
 * Synchronously assigns a module user to the current context. */
int AuthGlobalCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (global_auth_client_id) {
        return SiderModule_ReplyWithError(ctx, "Global user currently used");    
    }

    SiderModule_AuthenticateClientWithUser(ctx, global, HelloACL_UserChanged, NULL, &global_auth_client_id);

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

#define TIMEOUT_TIME 1000

/* Reply callback for auth command HELLOACL.AUTHASYNC */
int HelloACL_Reply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    size_t length;

    SiderModuleString *user_string = SiderModule_GetBlockedClientPrivateData(ctx);
    const char *name = SiderModule_StringPtrLen(user_string, &length);

    if (SiderModule_AuthenticateClientWithACLUser(ctx, name, length, NULL, NULL, NULL) == 
            REDISMODULE_ERR) {
        return SiderModule_ReplyWithError(ctx, "Invalid Username or password");    
    }
    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* Timeout callback for auth command HELLOACL.AUTHASYNC */
int HelloACL_Timeout(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    return SiderModule_ReplyWithSimpleString(ctx, "Request timedout");
}

/* Private data frees data for HELLOACL.AUTHASYNC command. */
void HelloACL_FreeData(SiderModuleCtx *ctx, void *privdata) {
    REDISMODULE_NOT_USED(ctx);
    SiderModule_FreeString(NULL, privdata);
}

/* Background authentication can happen here. */
void *HelloACL_ThreadMain(void *args) {
    void **targs = args;
    SiderModuleBlockedClient *bc = targs[0];
    SiderModuleString *user = targs[1];
    SiderModule_Free(targs);

    SiderModule_UnblockClient(bc,user);
    return NULL;
}

/* HELLOACL.AUTHASYNC 
 * Asynchronously assigns an ACL user to the current context. */
int AuthAsyncCommand_SiderCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 2) return SiderModule_WrongArity(ctx);

    pthread_t tid;
    SiderModuleBlockedClient *bc = SiderModule_BlockClient(ctx, HelloACL_Reply, HelloACL_Timeout, HelloACL_FreeData, TIMEOUT_TIME);
    

    void **targs = SiderModule_Alloc(sizeof(void*)*2);
    targs[0] = bc;
    targs[1] = SiderModule_CreateStringFromString(NULL, argv[1]);

    if (pthread_create(&tid, NULL, HelloACL_ThreadMain, targs) != 0) {
        SiderModule_AbortBlock(bc);
        return SiderModule_ReplyWithError(ctx, "-ERR Can't start thread");
    }

    return REDISMODULE_OK;
}

/* This function must be present on each Sider module. It is used in order to
 * register the commands into the Sider server. */
int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"helloacl",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"helloacl.reset",
        ResetCommand_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"helloacl.revoke",
        RevokeCommand_SiderCommand,"",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"helloacl.authglobal",
        AuthGlobalCommand_SiderCommand,"no-auth",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"helloacl.authasync",
        AuthAsyncCommand_SiderCommand,"no-auth",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    global = SiderModule_CreateModuleUser("global");
    SiderModule_SetModuleUserACL(global, "allcommands");
    SiderModule_SetModuleUserACL(global, "allkeys");
    SiderModule_SetModuleUserACL(global, "on");

    global_auth_client_id = 0;

    return REDISMODULE_OK;
}
