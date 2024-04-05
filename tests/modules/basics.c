/* Module designed to test the Sider modules subsystem.
 *
 * -----------------------------------------------------------------------------
 *
 * Copyright (c) 2016, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include "sidermodule.h"
#include <string.h>
#include <stdlib.h>

/* --------------------------------- Helpers -------------------------------- */

/* Return true if the reply and the C null term string matches. */
int TestMatchReply(SiderModuleCallReply *reply, char *str) {
    SiderModuleString *mystr;
    mystr = SiderModule_CreateStringFromCallReply(reply);
    if (!mystr) return 0;
    const char *ptr = SiderModule_StringPtrLen(mystr,NULL);
    return strcmp(ptr,str) == 0;
}

/* ------------------------------- Test units ------------------------------- */

/* TEST.CALL -- Test Call() API. */
int TestCall(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","mylist");
    SiderModuleString *mystr = SiderModule_CreateString(ctx,"foo",3);
    SiderModule_Call(ctx,"RPUSH","csl","mylist",mystr,(long long)1234);
    reply = SiderModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    long long items = SiderModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    SiderModuleCallReply *item0, *item1;

    item0 = SiderModule_CallReplyArrayElement(reply,0);
    item1 = SiderModule_CallReplyArrayElement(reply,1);
    if (!TestMatchReply(item0,"foo")) goto fail;
    if (!TestMatchReply(item1,"1234")) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Attribute(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "attrib"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_STRING) goto fail;

    /* make sure we can not reply to resp2 client with resp3 (it might be a string but it contains attribute) */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    if (!TestMatchReply(reply,"Some real reply following the attribute")) goto fail;

    reply = SiderModule_CallReplyAttribute(reply);
    if (!reply || SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_ATTRIBUTE) goto fail;
    /* make sure we can not reply to resp2 client with resp3 attribute */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;
    if (SiderModule_CallReplyLength(reply) != 1) goto fail;

    SiderModuleCallReply *key, *val;
    if (SiderModule_CallReplyAttributeElement(reply,0,&key,&val) != REDISMODULE_OK) goto fail;
    if (!TestMatchReply(key,"key-popularity")) goto fail;
    if (SiderModule_CallReplyType(val) != REDISMODULE_REPLY_ARRAY) goto fail;
    if (SiderModule_CallReplyLength(val) != 2) goto fail;
    if (!TestMatchReply(SiderModule_CallReplyArrayElement(val, 0),"key:123")) goto fail;
    if (!TestMatchReply(SiderModule_CallReplyArrayElement(val, 1),"90")) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestGetResp(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    int flags = SiderModule_GetContextFlags(ctx);

    if (flags & REDISMODULE_CTX_FLAGS_RESP3) {
        SiderModule_ReplyWithLongLong(ctx, 3);
    } else {
        SiderModule_ReplyWithLongLong(ctx, 2);
    }

    return REDISMODULE_OK;
}

int TestCallRespAutoMode(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","myhash");
    SiderModule_Call(ctx,"HSET","ccccc","myhash", "f1", "v1", "f2", "v2");
    /* 0 stands for auto mode, we will get the reply in the same format as the client */
    reply = SiderModule_Call(ctx,"HGETALL","0c" ,"myhash");
    SiderModule_ReplyWithCallReply(ctx, reply);
    return REDISMODULE_OK;
}

int TestCallResp3Map(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","myhash");
    SiderModule_Call(ctx,"HSET","ccccc","myhash", "f1", "v1", "f2", "v2");
    reply = SiderModule_Call(ctx,"HGETALL","3c" ,"myhash"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_MAP) goto fail;

    /* make sure we can not reply to resp2 client with resp3 map */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    long long items = SiderModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    SiderModuleCallReply *key0, *key1;
    SiderModuleCallReply *val0, *val1;
    if (SiderModule_CallReplyMapElement(reply,0,&key0,&val0) != REDISMODULE_OK) goto fail;
    if (SiderModule_CallReplyMapElement(reply,1,&key1,&val1) != REDISMODULE_OK) goto fail;
    if (!TestMatchReply(key0,"f1")) goto fail;
    if (!TestMatchReply(key1,"f2")) goto fail;
    if (!TestMatchReply(val0,"v1")) goto fail;
    if (!TestMatchReply(val1,"v2")) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Bool(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "true"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_BOOL) goto fail;
    /* make sure we can not reply to resp2 client with resp3 bool */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    if (!SiderModule_CallReplyBool(reply)) goto fail;
    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "false"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_BOOL) goto fail;
    if (SiderModule_CallReplyBool(reply)) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Null(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "null"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_NULL) goto fail;

    /* make sure we can not reply to resp2 client with resp3 null */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallReplyWithNestedReply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","mylist");
    SiderModule_Call(ctx,"RPUSH","ccl","mylist","test",(long long)1234);
    reply = SiderModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_ARRAY) goto fail;
    if (SiderModule_CallReplyLength(reply) < 1) goto fail;
    SiderModuleCallReply *nestedReply = SiderModule_CallReplyArrayElement(reply, 0);

    SiderModule_ReplyWithCallReply(ctx,nestedReply);
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallReplyWithArrayReply(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","mylist");
    SiderModule_Call(ctx,"RPUSH","ccl","mylist","test",(long long)1234);
    reply = SiderModule_Call(ctx,"LRANGE","ccc","mylist","0","-1");
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_ARRAY) goto fail;

    SiderModule_ReplyWithCallReply(ctx,reply);
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Double(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "double"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_DOUBLE) goto fail;

    /* make sure we can not reply to resp2 client with resp3 double*/
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    double d = SiderModule_CallReplyDouble(reply);
    /* we compare strings, since comparing doubles directly can fail in various architectures, e.g. 32bit */
    char got[30], expected[30];
    snprintf(got, sizeof(got), "%.17g", d);
    snprintf(expected, sizeof(expected), "%.17g", 3.141);
    if (strcmp(got, expected) != 0) goto fail;
    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3BigNumber(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "bignum"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_BIG_NUMBER) goto fail;

    /* make sure we can not reply to resp2 client with resp3 big number */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    size_t len;
    const char* big_num = SiderModule_CallReplyBigNumber(reply, &len);
    SiderModule_ReplyWithStringBuffer(ctx,big_num,len);
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Verbatim(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    reply = SiderModule_Call(ctx,"DEBUG","3cc" ,"PROTOCOL", "verbatim"); /* 3 stands for resp 3 reply */
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_VERBATIM_STRING) goto fail;

    /* make sure we can not reply to resp2 client with resp3 verbatim string */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    const char* format;
    size_t len;
    const char* str = SiderModule_CallReplyVerbatim(reply, &len, &format);
    SiderModuleString *s = SiderModule_CreateStringPrintf(ctx, "%.*s:%.*s", 3, format, (int)len, str);
    SiderModule_ReplyWithString(ctx,s);
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

