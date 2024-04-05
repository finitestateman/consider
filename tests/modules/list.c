#include "sidermodule.h"
#include <assert.h>
#include <errno.h>
#include <strings.h>

/* LIST.GETALL key [REVERSE] */
int list_getall(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 2 || argc > 3) return SiderModule_WrongArity(ctx);
    int reverse = (argc == 3 &&
                   !strcasecmp(SiderModule_StringPtrLen(argv[2], NULL),
                               "REVERSE"));
    SiderModule_AutoMemory(ctx);
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    if (SiderModule_KeyType(key) != REDISMODULE_KEYTYPE_LIST) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }
    long n = SiderModule_ValueLength(key);
    SiderModule_ReplyWithArray(ctx, n);
    if (!reverse) {
        for (long i = 0; i < n; i++) {
            SiderModuleString *elem = SiderModule_ListGet(key, i);
            SiderModule_ReplyWithString(ctx, elem);
            SiderModule_FreeString(ctx, elem);
        }
    } else {
        for (long i = -1; i >= -n; i--) {
            SiderModuleString *elem = SiderModule_ListGet(key, i);
            SiderModule_ReplyWithString(ctx, elem);
            SiderModule_FreeString(ctx, elem);
        }
    }

    /* Test error condition: index out of bounds */
    assert(SiderModule_ListGet(key, n) == NULL);
    assert(errno == EDOM); /* no more elements in list */

    /* SiderModule_CloseKey(key); //implicit, done by auto memory */
    return REDISMODULE_OK;
}

/* LIST.EDIT key [REVERSE] cmdstr [value ..]
 *
 * cmdstr is a string of the following characters:
 *
 *     k -- keep
 *     d -- delete
 *     i -- insert value from args
 *     r -- replace with value from args
 *
 * The number of occurrences of "i" and "r" in cmdstr) should correspond to the
 * number of args after cmdstr.
 *
 * Reply with a RESP3 Map, containing the number of edits (inserts, replaces, deletes)
 * performed, as well as the last index and the entry it points to.
 */
