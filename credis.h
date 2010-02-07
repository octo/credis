/* credis.h -- a C client library for Redis, public API.
 *
 * Copyright (c) 2009, Jonas Romfelt <jonas at romfelt dot se>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
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

#ifndef __CREDIS_H
#define __CREDIS_H

#ifdef __cplusplus
extern "C" {
#endif

/* handle to a Redis server connection */
typedef struct _cr_redis* REDIS;

#define CREDIS_OK 0
#define CREDIS_ERR -90
#define CREDIS_ERR_NOMEM -91
#define CREDIS_ERR_RESOLVE -92
#define CREDIS_ERR_CONNECT -93
#define CREDIS_ERR_SEND -94
#define CREDIS_ERR_RECV -95
#define CREDIS_ERR_TIMEOUT -96
#define CREDIS_ERR_PROTOCOL -97

#define CREDIS_TYPE_NONE 1
#define CREDIS_TYPE_STRING 2
#define CREDIS_TYPE_LIST 3
#define CREDIS_TYPE_SET 4


/* Functions below should map quite nicely to redis command set. Refer 
 * to the official Redis documentation for further explanation of each 
 * command. See credis examples on how functions can be used. 
 *
 * IMPORTANT! Memory buffers are allocated and used by credis. Subsequent
 * calls to credis functions _will_ destroy the data to which returned
 * values reference to. If for instance the returned value by a call to 
 * credis_get() is to be used later in the program, a strdup() is highly 
 * recommended. 
 *
 * TODO
 *  - currently only support for zero-terminated strings, not for storing 
 *    abritary binary data as bulk data
 *  - commands for sets are not implemented
 *  - not fully tested
 *  - expect API improvements after feedback 
 *  - contd. ... */


/**** Connection handling ****************************************************/

/* setting host to NULL will use "localhost". setting port to 0 will use 
 * default port 6379 */
REDIS credis_connect(const char *host, int port, int timeout);

void credis_close(REDIS rhnd);

void credis_quit(REDIS rhnd);

int credis_auth(REDIS rhnd, const char *password);

int credis_ping(REDIS rhnd);


/**** Commands operating on string values ************************************/

int credis_set(REDIS rhnd, const char *key, const char *val);

int credis_get(REDIS rhnd, const char *key, char **val);

int credis_getset(REDIS rhnd, const char *key, const char *set_val, char **get_val);

/* returns number of values returned in vector `valv'. `keyc' is the number of
 * keys stored in `keyv'. */
int credis_mget(REDIS rhnd, int keyc, const char **keyv, char ***valv);

/* returns -1 if the key already exists */
int credis_setnx(REDIS rhnd, const char *key, const char *val);

/* TODO
 *
 * MSET key1 value1 key2 value2 ... keyN valueN set a multiple keys to multiple values in a single atomic operation
 * MSETNX key1 value1 key2 value2 ... keyN valueN set a multiple keys to multiple values in a single atomic operation if none of
 */

int credis_incr(REDIS rhnd, const char *key, int *new_val);

int credis_incrby(REDIS rhnd, const char *key, int incr_val, int *new_val);

int credis_decr(REDIS rhnd, const char *key, int *new_val);

int credis_decrby(REDIS rhnd, const char *key, int decr_val, int *new_val);

/* returns -1 if the key doesn't exists and 0 if it does */
int credis_exists(REDIS rhnd, const char *key);

/* returns -1 if the key doesn't exists and 0 if it was removed */
int credis_del(REDIS rhnd, const char *key);

/* returns type, refer to CREDIS_TYPE_* definitions */
int credis_type(REDIS rhnd, const char *key);



/**** Commands operating on key space ****************************************/

/* returns number of keys returned in vector `keyv' */
int credis_keys(REDIS rhnd, const char *pattern, char ***keyv);

int credis_randomkey(REDIS rhnd, char **key);

int credis_rename(REDIS rhnd, const char *key, const char *new_key_name);

/* returns -1 if the key already exists */
int credis_renamenx(REDIS rhnd, const char *key, const char *new_key_name);

/* returns size of db */
int credis_dbsize(REDIS rhnd);

/* returns -1 if the timeout was not set; either due to key already has 
   an associated timeout or key does not exist */
int credis_expire(REDIS rhnd, const char *key, int secs);

/* returns seconds or -1 if key does not have expire set */
int credis_ttl(REDIS rhnd, const char *key);



/**** Commands operating on lists ********************************************/

int credis_rpush(REDIS rhnd, const char *key, const char *element);

int credis_lpush(REDIS rhnd, const char *key, const char *element);

/* returns length of list */
int credis_llen(REDIS rhnd, const char *key);

/* returns number of elements returned in vector `elementv' */
int credis_lrange(REDIS rhnd, const char *key, int start, int range, char ***elementv);

int credis_lindex(REDIS rhnd, const char *key, int index, char **element);

int credis_lset(REDIS rhnd, const char *key, int index, const char *element);

/* returns number of elements removed */
int credis_lrem(REDIS rhnd, const char *key, int count, const char *element);



/**** Commands operating on sets *********************************************/

/* TODO
 *
 * SADD key member Add the specified member to the Set value at key
 * SREM key member Remove the specified member from the Set value at key
 * SPOP key Remove and return (pop) a random element from the Set value at key
 * SMOVE srckey dstkey member Move the specified member from one Set to another atomically
 * SCARD key Return the number of elements (the cardinality) of the Set at key
 * SISMEMBER key member Test if the specified value is a member of the Set at key
 * SINTER key1 key2 ... keyN Return the intersection between the Sets stored at key1, key2, ..., keyN
 * SINTERSTORE dstkey key1 key2 ... keyN Compute the intersection between the Sets stored at key1, key2, ..., keyN, and store the resulting Set at dstkey
 * SUNION key1 key2 ... keyN Return the union between the Sets stored at key1, key2, ..., keyN
 * SUNIONSTORE dstkey key1 key2 ... keyN Compute the union between the Sets stored at key1, key2, ..., keyN, and store the resulting Set at dstkey
 * SDIFF key1 key2 ... keyN Return the difference between the Set stored at key1 and all the Sets key2, ..., keyN
 * SDIFFSTORE dstkey key1 key2 ... keyN Compute the difference between the Set key1 and all the Sets key2, ..., keyN, and store the resulting Set at dstkey
 * SMEMBERS key Return all the members of the Set value at key
 * SRANDMEMBER key Return a random member of the Set value at key
 */


/**** Multiple databases handling commands ***********************************/

int credis_select(REDIS rhnd, int index);

/* returns -1 if the key was not moved; already present at target 
 * or not found on current db */
int credis_move(REDIS rhnd, const char *key, int index);

int credis_flushdb(REDIS rhnd);

int credis_flushall(REDIS rhnd);



/**** Sorting ***************************************************************/

/* returns number of elements returned in vector `elementv' */
int credis_sort(REDIS rhnd, const char *query, char ***elementv);


/**** Persistence control commands ******************************************/

int credis_save(REDIS rhnd);

int credis_bgsave(REDIS rhnd);

/* returns UNIX time stamp of last successfull save to disk */
int credis_lastsave(REDIS rhnd);

int credis_shutdown(REDIS rhnd);


/**** Remote server control commands *****************************************/

int credis_info(REDIS rhnd, char **info);

int credis_monitor(REDIS rhnd);

/* setting host to NULL and/or port to 0 will turn off replication */
int credis_slaveof(REDIS rhnd, const char *host, int port);

#ifdef __cplusplus
}
#endif

#endif /* __CREDIS_H */