int TestCallResp3Set(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    SiderModule_Call(ctx,"DEL","c","myset");
    SiderModule_Call(ctx,"sadd","ccc","myset", "v1", "v2");
    reply = SiderModule_Call(ctx,"smembers","3c" ,"myset"); // N stands for resp 3 reply
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_SET) goto fail;

    /* make sure we can not reply to resp2 client with resp3 set */
    if (SiderModule_ReplyWithCallReply(ctx, reply) != REDISMODULE_ERR) goto fail;

    long long items = SiderModule_CallReplyLength(reply);
    if (items != 2) goto fail;

    SiderModuleCallReply *val0, *val1;

    val0 = SiderModule_CallReplySetElement(reply,0);
    val1 = SiderModule_CallReplySetElement(reply,1);

    /*
     * The order of elements on sets are not promised so we just
     * veridy that the reply matches one of the elements.
     */
    if (!TestMatchReply(val0,"v1") && !TestMatchReply(val0,"v2")) goto fail;
    if (!TestMatchReply(val1,"v1") && !TestMatchReply(val1,"v2")) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"OK");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,"ERR");
    return REDISMODULE_OK;
}

/* TEST.STRING.APPEND -- Test appending to an existing string object. */
int TestStringAppend(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleString *s = SiderModule_CreateString(ctx,"foo",3);
    SiderModule_StringAppendBuffer(ctx,s,"bar",3);
    SiderModule_ReplyWithString(ctx,s);
    SiderModule_FreeString(ctx,s);
    return REDISMODULE_OK;
}

