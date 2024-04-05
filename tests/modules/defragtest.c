/* A module that implements defrag callback mechanisms.
 */

#include "sidermodule.h"
#include <stdlib.h>

static SiderModuleType *FragType;

struct FragObject {
    unsigned long len;
    void **values;
    int maxstep;
};

/* Make sure we get the expected cursor */
unsigned long int last_set_cursor = 0;

unsigned long int datatype_attempts = 0;
unsigned long int datatype_defragged = 0;
unsigned long int datatype_resumes = 0;
unsigned long int datatype_wrong_cursor = 0;
unsigned long int global_attempts = 0;
unsigned long int global_defragged = 0;

int global_strings_len = 0;
SiderModuleString **global_strings = NULL;

static void createGlobalStrings(SiderModuleCtx *ctx, int count)
{
    global_strings_len = count;
    global_strings = SiderModule_Alloc(sizeof(SiderModuleString *) * count);

    for (int i = 0; i < count; i++) {
        global_strings[i] = SiderModule_CreateStringFromLongLong(ctx, i);
    }
}

static void defragGlobalStrings(SiderModuleDefragCtx *ctx)
{
    for (int i = 0; i < global_strings_len; i++) {
        SiderModuleString *new = SiderModule_DefragSiderModuleString(ctx, global_strings[i]);
        global_attempts++;
        if (new != NULL) {
            global_strings[i] = new;
            global_defragged++;
        }
    }
}

static void FragInfo(SiderModuleInfoCtx *ctx, int for_crash_report) {
    REDISMODULE_NOT_USED(for_crash_report);

    SiderModule_InfoAddSection(ctx, "stats");
    SiderModule_InfoAddFieldLongLong(ctx, "datatype_attempts", datatype_attempts);
    SiderModule_InfoAddFieldLongLong(ctx, "datatype_defragged", datatype_defragged);
    SiderModule_InfoAddFieldLongLong(ctx, "datatype_resumes", datatype_resumes);
    SiderModule_InfoAddFieldLongLong(ctx, "datatype_wrong_cursor", datatype_wrong_cursor);
    SiderModule_InfoAddFieldLongLong(ctx, "global_attempts", global_attempts);
    SiderModule_InfoAddFieldLongLong(ctx, "global_defragged", global_defragged);
}

struct FragObject *createFragObject(unsigned long len, unsigned long size, int maxstep) {
    struct FragObject *o = SiderModule_Alloc(sizeof(*o));
    o->len = len;
    o->values = SiderModule_Alloc(sizeof(SiderModuleString*) * len);
    o->maxstep = maxstep;

    for (unsigned long i = 0; i < len; i++) {
        o->values[i] = SiderModule_Calloc(1, size);
    }

    return o;
}

/* FRAG.RESETSTATS */
static int fragResetStatsCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    datatype_attempts = 0;
    datatype_defragged = 0;
    datatype_resumes = 0;
    datatype_wrong_cursor = 0;
    global_attempts = 0;
    global_defragged = 0;

    SiderModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}

/* FRAG.CREATE key len size maxstep */
static int fragCreateCommand(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    if (argc != 5)
        return SiderModule_WrongArity(ctx);

    SiderModuleKey *key = SiderModule_OpenKey(ctx,argv[1],
                                              REDISMODULE_READ|REDISMODULE_WRITE);
    int type = SiderModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY)
    {
        return SiderModule_ReplyWithError(ctx, "ERR key exists");
    }

    long long len;
    if ((SiderModule_StringToLongLong(argv[2], &len) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid len");
    }

    long long size;
    if ((SiderModule_StringToLongLong(argv[3], &size) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid size");
    }

    long long maxstep;
    if ((SiderModule_StringToLongLong(argv[4], &maxstep) != REDISMODULE_OK)) {
        return SiderModule_ReplyWithError(ctx, "ERR invalid maxstep");
    }

    struct FragObject *o = createFragObject(len, size, maxstep);
    SiderModule_ModuleTypeSetValue(key, FragType, o);
    SiderModule_ReplyWithSimpleString(ctx, "OK");
    SiderModule_CloseKey(key);

    return REDISMODULE_OK;
}

void FragFree(void *value) {
    struct FragObject *o = value;

    for (unsigned long i = 0; i < o->len; i++)
        SiderModule_Free(o->values[i]);
    SiderModule_Free(o->values);
    SiderModule_Free(o);
}

size_t FragFreeEffort(SiderModuleString *key, const void *value) {
    REDISMODULE_NOT_USED(key);

    const struct FragObject *o = value;
    return o->len;
}

int FragDefrag(SiderModuleDefragCtx *ctx, SiderModuleString *key, void **value) {
    REDISMODULE_NOT_USED(key);
    unsigned long i = 0;
    int steps = 0;

    int dbid = SiderModule_GetDbIdFromDefragCtx(ctx);
    SiderModule_Assert(dbid != -1);

    /* Attempt to get cursor, validate it's what we're exepcting */
    if (SiderModule_DefragCursorGet(ctx, &i) == REDISMODULE_OK) {
        if (i > 0) datatype_resumes++;

        /* Validate we're expecting this cursor */
        if (i != last_set_cursor) datatype_wrong_cursor++;
    } else {
        if (last_set_cursor != 0) datatype_wrong_cursor++;
    }

    /* Attempt to defrag the object itself */
    datatype_attempts++;
    struct FragObject *o = SiderModule_DefragAlloc(ctx, *value);
    if (o == NULL) {
        /* Not defragged */
        o = *value;
    } else {
        /* Defragged */
        *value = o;
        datatype_defragged++;
    }

    /* Deep defrag now */
    for (; i < o->len; i++) {
        datatype_attempts++;
        void *new = SiderModule_DefragAlloc(ctx, o->values[i]);
        if (new) {
            o->values[i] = new;
            datatype_defragged++;
        }

        if ((o->maxstep && ++steps > o->maxstep) ||
            ((i % 64 == 0) && SiderModule_DefragShouldStop(ctx)))
        {
            SiderModule_DefragCursorSet(ctx, i);
            last_set_cursor = i;
            return 1;
        }
    }

    last_set_cursor = 0;
    return 0;
}

int SiderModule_OnLoad(SiderModuleCtx *ctx, SiderModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (SiderModule_Init(ctx, "defragtest", 1, REDISMODULE_APIVER_1)
        == REDISMODULE_ERR) return REDISMODULE_ERR;

    if (SiderModule_GetTypeMethodVersion() < REDISMODULE_TYPE_METHOD_VERSION) {
        return REDISMODULE_ERR;
    }

    long long glen;
    if (argc != 1 || SiderModule_StringToLongLong(argv[0], &glen) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    createGlobalStrings(ctx, glen);

    SiderModuleTypeMethods tm = {
            .version = REDISMODULE_TYPE_METHOD_VERSION,
            .free = FragFree,
            .free_effort = FragFreeEffort,
            .defrag = FragDefrag
    };

    FragType = SiderModule_CreateDataType(ctx, "frag_type", 0, &tm);
    if (FragType == NULL) return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "frag.create",
                                  fragCreateCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (SiderModule_CreateCommand(ctx, "frag.resetstats",
                                  fragResetStatsCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    SiderModule_RegisterInfoFunc(ctx, FragInfo);
    SiderModule_RegisterDefragFunc(ctx, defragGlobalStrings);

    return REDISMODULE_OK;
}
