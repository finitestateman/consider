/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2014, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 * Copyright (c) 2015, Matt Stancliff <matt at genges dot com>,
 *                     Jan-Erik Rediger <janerik at fnordig dot com>
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
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>

#include "hisider.h"
#include "net.h"
#include "sds.h"
#include "async.h"
#include "win32.h"

extern int siderContextUpdateConnectTimeout(siderContext *c, const struct timeval *timeout);
extern int siderContextUpdateCommandTimeout(siderContext *c, const struct timeval *timeout);

static siderContextFuncs siderContextDefaultFuncs = {
    .close = siderNetClose,
    .free_privctx = NULL,
    .async_read = siderAsyncRead,
    .async_write = siderAsyncWrite,
    .read = siderNetRead,
    .write = siderNetWrite
};

static siderReply *createReplyObject(int type);
static void *createStringObject(const siderReadTask *task, char *str, size_t len);
static void *createArrayObject(const siderReadTask *task, size_t elements);
static void *createIntegerObject(const siderReadTask *task, long long value);
static void *createDoubleObject(const siderReadTask *task, double value, char *str, size_t len);
static void *createNilObject(const siderReadTask *task);
static void *createBoolObject(const siderReadTask *task, int bval);

/* Default set of functions to build the reply. Keep in mind that such a
 * function returning NULL is interpreted as OOM. */
static siderReplyObjectFunctions defaultFunctions = {
    createStringObject,
    createArrayObject,
    createIntegerObject,
    createDoubleObject,
    createNilObject,
    createBoolObject,
    freeReplyObject
};

/* Create a reply object */
static siderReply *createReplyObject(int type) {
    siderReply *r = hi_calloc(1,sizeof(*r));

    if (r == NULL)
        return NULL;

    r->type = type;
    return r;
}

/* Free a reply object */
void freeReplyObject(void *reply) {
    siderReply *r = reply;
    size_t j;

    if (r == NULL)
        return;

    switch(r->type) {
    case REDIS_REPLY_INTEGER:
    case REDIS_REPLY_NIL:
    case REDIS_REPLY_BOOL:
        break; /* Nothing to free */
    case REDIS_REPLY_ARRAY:
    case REDIS_REPLY_MAP:
    case REDIS_REPLY_SET:
    case REDIS_REPLY_PUSH:
        if (r->element != NULL) {
            for (j = 0; j < r->elements; j++)
                freeReplyObject(r->element[j]);
            hi_free(r->element);
        }
        break;
    case REDIS_REPLY_ERROR:
    case REDIS_REPLY_STATUS:
    case REDIS_REPLY_STRING:
    case REDIS_REPLY_DOUBLE:
    case REDIS_REPLY_VERB:
    case REDIS_REPLY_BIGNUM:
        hi_free(r->str);
        break;
    }
    hi_free(r);
}

static void *createStringObject(const siderReadTask *task, char *str, size_t len) {
    siderReply *r, *parent;
    char *buf;

    r = createReplyObject(task->type);
    if (r == NULL)
        return NULL;

    assert(task->type == REDIS_REPLY_ERROR  ||
           task->type == REDIS_REPLY_STATUS ||
           task->type == REDIS_REPLY_STRING ||
           task->type == REDIS_REPLY_VERB   ||
           task->type == REDIS_REPLY_BIGNUM);

    /* Copy string value */
    if (task->type == REDIS_REPLY_VERB) {
        buf = hi_malloc(len-4+1); /* Skip 4 bytes of verbatim type header. */
        if (buf == NULL) goto oom;

        memcpy(r->vtype,str,3);
        r->vtype[3] = '\0';
        memcpy(buf,str+4,len-4);
        buf[len-4] = '\0';
        r->len = len - 4;
    } else {
        buf = hi_malloc(len+1);
        if (buf == NULL) goto oom;

        memcpy(buf,str,len);
        buf[len] = '\0';
        r->len = len;
    }
    r->str = buf;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;

oom:
    freeReplyObject(r);
    return NULL;
}