/* TEST.STRING.APPEND.AM -- Test append with retain when auto memory is on. */
int TestStringAppendAM(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleString *s = SiderModule_CreateString(ctx,"foo",3);
    SiderModule_RetainString(ctx,s);
    SiderModule_TrimStringAllocation(s);    /* Mostly NOP, but exercises the API function */
    SiderModule_StringAppendBuffer(ctx,s,"bar",3);
    SiderModule_ReplyWithString(ctx,s);
    SiderModule_FreeString(ctx,s);
    return REDISMODULE_OK;
}

/* TEST.STRING.TRIM -- Test we trim a string with free space. */
int TestTrimString(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    SiderModuleString *s = SiderModule_CreateString(ctx,"foo",3);
    char *tmp = SiderModule_Alloc(1024);
    SiderModule_StringAppendBuffer(ctx,s,tmp,1024);
    size_t string_len = SiderModule_MallocSizeString(s);
    SiderModule_TrimStringAllocation(s);
    size_t len_after_trim = SiderModule_MallocSizeString(s);

    /* Determine if using jemalloc memory allocator. */
    SiderModuleServerInfoData *info = SiderModule_GetServerInfo(ctx, "memory");
    const char *field = SiderModule_ServerInfoGetFieldC(info, "mem_allocator");
    int use_jemalloc = !strncmp(field, "jemalloc", 8);

    /* Jemalloc will reallocate `s` from 2k to 1k after SiderModule_TrimStringAllocation(),
     * but non-jemalloc memory allocators may keep the old size. */
    if ((use_jemalloc && len_after_trim < string_len) ||
        (!use_jemalloc && len_after_trim <= string_len))
    {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        SiderModule_ReplyWithError(ctx, "String was not trimmed as expected.");
    }
    SiderModule_FreeServerInfo(ctx, info);
    SiderModule_Free(tmp);
    SiderModule_FreeString(ctx,s);
    return REDISMODULE_OK;
}

/* TEST.STRING.PRINTF -- Test string formatting. */
int TestStringPrintf(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);
    if (argc < 3) {
        return SiderModule_WrongArity(ctx);
    }
    SiderModuleString *s = SiderModule_CreateStringPrintf(ctx,
        "Got %d args. argv[1]: %s, argv[2]: %s",
        argc,
        SiderModule_StringPtrLen(argv[1], NULL),
        SiderModule_StringPtrLen(argv[2], NULL)
    );

    SiderModule_ReplyWithString(ctx,s);

    return REDISMODULE_OK;
}

int failTest(SiderModuleCtx *ctx, const char *msg) {
    SiderModule_ReplyWithError(ctx, msg);
    return REDISMODULE_ERR;
}