int list_edit(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc < 3) return SiderModule_WrongArity(ctx);
    SiderModule_AutoMemory(ctx);
    int argpos = 1; /* the next arg */

    /* key */
    int keymode = REDISMODULE_READ | REDISMODULE_WRITE;
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[argpos++], keymode);
    if (SiderModule_KeyType(key) != REDISMODULE_KEYTYPE_LIST) {
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    /* REVERSE */
    int reverse = 0;
    if (argc >= 4 &&
        !strcasecmp(SiderModule_StringPtrLen(argv[argpos], NULL), "REVERSE")) {
        reverse = 1;
        argpos++;
    }

    /* cmdstr */
    size_t cmdstr_len;
    const char *cmdstr = SiderModule_StringPtrLen(argv[argpos++], &cmdstr_len);

    /* validate cmdstr vs. argc */
    long num_req_args = 0;
    long min_list_length = 0;
    for (size_t cmdpos = 0; cmdpos < cmdstr_len; cmdpos++) {
        char c = cmdstr[cmdpos];
        if (c == 'i' || c == 'r') num_req_args++;
        if (c == 'd' || c == 'r' || c == 'k') min_list_length++;
    }
    if (argc < argpos + num_req_args) {
        return SiderModule_ReplyWithError(ctx, "ERR too few args");
    }
    if ((long)SiderModule_ValueLength(key) < min_list_length) {
        return SiderModule_ReplyWithError(ctx, "ERR list too short");
    }

    /* Iterate over the chars in cmdstr (edit instructions) */
    long long num_inserts = 0, num_deletes = 0, num_replaces = 0;
    long index = reverse ? -1 : 0;
    SiderModuleString *value;

    for (size_t cmdpos = 0; cmdpos < cmdstr_len; cmdpos++) {
        switch (cmdstr[cmdpos]) {
        case 'i': /* insert */
            value = argv[argpos++];
            assert(SiderModule_ListInsert(key, index, value) == REDISMODULE_OK);
            index += reverse ? -1 : 1;
            num_inserts++;
            break;
        case 'd': /* delete */
            assert(SiderModule_ListDelete(key, index) == REDISMODULE_OK);
            num_deletes++;
            break;
        case 'r': /* replace */
            value = argv[argpos++];
            assert(SiderModule_ListSet(key, index, value) == REDISMODULE_OK);
            index += reverse ? -1 : 1;
            num_replaces++;
            break;
        case 'k': /* keep */
            index += reverse ? -1 : 1;
            break;
        }
    }

    SiderModuleString *v = SiderModule_ListGet(key, index);
    SiderModule_ReplyWithMap(ctx, v ? 5 : 4);
    SiderModule_ReplyWithCString(ctx, "i");
    SiderModule_ReplyWithLongLong(ctx, num_inserts);
    SiderModule_ReplyWithCString(ctx, "d");
    SiderModule_ReplyWithLongLong(ctx, num_deletes);
    SiderModule_ReplyWithCString(ctx, "r");
    SiderModule_ReplyWithLongLong(ctx, num_replaces);
    SiderModule_ReplyWithCString(ctx, "index");
    SiderModule_ReplyWithLongLong(ctx, index);
    if (v) {
        SiderModule_ReplyWithCString(ctx, "entry");
        SiderModule_ReplyWithString(ctx, v);
        SiderModule_FreeString(ctx, v);
    } 

    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

/* Reply based on errno as set by the List API functions. */
static int replyByErrno(SiderModuleCtx *ctx) {
    switch (errno) {
    case EDOM:
        return SiderModule_ReplyWithError(ctx, "ERR index out of bounds");
    case ENOTSUP:
        return SiderModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    default: assert(0); /* Can't happen */
    }
}

/* LIST.GET key index */
int list_get(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    long long index;
    if (SiderModule_StringToLongLong(argv[2], &index) != REDISMODULE_OK) {
        return SiderModule_ReplyWithError(ctx, "ERR index must be a number");
    }
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    SiderModuleString *value = SiderModule_ListGet(key, index);
    if (value) {
        SiderModule_ReplyWithString(ctx, value);
        SiderModule_FreeString(ctx, value);
    } else {
        replyByErrno(ctx);
    }
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

/* LIST.SET key index value */
int list_set(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) return SiderModule_WrongArity(ctx);
    long long index;
    if (SiderModule_StringToLongLong(argv[2], &index) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "ERR index must be a number");
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    if (SiderModule_ListSet(key, index, argv[3]) == REDISMODULE_OK) {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

/* LIST.INSERT key index value
 *
 * If index is negative, value is inserted after, otherwise before the element
 * at index.
 */
int list_insert(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 4) return SiderModule_WrongArity(ctx);
    long long index;
    if (SiderModule_StringToLongLong(argv[2], &index) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "ERR index must be a number");
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    if (SiderModule_ListInsert(key, index, argv[3]) == REDISMODULE_OK) {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

/* LIST.DELETE key index */
int list_delete(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 3) return SiderModule_WrongArity(ctx);
    long long index;
    if (SiderModule_StringToLongLong(argv[2], &index) != REDISMODULE_OK) {
        SiderModule_ReplyWithError(ctx, "ERR index must be a number");
        return REDISMODULE_OK;
    }
    SiderModuleKey *key = SiderModule_OpenKey(ctx, argv[1], REDISMODULE_WRITE);
    if (SiderModule_ListDelete(key, index) == REDISMODULE_OK) {
        SiderModule_ReplyWithSimpleString(ctx, "OK");
    } else {
        replyByErrno(ctx);
    }
    SiderModule_CloseKey(key);
    return REDISMODULE_OK;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (SiderModule_Init(ctx, "list", 1, REDISMODULE_APIVER_1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.getall", list_getall, "",
                                  1, 1, 1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.edit", list_edit, "write",
                                  1, 1, 1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.get", list_get, "write",
                                  1, 1, 1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.set", list_set, "write",
                                  1, 1, 1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.insert", list_insert, "write",
                                  1, 1, 1) == REDISMODULE_OK &&
        SiderModule_CreateCommand(ctx, "list.delete", list_delete, "write",
                                  1, 1, 1) == REDISMODULE_OK) {
        return REDISMODULE_OK;
    } else {
        return REDISMODULE_ERR;
    }
}