static void *createArrayObject(const siderReadTask *task, size_t elements) {
    siderReply *r, *parent;

    r = createReplyObject(task->type);
    if (r == NULL)
        return NULL;

    if (elements > 0) {
        r->element = hi_calloc(elements,sizeof(siderReply*));
        if (r->element == NULL) {
            freeReplyObject(r);
            return NULL;
        }
    }

    r->elements = elements;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createIntegerObject(const siderReadTask *task, long long value) {
    siderReply *r, *parent;

    r = createReplyObject(REDIS_REPLY_INTEGER);
    if (r == NULL)
        return NULL;

    r->integer = value;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createDoubleObject(const siderReadTask *task, double value, char *str, size_t len) {
    siderReply *r, *parent;

    if (len == SIZE_MAX) // Prevents hi_malloc(0) if len equals to SIZE_MAX
        return NULL;

    r = createReplyObject(REDIS_REPLY_DOUBLE);
    if (r == NULL)
        return NULL;

    r->dval = value;
    r->str = hi_malloc(len+1);
    if (r->str == NULL) {
        freeReplyObject(r);
        return NULL;
    }

    /* The double reply also has the original protocol string representing a
     * double as a null terminated string. This way the caller does not need
     * to format back for string conversion, especially since Sider does efforts
     * to make the string more human readable avoiding the calssical double
     * decimal string conversion artifacts. */
    memcpy(r->str, str, len);
    r->str[len] = '\0';
    r->len = len;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createNilObject(const siderReadTask *task) {
    siderReply *r, *parent;

    r = createReplyObject(REDIS_REPLY_NIL);
    if (r == NULL)
        return NULL;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;
}

static void *createBoolObject(const siderReadTask *task, int bval) {
    siderReply *r, *parent;

    r = createReplyObject(REDIS_REPLY_BOOL);
    if (r == NULL)
        return NULL;

    r->integer = bval != 0;

    if (task->parent) {
        parent = task->parent->obj;
        assert(parent->type == REDIS_REPLY_ARRAY ||
               parent->type == REDIS_REPLY_MAP ||
               parent->type == REDIS_REPLY_SET ||
               parent->type == REDIS_REPLY_PUSH);
        parent->element[task->idx] = r;
    }
    return r;
}

/* Return the number of digits of 'v' when converted to string in radix 10.
 * Implementation borrowed from link in sider/src/util.c:string2ll(). */
static uint32_t countDigits(uint64_t v) {
  uint32_t result = 1;
  for (;;) {
    if (v < 10) return result;
    if (v < 100) return result + 1;
    if (v < 1000) return result + 2;
    if (v < 10000) return result + 3;
    v /= 10000U;
    result += 4;
  }
}

/* Helper that calculates the bulk length given a certain string length. */
static size_t bulklen(size_t len) {
    return 1+countDigits(len)+2+len+2;
}

int sidervFormatCommand(char **target, const char *format, va_list ap) {
    const char *c = format;
    char *cmd = NULL; /* final command */
    int pos; /* position in final command */
    hisds curarg, newarg; /* current argument */
    int touched = 0; /* was the current argument touched? */
    char **curargv = NULL, **newargv = NULL;
    int argc = 0;
    int totlen = 0;
    int error_type = 0; /* 0 = no error; -1 = memory error; -2 = format error */
    int j;

    /* Abort if there is not target to set */
    if (target == NULL)
        return -1;

    /* Build the command string accordingly to protocol */
    curarg = hi_sdsempty();
    if (curarg == NULL)
        return -1;

    while(*c != '\0') {
        if (*c != '%' || c[1] == '\0') {
            if (*c == ' ') {
                if (touched) {
                    newargv = hi_realloc(curargv,sizeof(char*)*(argc+1));
                    if (newargv == NULL) goto memory_err;
                    curargv = newargv;
                    curargv[argc++] = curarg;
                    totlen += bulklen(hi_sdslen(curarg));

                    /* curarg is put in argv so it can be overwritten. */
                    curarg = hi_sdsempty();
                    if (curarg == NULL) goto memory_err;
                    touched = 0;
                }
            } else {
                newarg = hi_sdscatlen(curarg,c,1);
                if (newarg == NULL) goto memory_err;
                curarg = newarg;
                touched = 1;
            }
        } else {
            char *arg;
            size_t size;

            /* Set newarg so it can be checked even if it is not touched. */
            newarg = curarg;

            switch(c[1]) {
            case 's':
                arg = va_arg(ap,char*);
                size = strlen(arg);
                if (size > 0)
                    newarg = hi_sdscatlen(curarg,arg,size);
                break;
            case 'b':
                arg = va_arg(ap,char*);
                size = va_arg(ap,size_t);
                if (size > 0)
                    newarg = hi_sdscatlen(curarg,arg,size);
                break;
            case '%':
                newarg = hi_sdscat(curarg,"%");
                break;
            default:
                /* Try to detect printf format */
                {
                    static const char intfmts[] = "diouxX";
                    static const char flags[] = "#0-+ ";
                    char _format[16];
                    const char *_p = c+1;
                    size_t _l = 0;
                    va_list _cpy;

                    /* Flags */
                    while (*_p != '\0' && strchr(flags,*_p) != NULL) _p++;

                    /* Field width */
                    while (*_p != '\0' && isdigit((int) *_p)) _p++;

                    /* Precision */
                    if (*_p == '.') {
                        _p++;
                        while (*_p != '\0' && isdigit((int) *_p)) _p++;
                    }

                    /* Copy va_list before consuming with va_arg */
                    va_copy(_cpy,ap);

                    /* Make sure we have more characters otherwise strchr() accepts
                     * '\0' as an integer specifier. This is checked after above
                     * va_copy() to avoid UB in fmt_invalid's call to va_end(). */
                    if (*_p == '\0') goto fmt_invalid;

                    /* Integer conversion (without modifiers) */
                    if (strchr(intfmts,*_p) != NULL) {
                        va_arg(ap,int);
                        goto fmt_valid;
                    }

                    /* Double conversion (without modifiers) */
                    if (strchr("eEfFgGaA",*_p) != NULL) {
                        va_arg(ap,double);
                        goto fmt_valid;
                    }

                    /* Size: char */
                    if (_p[0] == 'h' && _p[1] == 'h') {
                        _p += 2;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,int); /* char gets promoted to int */
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: short */
                    if (_p[0] == 'h') {
                        _p += 1;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,int); /* short gets promoted to int */
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: long long */
                    if (_p[0] == 'l' && _p[1] == 'l') {
                        _p += 2;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,long long);
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                    /* Size: long */
                    if (_p[0] == 'l') {
                        _p += 1;
                        if (*_p != '\0' && strchr(intfmts,*_p) != NULL) {
                            va_arg(ap,long);
                            goto fmt_valid;
                        }
                        goto fmt_invalid;
                    }

                fmt_invalid:
                    va_end(_cpy);
                    goto format_err;

                fmt_valid:
                    _l = (_p+1)-c;
                    if (_l < sizeof(_format)-2) {
                        memcpy(_format,c,_l);
                        _format[_l] = '\0';
                        newarg = hi_sdscatvprintf(curarg,_format,_cpy);

                        /* Update current position (note: outer blocks
                         * increment c twice so compensate here) */
                        c = _p-1;
                    }

                    va_end(_cpy);
                    break;
                }
            }

            if (newarg == NULL) goto memory_err;
            curarg = newarg;

            touched = 1;
            c++;
            if (*c == '\0')
                break;
        }
        c++;
    }

    /* Add the last argument if needed */
    if (touched) {
        newargv = hi_realloc(curargv,sizeof(char*)*(argc+1));
        if (newargv == NULL) goto memory_err;
        curargv = newargv;
        curargv[argc++] = curarg;
        totlen += bulklen(hi_sdslen(curarg));
    } else {
        hi_sdsfree(curarg);
    }

    /* Clear curarg because it was put in curargv or was free'd. */
    curarg = NULL;

    /* Add bytes needed to hold multi bulk count */
    totlen += 1+countDigits(argc)+2;

    /* Build the command at protocol level */
    cmd = hi_malloc(totlen+1);
    if (cmd == NULL) goto memory_err;

    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        pos += sprintf(cmd+pos,"$%zu\r\n",hi_sdslen(curargv[j]));
        memcpy(cmd+pos,curargv[j],hi_sdslen(curargv[j]));
        pos += hi_sdslen(curargv[j]);
        hi_sdsfree(curargv[j]);
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    cmd[pos] = '\0';

    hi_free(curargv);
    *target = cmd;
    return totlen;

format_err:
    error_type = -2;
    goto cleanup;

memory_err:
    error_type = -1;
    goto cleanup;

cleanup:
    if (curargv) {
        while(argc--)
            hi_sdsfree(curargv[argc]);
        hi_free(curargv);
    }

    hi_sdsfree(curarg);
    hi_free(cmd);

    return error_type;
}

/* Format a command according to the Sider protocol. This function
 * takes a format similar to printf:
 *
 * %s represents a C null terminated string you want to interpolate
 * %b represents a binary safe string
 *
 * When using %b you need to provide both the pointer to the string
 * and the length in bytes as a size_t. Examples:
 *
 * len = siderFormatCommand(target, "GET %s", mykey);
 * len = siderFormatCommand(target, "SET %s %b", mykey, myval, myvallen);
 */
int siderFormatCommand(char **target, const char *format, ...) {
    va_list ap;
    int len;
    va_start(ap,format);
    len = sidervFormatCommand(target,format,ap);
    va_end(ap);

    /* The API says "-1" means bad result, but we now also return "-2" in some
     * cases.  Force the return value to always be -1. */
    if (len < 0)
        len = -1;

    return len;
}

/* Format a command according to the Sider protocol using an hisds string and
 * hi_sdscatfmt for the processing of arguments. This function takes the
 * number of arguments, an array with arguments and an array with their
 * lengths. If the latter is set to NULL, strlen will be used to compute the
 * argument lengths.
 */
long long siderFormatSdsCommandArgv(hisds *target, int argc, const char **argv,
                                    const size_t *argvlen)
{
    hisds cmd, aux;
    unsigned long long totlen, len;
    int j;

    /* Abort on a NULL target */
    if (target == NULL)
        return -1;

    /* Calculate our total size */
    totlen = 1+countDigits(argc)+2;
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        totlen += bulklen(len);
    }

    /* Use an SDS string for command construction */
    cmd = hi_sdsempty();
    if (cmd == NULL)
        return -1;

    /* We already know how much storage we need */
    aux = hi_sdsMakeRoomFor(cmd, totlen);
    if (aux == NULL) {
        hi_sdsfree(cmd);
        return -1;
    }

    cmd = aux;

    /* Construct command */
    cmd = hi_sdscatfmt(cmd, "*%i\r\n", argc);
    for (j=0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        cmd = hi_sdscatfmt(cmd, "$%U\r\n", len);
        cmd = hi_sdscatlen(cmd, argv[j], len);
        cmd = hi_sdscatlen(cmd, "\r\n", sizeof("\r\n")-1);
    }

    assert(hi_sdslen(cmd)==totlen);

    *target = cmd;
    return totlen;
}

void siderFreeSdsCommand(hisds cmd) {
    hi_sdsfree(cmd);
}

/* Format a command according to the Sider protocol. This function takes the
 * number of arguments, an array with arguments and an array with their
 * lengths. If the latter is set to NULL, strlen will be used to compute the
 * argument lengths.
 */
long long siderFormatCommandArgv(char **target, int argc, const char **argv, const size_t *argvlen) {
    char *cmd = NULL; /* final command */
    size_t pos; /* position in final command */
    size_t len, totlen;
    int j;

    /* Abort on a NULL target */
    if (target == NULL)
        return -1;

    /* Calculate number of bytes needed for the command */
    totlen = 1+countDigits(argc)+2;
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        totlen += bulklen(len);
    }

    /* Build the command at protocol level */
    cmd = hi_malloc(totlen+1);
    if (cmd == NULL)
        return -1;

    pos = sprintf(cmd,"*%d\r\n",argc);
    for (j = 0; j < argc; j++) {
        len = argvlen ? argvlen[j] : strlen(argv[j]);
        pos += sprintf(cmd+pos,"$%zu\r\n",len);
        memcpy(cmd+pos,argv[j],len);
        pos += len;
        cmd[pos++] = '\r';
        cmd[pos++] = '\n';
    }
    assert(pos == totlen);
    cmd[pos] = '\0';

    *target = cmd;
    return totlen;
}

void siderFreeCommand(char *cmd) {
    hi_free(cmd);
}

void __siderSetError(siderContext *c, int type, const char *str) {
    size_t len;

    c->err = type;
    if (str != NULL) {
        len = strlen(str);
        len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
        memcpy(c->errstr,str,len);
        c->errstr[len] = '\0';
    } else {
        /* Only REDIS_ERR_IO may lack a description! */
        assert(type == REDIS_ERR_IO);
        strerror_r(errno, c->errstr, sizeof(c->errstr));
    }
}

siderReader *siderReaderCreate(void) {
    return siderReaderCreateWithFunctions(&defaultFunctions);
}

static void siderPushAutoFree(void *privdata, void *reply) {
    (void)privdata;
    freeReplyObject(reply);
}

static siderContext *siderContextInit(void) {
    siderContext *c;

    c = hi_calloc(1, sizeof(*c));
    if (c == NULL)
        return NULL;

    c->funcs = &siderContextDefaultFuncs;

    c->obuf = hi_sdsempty();
    c->reader = siderReaderCreate();
    c->fd = REDIS_INVALID_FD;

    if (c->obuf == NULL || c->reader == NULL) {
        siderFree(c);
        return NULL;
    }

    return c;
}

void siderFree(siderContext *c) {
    if (c == NULL)
        return;

    if (c->funcs && c->funcs->close) {
        c->funcs->close(c);
    }

    hi_sdsfree(c->obuf);
    siderReaderFree(c->reader);
    hi_free(c->tcp.host);
    hi_free(c->tcp.source_addr);
    hi_free(c->unix_sock.path);
    hi_free(c->connect_timeout);
    hi_free(c->command_timeout);
    hi_free(c->saddr);

    if (c->privdata && c->free_privdata)
        c->free_privdata(c->privdata);

    if (c->funcs && c->funcs->free_privctx)
        c->funcs->free_privctx(c->privctx);

    memset(c, 0xff, sizeof(*c));
    hi_free(c);
}

siderFD siderFreeKeepFd(siderContext *c) {
    siderFD fd = c->fd;
    c->fd = REDIS_INVALID_FD;
    siderFree(c);
    return fd;
}

int siderReconnect(siderContext *c) {
    c->err = 0;
    memset(c->errstr, '\0', strlen(c->errstr));

    if (c->privctx && c->funcs->free_privctx) {
        c->funcs->free_privctx(c->privctx);
        c->privctx = NULL;
    }

    if (c->funcs && c->funcs->close) {
        c->funcs->close(c);
    }

    hi_sdsfree(c->obuf);
    siderReaderFree(c->reader);

    c->obuf = hi_sdsempty();
    c->reader = siderReaderCreate();

    if (c->obuf == NULL || c->reader == NULL) {
        __siderSetError(c, REDIS_ERR_OOM, "Out of memory");
        return REDIS_ERR;
    }

    int ret = REDIS_ERR;
    if (c->connection_type == REDIS_CONN_TCP) {
        ret = siderContextConnectBindTcp(c, c->tcp.host, c->tcp.port,
               c->connect_timeout, c->tcp.source_addr);
    } else if (c->connection_type == REDIS_CONN_UNIX) {
        ret = siderContextConnectUnix(c, c->unix_sock.path, c->connect_timeout);
    } else {
        /* Something bad happened here and shouldn't have. There isn't
           enough information in the context to reconnect. */
        __siderSetError(c,REDIS_ERR_OTHER,"Not enough information to reconnect");
        ret = REDIS_ERR;
    }

    if (c->command_timeout != NULL && (c->flags & REDIS_BLOCK) && c->fd != REDIS_INVALID_FD) {
        siderContextSetTimeout(c, *c->command_timeout);
    }

    return ret;
}

siderContext *siderConnectWithOptions(const siderOptions *options) {
    siderContext *c = siderContextInit();
    if (c == NULL) {
        return NULL;
    }
    if (!(options->options & REDIS_OPT_NONBLOCK)) {
        c->flags |= REDIS_BLOCK;
    }
    if (options->options & REDIS_OPT_REUSEADDR) {
        c->flags |= REDIS_REUSEADDR;
    }
    if (options->options & REDIS_OPT_NOAUTOFREE) {
        c->flags |= REDIS_NO_AUTO_FREE;
    }
    if (options->options & REDIS_OPT_NOAUTOFREEREPLIES) {
        c->flags |= REDIS_NO_AUTO_FREE_REPLIES;
    }
    if (options->options & REDIS_OPT_PREFER_IPV4) {
        c->flags |= REDIS_PREFER_IPV4;
    }
    if (options->options & REDIS_OPT_PREFER_IPV6) {
        c->flags |= REDIS_PREFER_IPV6;
    }

    /* Set any user supplied RESP3 PUSH handler or use freeReplyObject
     * as a default unless specifically flagged that we don't want one. */
    if (options->push_cb != NULL)
        siderSetPushCallback(c, options->push_cb);
    else if (!(options->options & REDIS_OPT_NO_PUSH_AUTOFREE))
        siderSetPushCallback(c, siderPushAutoFree);

    c->privdata = options->privdata;
    c->free_privdata = options->free_privdata;

    if (siderContextUpdateConnectTimeout(c, options->connect_timeout) != REDIS_OK ||
        siderContextUpdateCommandTimeout(c, options->command_timeout) != REDIS_OK) {
        __siderSetError(c, REDIS_ERR_OOM, "Out of memory");
        return c;
    }

    if (options->type == REDIS_CONN_TCP) {
        siderContextConnectBindTcp(c, options->endpoint.tcp.ip,
                                   options->endpoint.tcp.port, options->connect_timeout,
                                   options->endpoint.tcp.source_addr);
    } else if (options->type == REDIS_CONN_UNIX) {
        siderContextConnectUnix(c, options->endpoint.unix_socket,
                                options->connect_timeout);
    } else if (options->type == REDIS_CONN_USERFD) {
        c->fd = options->endpoint.fd;
        c->flags |= REDIS_CONNECTED;
    } else {
        siderFree(c);
        return NULL;
    }

    if (c->err == 0 && c->fd != REDIS_INVALID_FD &&
        options->command_timeout != NULL && (c->flags & REDIS_BLOCK))
    {
        siderContextSetTimeout(c, *options->command_timeout);
    }

    return c;
}

/* Connect to a Sider instance. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
siderContext *siderConnect(const char *ip, int port) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectWithTimeout(const char *ip, int port, const struct timeval tv) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.connect_timeout = &tv;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectNonBlock(const char *ip, int port) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.options |= REDIS_OPT_NONBLOCK;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectBindNonBlock(const char *ip, int port,
                                       const char *source_addr) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.endpoint.tcp.source_addr = source_addr;
    options.options |= REDIS_OPT_NONBLOCK;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectBindNonBlockWithReuse(const char *ip, int port,
                                                const char *source_addr) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, ip, port);
    options.endpoint.tcp.source_addr = source_addr;
    options.options |= REDIS_OPT_NONBLOCK|REDIS_OPT_REUSEADDR;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectUnix(const char *path) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_UNIX(&options, path);
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectUnixWithTimeout(const char *path, const struct timeval tv) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_UNIX(&options, path);
    options.connect_timeout = &tv;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectUnixNonBlock(const char *path) {
    siderOptions options = {0};
    REDIS_OPTIONS_SET_UNIX(&options, path);
    options.options |= REDIS_OPT_NONBLOCK;
    return siderConnectWithOptions(&options);
}

siderContext *siderConnectFd(siderFD fd) {
    siderOptions options = {0};
    options.type = REDIS_CONN_USERFD;
    options.endpoint.fd = fd;
    return siderConnectWithOptions(&options);
}

/* Set read/write timeout on a blocking socket. */
int siderSetTimeout(siderContext *c, const struct timeval tv) {
    if (c->flags & REDIS_BLOCK)
        return siderContextSetTimeout(c,tv);
    return REDIS_ERR;
}

int siderEnableKeepAliveWithInterval(siderContext *c, int interval) {
    return siderKeepAlive(c, interval);
}

/* Enable connection KeepAlive. */
int siderEnableKeepAlive(siderContext *c) {
    return siderKeepAlive(c, REDIS_KEEPALIVE_INTERVAL);
}

/* Set the socket option TCP_USER_TIMEOUT. */
int siderSetTcpUserTimeout(siderContext *c, unsigned int timeout) {
    return siderContextSetTcpUserTimeout(c, timeout);
}

/* Set a user provided RESP3 PUSH handler and return any old one set. */
siderPushFn *siderSetPushCallback(siderContext *c, siderPushFn *fn) {
    siderPushFn *old = c->push_cb;
    c->push_cb = fn;
    return old;
}

/* Use this function to handle a read event on the descriptor. It will try
 * and read some bytes from the socket and feed them to the reply parser.
 *
 * After this function is called, you may use siderGetReplyFromReader to
 * see if there is a reply available. */
int siderBufferRead(siderContext *c) {
    char buf[1024*16];
    int nread;

    /* Return early when the context has seen an error. */
    if (c->err)
        return REDIS_ERR;

    nread = c->funcs->read(c, buf, sizeof(buf));
    if (nread < 0) {
        return REDIS_ERR;
    }
    if (nread > 0 && siderReaderFeed(c->reader, buf, nread) != REDIS_OK) {
        __siderSetError(c, c->reader->err, c->reader->errstr);
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* Write the output buffer to the socket.
 *
 * Returns REDIS_OK when the buffer is empty, or (a part of) the buffer was
 * successfully written to the socket. When the buffer is empty after the
 * write operation, "done" is set to 1 (if given).
 *
 * Returns REDIS_ERR if an unrecoverable error occurred in the underlying
 * c->funcs->write function.
 */
int siderBufferWrite(siderContext *c, int *done) {

    /* Return early when the context has seen an error. */
    if (c->err)
        return REDIS_ERR;

    if (hi_sdslen(c->obuf) > 0) {
        ssize_t nwritten = c->funcs->write(c);
        if (nwritten < 0) {
            return REDIS_ERR;
        } else if (nwritten > 0) {
            if (nwritten == (ssize_t)hi_sdslen(c->obuf)) {
                hi_sdsfree(c->obuf);
                c->obuf = hi_sdsempty();
                if (c->obuf == NULL)
                    goto oom;
            } else {
                if (hi_sdsrange(c->obuf,nwritten,-1) < 0) goto oom;
            }
        }
    }
    if (done != NULL) *done = (hi_sdslen(c->obuf) == 0);
    return REDIS_OK;

oom:
    __siderSetError(c, REDIS_ERR_OOM, "Out of memory");
    return REDIS_ERR;
}

/* Internal helper that returns 1 if the reply was a RESP3 PUSH
 * message and we handled it with a user-provided callback. */
static int siderHandledPushReply(siderContext *c, void *reply) {
    if (reply && c->push_cb && siderIsPushReply(reply)) {
        c->push_cb(c->privdata, reply);
        return 1;
    }

    return 0;
}

/* Get a reply from our reader or set an error in the context. */
int siderGetReplyFromReader(siderContext *c, void **reply) {
    if (siderReaderGetReply(c->reader, reply) == REDIS_ERR) {
        __siderSetError(c,c->reader->err,c->reader->errstr);
        return REDIS_ERR;
    }

    return REDIS_OK;
}

/* Internal helper to get the next reply from our reader while handling
 * any PUSH messages we encounter along the way.  This is separate from
 * siderGetReplyFromReader so as to not change its behavior. */
static int siderNextInBandReplyFromReader(siderContext *c, void **reply) {
    do {
        if (siderGetReplyFromReader(c, reply) == REDIS_ERR)
            return REDIS_ERR;
    } while (siderHandledPushReply(c, *reply));

    return REDIS_OK;
}

int siderGetReply(siderContext *c, void **reply) {
    int wdone = 0;
    void *aux = NULL;

    /* Try to read pending replies */
    if (siderNextInBandReplyFromReader(c,&aux) == REDIS_ERR)
        return REDIS_ERR;

    /* For the blocking context, flush output buffer and read reply */
    if (aux == NULL && c->flags & REDIS_BLOCK) {
        /* Write until done */
        do {
            if (siderBufferWrite(c,&wdone) == REDIS_ERR)
                return REDIS_ERR;
        } while (!wdone);

        /* Read until there is a reply */
        do {
            if (siderBufferRead(c) == REDIS_ERR)
                return REDIS_ERR;

            if (siderNextInBandReplyFromReader(c,&aux) == REDIS_ERR)
                return REDIS_ERR;
        } while (aux == NULL);
    }

    /* Set reply or free it if we were passed NULL */
    if (reply != NULL) {
        *reply = aux;
    } else {
        freeReplyObject(aux);
    }

    return REDIS_OK;
}


/* Helper function for the siderAppendCommand* family of functions.
 *
 * Write a formatted command to the output buffer. When this family
 * is used, you need to call siderGetReply yourself to retrieve
 * the reply (or replies in pub/sub).
 */
int __siderAppendCommand(siderContext *c, const char *cmd, size_t len) {
    hisds newbuf;

    newbuf = hi_sdscatlen(c->obuf,cmd,len);
    if (newbuf == NULL) {
        __siderSetError(c,REDIS_ERR_OOM,"Out of memory");
        return REDIS_ERR;
    }

    c->obuf = newbuf;
    return REDIS_OK;
}

int siderAppendFormattedCommand(siderContext *c, const char *cmd, size_t len) {

    if (__siderAppendCommand(c, cmd, len) != REDIS_OK) {
        return REDIS_ERR;
    }

    return REDIS_OK;
}

int sidervAppendCommand(siderContext *c, const char *format, va_list ap) {
    char *cmd;
    int len;

    len = sidervFormatCommand(&cmd,format,ap);
    if (len == -1) {
        __siderSetError(c,REDIS_ERR_OOM,"Out of memory");
        return REDIS_ERR;
    } else if (len == -2) {
        __siderSetError(c,REDIS_ERR_OTHER,"Invalid format string");
        return REDIS_ERR;
    }

    if (__siderAppendCommand(c,cmd,len) != REDIS_OK) {
        hi_free(cmd);
        return REDIS_ERR;
    }

    hi_free(cmd);
    return REDIS_OK;
}

int siderAppendCommand(siderContext *c, const char *format, ...) {
    va_list ap;
    int ret;

    va_start(ap,format);
    ret = sidervAppendCommand(c,format,ap);
    va_end(ap);
    return ret;
}

int siderAppendCommandArgv(siderContext *c, int argc, const char **argv, const size_t *argvlen) {
    hisds cmd;
    long long len;

    len = siderFormatSdsCommandArgv(&cmd,argc,argv,argvlen);
    if (len == -1) {
        __siderSetError(c,REDIS_ERR_OOM,"Out of memory");
        return REDIS_ERR;
    }

    if (__siderAppendCommand(c,cmd,len) != REDIS_OK) {
        hi_sdsfree(cmd);
        return REDIS_ERR;
    }

    hi_sdsfree(cmd);
    return REDIS_OK;
}

/* Helper function for the siderCommand* family of functions.
 *
 * Write a formatted command to the output buffer. If the given context is
 * blocking, immediately read the reply into the "reply" pointer. When the
 * context is non-blocking, the "reply" pointer will not be used and the
 * command is simply appended to the write buffer.
 *
 * Returns the reply when a reply was successfully retrieved. Returns NULL
 * otherwise. When NULL is returned in a blocking context, the error field
 * in the context will be set.
 */
static void *__siderBlockForReply(siderContext *c) {
    void *reply;

    if (c->flags & REDIS_BLOCK) {
        if (siderGetReply(c,&reply) != REDIS_OK)
            return NULL;
        return reply;
    }
    return NULL;
}

void *sidervCommand(siderContext *c, const char *format, va_list ap) {
    if (sidervAppendCommand(c,format,ap) != REDIS_OK)
        return NULL;
    return __siderBlockForReply(c);
}

void *siderCommand(siderContext *c, const char *format, ...) {
    va_list ap;
    va_start(ap,format);
    void *reply = sidervCommand(c,format,ap);
    va_end(ap);
    return reply;
}

void *siderCommandArgv(siderContext *c, int argc, const char **argv, const size_t *argvlen) {
    if (siderAppendCommandArgv(c,argc,argv,argvlen) != REDIS_OK)
        return NULL;
    return __siderBlockForReply(c);
}