int TestUnlink(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleKey *k = SiderModule_OpenKey(ctx, SiderModule_CreateStringPrintf(ctx, "unlinked"), REDISMODULE_WRITE | REDISMODULE_READ);
    if (!k) return failTest(ctx, "Could not create key");

    if (REDISMODULE_ERR == SiderModule_StringSet(k, SiderModule_CreateStringPrintf(ctx, "Foobar"))) {
        return failTest(ctx, "Could not set string value");
    }

    SiderModuleCallReply *rep = SiderModule_Call(ctx, "EXISTS", "c", "unlinked");
    if (!rep || SiderModule_CallReplyInteger(rep) != 1) {
        return failTest(ctx, "Key does not exist before unlink");
    }

    if (REDISMODULE_ERR == SiderModule_UnlinkKey(k)) {
        return failTest(ctx, "Could not unlink key");
    }

    rep = SiderModule_Call(ctx, "EXISTS", "c", "unlinked");
    if (!rep || SiderModule_CallReplyInteger(rep) != 0) {
        return failTest(ctx, "Could not verify key to be unlinked");
    }
    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

int TestNestedCallReplyArrayElement(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModuleString *expect_key = SiderModule_CreateString(ctx, "mykey", strlen("mykey"));
    SiderModule_SelectDb(ctx, 1);
    SiderModule_Call(ctx, "LPUSH", "sc", expect_key, "myvalue");

    SiderModuleCallReply *scan_reply = SiderModule_Call(ctx, "SCAN", "l", (long long)0);
    SiderModule_Assert(scan_reply != NULL && SiderModule_CallReplyType(scan_reply) == REDISMODULE_REPLY_ARRAY);
    SiderModule_Assert(SiderModule_CallReplyLength(scan_reply) == 2);

    long long scan_cursor;
    SiderModuleCallReply *cursor_reply = SiderModule_CallReplyArrayElement(scan_reply, 0);
    SiderModule_Assert(SiderModule_CallReplyType(cursor_reply) == REDISMODULE_REPLY_STRING);
    SiderModule_Assert(SiderModule_StringToLongLong(SiderModule_CreateStringFromCallReply(cursor_reply), &scan_cursor) == REDISMODULE_OK);
    SiderModule_Assert(scan_cursor == 0);

    SiderModuleCallReply *keys_reply = SiderModule_CallReplyArrayElement(scan_reply, 1);
    SiderModule_Assert(SiderModule_CallReplyType(keys_reply) == REDISMODULE_REPLY_ARRAY);
    SiderModule_Assert( SiderModule_CallReplyLength(keys_reply) == 1);
 
    SiderModuleCallReply *key_reply = SiderModule_CallReplyArrayElement(keys_reply, 0);
    SiderModule_Assert(SiderModule_CallReplyType(key_reply) == REDISMODULE_REPLY_STRING);
    SiderModuleString *key = SiderModule_CreateStringFromCallReply(key_reply);
    SiderModule_Assert(SiderModule_StringCompare(key, expect_key) == 0);

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/* TEST.STRING.TRUNCATE -- Test truncating an existing string object. */
int TestStringTruncate(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_Call(ctx, "SET", "cc", "foo", "abcde");
    SiderModuleKey *k = SiderModule_OpenKey(ctx, SiderModule_CreateStringPrintf(ctx, "foo"), REDISMODULE_READ | REDISMODULE_WRITE);
    if (!k) return failTest(ctx, "Could not create key");

    size_t len = 0;
    char* s;

    /* expand from 5 to 8 and check null pad */
    if (REDISMODULE_ERR == SiderModule_StringTruncate(k, 8)) {
        return failTest(ctx, "Could not truncate string value (8)");
    }
    s = SiderModule_StringDMA(k, &len, REDISMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (8)");
    } else if (len != 8) {
        return failTest(ctx, "Failed to expand string value (8)");
    } else if (0 != strncmp(s, "abcde\0\0\0", 8)) {
        return failTest(ctx, "Failed to null pad string value (8)");
    }

    /* shrink from 8 to 4 */
    if (REDISMODULE_ERR == SiderModule_StringTruncate(k, 4)) {
        return failTest(ctx, "Could not truncate string value (4)");
    }
    s = SiderModule_StringDMA(k, &len, REDISMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (4)");
    } else if (len != 4) {
        return failTest(ctx, "Failed to shrink string value (4)");
    } else if (0 != strncmp(s, "abcd", 4)) {
        return failTest(ctx, "Failed to truncate string value (4)");
    }

    /* shrink to 0 */
    if (REDISMODULE_ERR == SiderModule_StringTruncate(k, 0)) {
        return failTest(ctx, "Could not truncate string value (0)");
    }
    s = SiderModule_StringDMA(k, &len, REDISMODULE_READ);
    if (!s) {
        return failTest(ctx, "Failed to read truncated string (0)");
    } else if (len != 0) {
        return failTest(ctx, "Failed to shrink string value to (0)");
    }

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

int NotifyCallback(SiderModuleCtx *ctx, int type, const char *event,
                   SiderModuleString *key) {
  SiderModule_AutoMemory(ctx);
  /* Increment a counter on the notifications: for each key notified we
   * increment a counter */
  SiderModule_Log(ctx, "notice", "Got event type %d, event %s, key %s", type,
                  event, SiderModule_StringPtrLen(key, NULL));

  SiderModule_Call(ctx, "HINCRBY", "csc", "notifications", key, "1");
  return REDISMODULE_OK;
}

/* TEST.NOTIFICATIONS -- Test Keyspace Notifications. */
int TestNotifications(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    SiderModule_AutoMemory(ctx);
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

#define FAIL(msg, ...)                                                                       \
    {                                                                                        \
        SiderModule_Log(ctx, "warning", "Failed NOTIFY Test. Reason: " #msg, ##__VA_ARGS__); \
        goto err;                                                                            \
    }
    SiderModule_Call(ctx, "FLUSHDB", "");

    SiderModule_Call(ctx, "SET", "cc", "foo", "bar");
    SiderModule_Call(ctx, "SET", "cc", "foo", "baz");
    SiderModule_Call(ctx, "SADD", "cc", "bar", "x");
    SiderModule_Call(ctx, "SADD", "cc", "bar", "y");

    SiderModule_Call(ctx, "HSET", "ccc", "baz", "x", "y");
    /* LPUSH should be ignored and not increment any counters */
    SiderModule_Call(ctx, "LPUSH", "cc", "l", "y");
    SiderModule_Call(ctx, "LPUSH", "cc", "l", "y");

    /* Miss some keys intentionally so we will get a "keymiss" notification. */
    SiderModule_Call(ctx, "GET", "c", "nosuchkey");
    SiderModule_Call(ctx, "SMEMBERS", "c", "nosuchkey");

    size_t sz;
    const char *rep;
    SiderModuleCallReply *r = SiderModule_Call(ctx, "HGET", "cc", "notifications", "foo");
    if (r == NULL || SiderModule_CallReplyType(r) != REDISMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for foo");
    } else {
        rep = SiderModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%s'. expected '2'", SiderModule_CallReplyStringPtr(r, NULL));
        }
    }

    r = SiderModule_Call(ctx, "HGET", "cc", "notifications", "bar");
    if (r == NULL || SiderModule_CallReplyType(r) != REDISMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for bar");
    } else {
        rep = SiderModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%s'. expected '2'", rep);
        }
    }

    r = SiderModule_Call(ctx, "HGET", "cc", "notifications", "baz");
    if (r == NULL || SiderModule_CallReplyType(r) != REDISMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for baz");
    } else {
        rep = SiderModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '1') {
            FAIL("Got reply '%.*s'. expected '1'", (int)sz, rep);
        }
    }
    /* For l we expect nothing since we didn't subscribe to list events */
    r = SiderModule_Call(ctx, "HGET", "cc", "notifications", "l");
    if (r == NULL || SiderModule_CallReplyType(r) != REDISMODULE_REPLY_NULL) {
        FAIL("Wrong reply for l");
    }

    r = SiderModule_Call(ctx, "HGET", "cc", "notifications", "nosuchkey");
    if (r == NULL || SiderModule_CallReplyType(r) != REDISMODULE_REPLY_STRING) {
        FAIL("Wrong or no reply for nosuchkey");
    } else {
        rep = SiderModule_CallReplyStringPtr(r, &sz);
        if (sz != 1 || *rep != '2') {
            FAIL("Got reply '%.*s'. expected '2'", (int)sz, rep);
        }
    }

    SiderModule_Call(ctx, "FLUSHDB", "");

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
err:
    SiderModule_Call(ctx, "FLUSHDB", "");

    return SiderModule_ReplyWithSimpleString(ctx, "ERR");
}

/* TEST.CTXFLAGS -- Test GetContextFlags. */
int TestCtxFlags(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argc);
    REDISMODULE_NOT_USED(argv);

    SiderModule_AutoMemory(ctx);

    int ok = 1;
    const char *errString = NULL;
#undef FAIL
#define FAIL(msg)        \
    {                    \
        ok = 0;          \
        errString = msg; \
        goto end;        \
    }

    int flags = SiderModule_GetContextFlags(ctx);
    if (flags == 0) {
        FAIL("Got no flags");
    }

    if (flags & REDISMODULE_CTX_FLAGS_LUA) FAIL("Lua flag was set");
    if (flags & REDISMODULE_CTX_FLAGS_MULTI) FAIL("Multi flag was set");

    if (flags & REDISMODULE_CTX_FLAGS_AOF) FAIL("AOF Flag was set")
    /* Enable AOF to test AOF flags */
    SiderModule_Call(ctx, "config", "ccc", "set", "appendonly", "yes");
    flags = SiderModule_GetContextFlags(ctx);
    if (!(flags & REDISMODULE_CTX_FLAGS_AOF)) FAIL("AOF Flag not set after config set");

    /* Disable RDB saving and test the flag. */
    SiderModule_Call(ctx, "config", "ccc", "set", "save", "");
    flags = SiderModule_GetContextFlags(ctx);
    if (flags & REDISMODULE_CTX_FLAGS_RDB) FAIL("RDB Flag was set");
    /* Enable RDB to test RDB flags */
    SiderModule_Call(ctx, "config", "ccc", "set", "save", "900 1");
    flags = SiderModule_GetContextFlags(ctx);
    if (!(flags & REDISMODULE_CTX_FLAGS_RDB)) FAIL("RDB Flag was not set after config set");

    if (!(flags & REDISMODULE_CTX_FLAGS_MASTER)) FAIL("Master flag was not set");
    if (flags & REDISMODULE_CTX_FLAGS_SLAVE) FAIL("Slave flag was set");
    if (flags & REDISMODULE_CTX_FLAGS_READONLY) FAIL("Read-only flag was set");
    if (flags & REDISMODULE_CTX_FLAGS_CLUSTER) FAIL("Cluster flag was set");

    /* Disable maxmemory and test the flag. (it is implicitly set in 32bit builds. */
    SiderModule_Call(ctx, "config", "ccc", "set", "maxmemory", "0");
    flags = SiderModule_GetContextFlags(ctx);
    if (flags & REDISMODULE_CTX_FLAGS_MAXMEMORY) FAIL("Maxmemory flag was set");

    /* Enable maxmemory and test the flag. */
    SiderModule_Call(ctx, "config", "ccc", "set", "maxmemory", "100000000");
    flags = SiderModule_GetContextFlags(ctx);
    if (!(flags & REDISMODULE_CTX_FLAGS_MAXMEMORY))
        FAIL("Maxmemory flag was not set after config set");

    if (flags & REDISMODULE_CTX_FLAGS_EVICT) FAIL("Eviction flag was set");
    SiderModule_Call(ctx, "config", "ccc", "set", "maxmemory-policy", "allkeys-lru");
    flags = SiderModule_GetContextFlags(ctx);
    if (!(flags & REDISMODULE_CTX_FLAGS_EVICT)) FAIL("Eviction flag was not set after config set");

end:
    /* Revert config changes */
    SiderModule_Call(ctx, "config", "ccc", "set", "appendonly", "no");
    SiderModule_Call(ctx, "config", "ccc", "set", "save", "");
    SiderModule_Call(ctx, "config", "ccc", "set", "maxmemory", "0");
    SiderModule_Call(ctx, "config", "ccc", "set", "maxmemory-policy", "noeviction");

    if (!ok) {
        SiderModule_Log(ctx, "warning", "Failed CTXFLAGS Test. Reason: %s", errString);
        return SiderModule_ReplyWithSimpleString(ctx, "ERR");
    }

    return SiderModule_ReplyWithSimpleString(ctx, "OK");
}

/* ----------------------------- Test framework ----------------------------- */

/* Return 1 if the reply matches the specified string, otherwise log errors
 * in the server log and return 0. */
int TestAssertErrorReply(SiderModuleCtx *ctx, SiderModuleCallReply *reply, char *str, size_t len) {
    SiderModuleString *mystr, *expected;
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_ERROR) {
        return 0;
    }

    mystr = SiderModule_CreateStringFromCallReply(reply);
    expected = SiderModule_CreateString(ctx,str,len);
    if (SiderModule_StringCompare(mystr,expected) != 0) {
        const char *mystr_ptr = SiderModule_StringPtrLen(mystr,NULL);
        const char *expected_ptr = SiderModule_StringPtrLen(expected,NULL);
        SiderModule_Log(ctx,"warning",
            "Unexpected Error reply reply '%s' (instead of '%s')",
            mystr_ptr, expected_ptr);
        return 0;
    }
    return 1;
}

int TestAssertStringReply(SiderModuleCtx *ctx, SiderModuleCallReply *reply, char *str, size_t len) {
    SiderModuleString *mystr, *expected;

    if (SiderModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR) {
        SiderModule_Log(ctx,"warning","Test error reply: %s",
            SiderModule_CallReplyStringPtr(reply, NULL));
        return 0;
    } else if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_STRING) {
        SiderModule_Log(ctx,"warning","Unexpected reply type %d",
            SiderModule_CallReplyType(reply));
        return 0;
    }
    mystr = SiderModule_CreateStringFromCallReply(reply);
    expected = SiderModule_CreateString(ctx,str,len);
    if (SiderModule_StringCompare(mystr,expected) != 0) {
        const char *mystr_ptr = SiderModule_StringPtrLen(mystr,NULL);
        const char *expected_ptr = SiderModule_StringPtrLen(expected,NULL);
        SiderModule_Log(ctx,"warning",
            "Unexpected string reply '%s' (instead of '%s')",
            mystr_ptr, expected_ptr);
        return 0;
    }
    return 1;
}

/* Return 1 if the reply matches the specified integer, otherwise log errors
 * in the server log and return 0. */
int TestAssertIntegerReply(SiderModuleCtx *ctx, SiderModuleCallReply *reply, long long expected) {
    if (SiderModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR) {
        SiderModule_Log(ctx,"warning","Test error reply: %s",
            SiderModule_CallReplyStringPtr(reply, NULL));
        return 0;
    } else if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_INTEGER) {
        SiderModule_Log(ctx,"warning","Unexpected reply type %d",
            SiderModule_CallReplyType(reply));
        return 0;
    }
    long long val = SiderModule_CallReplyInteger(reply);
    if (val != expected) {
        SiderModule_Log(ctx,"warning",
            "Unexpected integer reply '%lld' (instead of '%lld')",
            val, expected);
        return 0;
    }
    return 1;
}

#define T(name,...) \
    do { \
        SiderModule_Log(ctx,"warning","Testing %s", name); \
        reply = SiderModule_Call(ctx,name,__VA_ARGS__); \
    } while (0)

/* TEST.BASICS -- Run all the tests.
 * Note: it is useful to run these tests from the module rather than TCL
 * since it's easier to check the reply types like that (make a distinction
 * between 0 and "0", etc. */
int TestBasics(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    SiderModule_AutoMemory(ctx);
    SiderModuleCallReply *reply;

    /* Make sure the DB is empty before to proceed. */
    T("dbsize","");
    if (!TestAssertIntegerReply(ctx,reply,0)) goto fail;

    T("ping","");
    if (!TestAssertStringReply(ctx,reply,"PONG",4)) goto fail;

    T("test.call","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3map","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3set","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3double","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3bool","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callresp3null","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callreplywithnestedreply","");
    if (!TestAssertStringReply(ctx,reply,"test",4)) goto fail;

    T("test.callreplywithbignumberreply","");
    if (!TestAssertStringReply(ctx,reply,"1234567999999999999999999999999999999",37)) goto fail;

    T("test.callreplywithverbatimstringreply","");
    if (!TestAssertStringReply(ctx,reply,"txt:This is a verbatim\nstring",29)) goto fail;

    T("test.ctxflags","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.append","");
    if (!TestAssertStringReply(ctx,reply,"foobar",6)) goto fail;

    T("test.string.truncate","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.unlink","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.nestedcallreplyarray","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.append.am","");
    if (!TestAssertStringReply(ctx,reply,"foobar",6)) goto fail;
    
    T("test.string.trim","");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.string.printf", "cc", "foo", "bar");
    if (!TestAssertStringReply(ctx,reply,"Got 3 args. argv[1]: foo, argv[2]: bar",38)) goto fail;

    T("test.notify", "");
    if (!TestAssertStringReply(ctx,reply,"OK",2)) goto fail;

    T("test.callreplywitharrayreply", "");
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_ARRAY) goto fail;
    if (SiderModule_CallReplyLength(reply) != 2) goto fail;
    if (!TestAssertStringReply(ctx,SiderModule_CallReplyArrayElement(reply, 0),"test",4)) goto fail;
    if (!TestAssertStringReply(ctx,SiderModule_CallReplyArrayElement(reply, 1),"1234",4)) goto fail;

    T("foo", "E");
    if (!TestAssertErrorReply(ctx,reply,"ERR unknown command 'foo', with args beginning with: ",53)) goto fail;

    T("set", "Ec", "x");
    if (!TestAssertErrorReply(ctx,reply,"ERR wrong number of arguments for 'set' command",47)) goto fail;

    T("shutdown", "SE");
    if (!TestAssertErrorReply(ctx,reply,"ERR command 'shutdown' is not allowed on script mode",52)) goto fail;

    T("set", "WEcc", "x", "1");
    if (!TestAssertErrorReply(ctx,reply,"ERR Write command 'set' was called while write is not allowed.",62)) goto fail;

    SiderModule_ReplyWithSimpleString(ctx,"ALL TESTS PASSED");
    return REDISMODULE_OK;

fail:
    SiderModule_ReplyWithSimpleString(ctx,
        "SOME TEST DID NOT PASS! Check server logs");
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx,"test",1,REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    /* Perform RM_Call inside the SiderModule_OnLoad
     * to verify that it works as expected without crashing.
     * The tests will verify it on different configurations
     * options (cluster/no cluster). A simple ping command
     * is enough for this test. */
    SiderModuleCallReply *reply = SiderModule_Call(ctx, "ping", "");
    if (SiderModule_CallReplyType(reply) != REDISMODULE_REPLY_STRING) {
        SiderModule_FreeCallReply(reply);
        return REDISMODULE_ERR;
    }
    size_t len;
    const char *reply_str = SiderModule_CallReplyStringPtr(reply, &len);
    if (len != 4) {
        SiderModule_FreeCallReply(reply);
        return REDISMODULE_ERR;
    }
    if (memcmp(reply_str, "PONG", 4) != 0) {
        SiderModule_FreeCallReply(reply);
        return REDISMODULE_ERR;
    }
    SiderModule_FreeCallReply(reply);

    if (SiderModule_CreateCommand(ctx,"test.call",
        TestCall,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3map",
        TestCallResp3Map,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3attribute",
        TestCallResp3Attribute,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3set",
        TestCallResp3Set,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3double",
        TestCallResp3Double,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3bool",
        TestCallResp3Bool,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callresp3null",
        TestCallResp3Null,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callreplywitharrayreply",
        TestCallReplyWithArrayReply,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callreplywithnestedreply",
        TestCallReplyWithNestedReply,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callreplywithbignumberreply",
        TestCallResp3BigNumber,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.callreplywithverbatimstringreply",
        TestCallResp3Verbatim,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.string.append",
        TestStringAppend,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.string.trim",
        TestTrimString,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.string.append.am",
        TestStringAppendAM,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.string.truncate",
        TestStringTruncate,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.string.printf",
        TestStringPrintf,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.ctxflags",
        TestCtxFlags,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.unlink",
        TestUnlink,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.nestedcallreplyarray",
        TestNestedCallReplyArrayElement,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.basics",
        TestBasics,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* the following commands are used by an external test and should not be added to TestBasics */
    if (SiderModule_CreateCommand(ctx,"test.rmcallautomode",
        TestCallRespAutoMode,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx,"test.getresp",
        TestGetResp,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModule_SubscribeToKeyspaceEvents(ctx,
                                            REDISMODULE_NOTIFY_HASH |
                                            REDISMODULE_NOTIFY_SET |
                                            REDISMODULE_NOTIFY_STRING |
                                            REDISMODULE_NOTIFY_KEY_MISS,
                                        NotifyCallback);
    if (SiderModule_CreateCommand(ctx,"test.notify",
        TestNotifications,"write deny-oom",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
