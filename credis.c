/* credis.c -- a C client library for Redis
 *
 * Copyright (c) 2009-2010, Jonas Romfelt <jonas at romfelt dot se>
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "credis.h"

#define CR_ERROR '-'
#define CR_INLINE '+'
#define CR_BULK '$'
#define CR_MULTIBULK '*'
#define CR_INT ':'

#define CR_BUFFER_SIZE 4096
#define CR_MULTIBULK_SIZE 64

#define CR_VERSION_STRING_SIZE_STR "32"

#ifdef PRINTDEBUG
/* add -DPRINTDEBUG to CPPFLAGS in Makefile for debug outputs */
#define DEBUG(...)                                 \
  do {                                             \
    printf("%s() @ %d: ", __FUNCTION__, __LINE__); \
    printf(__VA_ARGS__);                           \
    printf("\n");                                  \
  } while (0)
#else
#define DEBUG(...)
#endif

typedef struct _cr_buffer {
  char *data;
  int idx;
  int len;
  int size;
} cr_buffer;

typedef struct _cr_multibulk { 
  char **bulks; 
  int size;
  int len; 
} cr_multibulk;

typedef struct _cr_reply {
  int integer;
  char *line;
  char *bulk;
  cr_multibulk multibulk;
} cr_reply;

typedef struct _cr_redis {
  int fd;
  char *ip;
  int port;
  int timeout;
  cr_buffer buf;
  cr_reply reply;
  int error;
} cr_redis;


/* Returns pointer to the '\r' of the first occurence of "\r\n", or NULL
 * if not found */
static char * cr_findnl(char *buf, int len) {
  while (--len >= 0) {
    if (*(buf++) == '\r')
      if (*buf == '\n')
        return --buf;
  }
  return NULL;
}


/* Allocate at least `size' bytes more buffer memory keeping the previously
 * allocated and used memory untouched.
 * Returns:
 *   0  on success
 *  -1  on error, i.e. more memory not available */
static int cr_moremem(cr_buffer *buf, int size)
{
  char *ptr;
  int total, n;

  n = size / CR_BUFFER_SIZE + 1;
  total = buf->size + n * CR_BUFFER_SIZE;

  DEBUG("allocate %d x CR_BUFFER_SIZE, total %d bytes", n, total);

  ptr = realloc(buf->data, total);
  if (ptr == NULL)
    return -1;

  buf->data = ptr;
  buf->size = total;
  return 0;
}


/* Appends a string `str' to the end of buffer `buf'. If available memory
 * in buffer is not enough to hold `str' more memory is allocated to the
 * buffer. 
 * Returns:
 *   0  on success
 *  <0  on error, i.e. more memory not available */
static int cr_appendstr(cr_buffer *buf, const char *str)
{
  int rc, avail;

  /* TODO instead of using formatted print use memcpy() and don't
     blindly add a space before `str' */

  avail = buf->size - buf->len;
  rc = snprintf(buf->data + buf->len, avail, " %s", str);
  if (rc >= avail) {
    DEBUG("truncated, get more memory and try again");
    if (cr_moremem(buf, rc - avail + 1))
      return CREDIS_ERR_NOMEM;
    
    avail = buf->size - buf->len;
    rc = snprintf(buf->data + buf->len, avail, " %s", str);
  }
  buf->len += rc;

  return 0;
}


/* Receives at most `size' bytes from socket `fd' to `buf'. Times out after 
 * `msecs' milliseconds if no data has yet arrived.
 * Returns:
 *  >0  number of read bytes on success
 *   0  server closed connection
 *  -1  on error
 *  -2  on timeout */
static int cr_receivedata(int fd, unsigned int msecs, char *buf, int size)
{
  fd_set fds;
  struct timeval tv;
  int rc;

  tv.tv_sec = msecs/1000;
  tv.tv_usec = (msecs%1000)*1000;

  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  rc = select(fd+1, &fds, NULL, NULL, &tv);

  if (rc > 0)
    return recv(fd, buf, size, 0);
  else if (rc == 0)
    return -2;
  else
    return -1;  
}


/* Sends `size' bytes from `buf' to socket `fd' and times out after `msecs' 
 * milliseconds if not all data has been sent. 
 * Returns:
 *  >0  number of bytes sent; if less than `size' it means that timeout occurred
 *  -1  on error */
static int cr_senddata(int fd, unsigned int msecs, char *buf, int size)
{
  fd_set fds;
  struct timeval tv;
  int rc, sent=0;
  
  /* NOTE: On Linux, select() modifies timeout to reflect the amount 
   * of time not slept, on other systems it is likely not the same */
  tv.tv_sec = msecs/1000;
  tv.tv_usec = (msecs%1000)*1000;

  while (sent < size) {
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    rc = select(fd+1, NULL, &fds, NULL, &tv);

    if (rc > 0) {
      rc = send(fd, buf+sent, size-sent, 0);
      if (rc < 0)
        return -1;
      sent += rc;
    }
    else if (rc == 0) /* timeout */
      break;
    else
      return -1;  
  }

  return sent;
}


/* Buffered read line, returns pointer to zero-terminated string 
 * and length of that string. `start' specifies from which byte
 * to start looking for "\r\n".
 * Returns:
 *  >0  length of string to which pointer `line' refers
 *   0  connection to Redis server was closed
 *  -1  on error, i.e. a string is not available */
static int cr_readln(REDIS rhnd, int start, char **line)
{
  cr_buffer *buf = &(rhnd->buf);
  char *nl;
  int rc, len, avail;

  if (buf->len == 0 || buf->idx >= buf->len) {
    buf->idx = 0;
    buf->len = 0;
  }

  /* TODO check that start doesn't go out of limit */
  while ((nl = cr_findnl(buf->data + buf->idx + start, buf->len - buf->idx)) == NULL) {
    DEBUG("need to read more data");
    avail = buf->size - buf->len;
    if (avail < (CR_BUFFER_SIZE/10)) {
      DEBUG("available buffer memory is low, get more memory");
      if (cr_moremem(buf, 1))
        return CREDIS_ERR_NOMEM;

      avail = buf->size - buf->len;
    }

    rc = cr_receivedata(rhnd->fd, rhnd->timeout, buf->data + buf->len, avail);
    if (rc > 0)
      buf->len += rc;
    else if (rc == 0)
      return 0; /* EOF reached, connection terminated */
    else 
      return -1; /* error */
  }

  *nl = '\0'; /* zero terminate */

  *line = buf->data + buf->idx;
  len = nl - *line;
  buf->idx = (nl - buf->data) + 2;

  DEBUG("<len=%d, idx=%d> %s", buf->len, buf->idx, *line);

  return len;
}


static int cr_receivemultibulk(REDIS rhnd, char *line) 
{
  int bnum, blen, i=0;

  bnum = atoi(line);

  if (bnum > rhnd->reply.multibulk.size) {
    int nsize = (bnum / CR_MULTIBULK_SIZE + 1) * CR_MULTIBULK_SIZE;
    rhnd->reply.multibulk.bulks = realloc(rhnd->reply.multibulk.bulks, nsize);
  }

  for ( ; bnum && cr_readln(rhnd, 0, &line) > 0; bnum--) {
    if (*(line++) != CR_BULK)
      return CREDIS_ERR_PROTOCOL;
    
    blen = atoi(line);
    if (blen == -1)
      rhnd->reply.multibulk.bulks[i++] = NULL;
    else {
      if (cr_readln(rhnd, blen, &line) != blen)
        return CREDIS_ERR_PROTOCOL;
      rhnd->reply.multibulk.bulks[i++] = line;
    }
  }
  
  if (bnum != 0)
    return CREDIS_ERR_PROTOCOL;

  rhnd->reply.multibulk.len = i;
  
  return 0;
}

static int cr_receivebulk(REDIS rhnd, char *line) 
{
  int blen;

  assert(line != NULL);

  blen = atoi(line);
  if (cr_readln(rhnd, blen, &line) >= 0) {
    rhnd->reply.bulk = line;
    return 0;
  }

  return CREDIS_ERR_PROTOCOL;
}


static int cr_receiveinline(REDIS rhnd, char *line) 
{
  rhnd->reply.line = line;
  return 0;
}


static int cr_receiveint(REDIS rhnd, char *line) 
{
  assert(line != NULL);
  rhnd->reply.integer = atoi(line);
  return 0;
}


static int cr_receiveerror(REDIS rhnd, char *line) 
{
  rhnd->reply.line = line;
  return CREDIS_ERR_PROTOCOL;
}


static int cr_receivereply(REDIS rhnd, char recvtype) 
{
  char *line, prefix=0;

  /* reset common send/receive buffer */
  rhnd->buf.len = 0;
  rhnd->buf.idx = 0;

  if (cr_readln(rhnd, 0, &line) > 0) {
    prefix = *(line++);
 
    if (prefix != recvtype && prefix != CR_ERROR)
      /* TODO empty inbuffer to have a clean start before next command */
      return CREDIS_ERR_PROTOCOL;

    switch(prefix) {
    case CR_ERROR:
      return cr_receiveerror(rhnd, line);
    case CR_INLINE:
      return cr_receiveinline(rhnd, line);
    case CR_INT:
      return cr_receiveint(rhnd, line);
    case CR_BULK:
      return cr_receivebulk(rhnd, line);
    case CR_MULTIBULK:
      return cr_receivemultibulk(rhnd, line);
    }   
  }

  return CREDIS_ERR_RECV;
}


static void cr_delete(REDIS rhnd) 
{
  if (rhnd->reply.multibulk.bulks != NULL)
    free(rhnd->reply.multibulk.bulks);
  if (rhnd->buf.data != NULL)
    free(rhnd->buf.data);
  if (rhnd->ip != NULL)
    free(rhnd->ip);
  if (rhnd != NULL)
    free(rhnd);
}


REDIS cr_new(void) 
{
  REDIS rhnd;

  if ((rhnd = calloc(sizeof(cr_redis), 1)) == NULL ||
      (rhnd->ip = malloc(32)) == NULL ||
      (rhnd->buf.data = malloc(CR_BUFFER_SIZE)) == NULL ||
      (rhnd->reply.multibulk.bulks = malloc(sizeof(char *)*CR_MULTIBULK_SIZE)) == NULL) {
    cr_delete(rhnd);
    return NULL;   
  }

  rhnd->buf.size = CR_BUFFER_SIZE;
  rhnd->reply.multibulk.size = CR_MULTIBULK_SIZE;

  return rhnd;
}


/* Send message that has been prepared in message buffer prior to the call
 * to this function. Wait and receive reply. */
static int cr_sendandreceive(REDIS rhnd, char recvtype)
{
  int rc;

  assert(rhnd != NULL);

  DEBUG("Sending message: len=%d, data=%s", rhnd->buf.len, rhnd->buf.data);

  rc = cr_senddata(rhnd->fd, rhnd->timeout, rhnd->buf.data, rhnd->buf.len);

  if (rc != rhnd->buf.len) {
    if (rc < 0)
      return CREDIS_ERR_SEND;
    return CREDIS_ERR_TIMEOUT;
  }

  return cr_receivereply(rhnd, recvtype);
}


/* Prepare message buffer for sending using a printf()-style formatting. */
static int cr_sendfandreceive(REDIS rhnd, char recvtype, const char *format, ...)
{
  int rc;
  va_list ap;
  cr_buffer *buf = &(rhnd->buf);

  assert(format != NULL);

  va_start(ap, format);
  rc = vsnprintf(buf->data, buf->size, format, ap);
  va_end(ap);

  if (rc < 0)
    return -1;

  if (rc >= buf->size) {
    DEBUG("truncated, get more memory and try again");
    if (cr_moremem(buf, rc - buf->size + 1))
      return CREDIS_ERR_NOMEM;

    va_start(ap, format);
    rc = vsnprintf(buf->data, buf->size, format, ap);
    va_end(ap);
  }

  buf->len = rc;

  return cr_sendandreceive(rhnd, recvtype);
}

void credis_close(REDIS rhnd)
{
  if (rhnd->fd > 0)
    close(rhnd->fd);
  cr_delete(rhnd);
}

REDIS credis_connect(const char *host, int port, int timeout)
{
  int fd, yes = 1;
  struct sockaddr_in sa;  
  REDIS rhnd;

  if ((rhnd = cr_new()) == NULL)
    return NULL;

  if (host == NULL)
    host = "127.0.0.1";
  if (port == 0)
    port = 6379;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ||
      setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1 ||
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1)
    goto error;

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_aton(host, &sa.sin_addr) == 0) {
    struct hostent *he = gethostbyname(host);
    if (he == NULL)
      goto error;
    memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
  }

  if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == -1)
    goto error;

  strcpy(rhnd->ip, inet_ntoa(sa.sin_addr));
  rhnd->port = port;
  rhnd->fd = fd;
  rhnd->timeout = timeout;
 
  return rhnd;

 error:
  if (fd > 0)
    close(fd);
  cr_delete(rhnd);
  return NULL;
}

int credis_set(REDIS rhnd, const char *key, const char *val)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "SET %s %d\r\n%s\r\n", 
                            key, strlen(val), val);
}

int credis_get(REDIS rhnd, const char *key, char **val)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "GET %s\r\n", key);

  if (rc == 0)
    *val = rhnd->reply.bulk;

  return rc;
}

int credis_getset(REDIS rhnd, const char *key, const char *set_val, char **get_val)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "GETSET %s %d\r\n%s\r\n", 
                              key, strlen(set_val), set_val);

  if (rc == 0)
    *get_val = rhnd->reply.bulk;

  return rc;
}

int credis_ping(REDIS rhnd) 
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "PING\r\n");
}

int credis_auth(REDIS rhnd, const char *password)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "AUTH %s\r\n", password);
}

int credis_mget(REDIS rhnd, int keyc, const char **keyv, char ***valv)
{
  cr_buffer *buf = &(rhnd->buf);
  int rc, i;

  buf->len = snprintf(buf->data, buf->size, "MGET");
  for (i = 0; i < keyc; i++) {
    if ((rc = cr_appendstr(buf, keyv[i])) != 0)
      return rc;
  }

  if ((rc = cr_appendstr(buf, "\r\n")) != 0)
    return rc;

  if ((rc = cr_sendandreceive(rhnd, CR_MULTIBULK)) == 0) {
    *valv = rhnd->reply.multibulk.bulks;
    rc = rhnd->reply.multibulk.len;
  }

  return rc;
}

int credis_setnx(REDIS rhnd, const char *key, const char *val)
{
  int rc = cr_sendfandreceive(rhnd, CR_INLINE, "SETNX %s %d\r\n%s\r\n", 
                              key, strlen(val), val);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

static int cr_incr(REDIS rhnd, int incr, int decr, const char *key, int *new_val)
{
  int rc = 0;

  if (incr == 1 || decr == 1)
    rc = cr_sendfandreceive(rhnd, CR_INT, "%s %s\r\n", 
                            incr>0?"INCR":"DECR", key);
  else if (incr > 1 || decr > 1)
    rc = cr_sendfandreceive(rhnd, CR_INT, "%s %s %d\r\n", 
                            incr>0?"INCRBY":"DECRBY", key, incr>0?incr:decr);

  if (rc == 0 && new_val != NULL)
    *new_val = rhnd->reply.integer;

  return rc;
}

int credis_incr(REDIS rhnd, const char *key, int *new_val)
{
  return cr_incr(rhnd, 1, 0, key, new_val);
}

int credis_decr(REDIS rhnd, const char *key, int *new_val)
{
  return cr_incr(rhnd, 0, 1, key, new_val);
}

int credis_incrby(REDIS rhnd, const char *key, int incr_val, int *new_val)
{
  return cr_incr(rhnd, incr_val, 0, key, new_val);
}

int credis_decrby(REDIS rhnd, const char *key, int decr_val, int *new_val)
{
  return cr_incr(rhnd, 0, decr_val, key, new_val);
}

int credis_exists(REDIS rhnd, const char *key)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "EXISTS %s\r\n", key);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

int credis_del(REDIS rhnd, const char *key)
{
  /* TODO it should be possible to remove multiple keys 
     at once: DEL key1 key2 ... keyN */
  int rc = cr_sendfandreceive(rhnd, CR_INT, "DEL %s\r\n", key);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

int credis_type(REDIS rhnd, const char *key)
{
  int rc = cr_sendfandreceive(rhnd, CR_INLINE, "TYPE %s\r\n", key);

  if (rc == 0) {
    char *t = rhnd->reply.bulk;
    if (!strcmp("string", t))
      rc = CREDIS_TYPE_STRING;
    else if (!strcmp("list", t))
      rc = CREDIS_TYPE_LIST;
    else if (!strcmp("set", t))
      rc = CREDIS_TYPE_SET;
    else
      rc = CREDIS_TYPE_NONE;
  }

  return rc;
}

int credis_keys(REDIS rhnd, const char *pattern, char ***keyv)
{
  int rc = cr_sendfandreceive(rhnd, CR_MULTIBULK, "KEYS %s\r\n", pattern);

  if (rc == 0) {
    *keyv = rhnd->reply.multibulk.bulks;
    rc = rhnd->reply.multibulk.len;
  }

  return rc;
}

int credis_randomkey(REDIS rhnd, char **key)
{
  int rc = cr_sendfandreceive(rhnd, CR_INLINE, "RANDOMKEY\r\n");

  if (rc == 0) 
    *key = rhnd->reply.line;

  return rc;
}

int credis_rename(REDIS rhnd, const char *key, const char *new_key_name)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "RENAME %s %s\r\n", 
                            key, new_key_name);
}

int credis_renamenx(REDIS rhnd, const char *key, const char *new_key_name)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "RENAMENX %s %s\r\n", 
                              key, new_key_name);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

int credis_dbsize(REDIS rhnd)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "DBSIZE\r\n");

  if (rc == 0) 
    rc = rhnd->reply.integer;

  return rc;
}

int credis_expire(REDIS rhnd, const char *key, int secs)
{ 
  int rc = cr_sendfandreceive(rhnd, CR_INT, "EXPIRE %s %d\r\n", key, secs);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

int credis_ttl(REDIS rhnd, const char *key)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "TTL %s\r\n", key);

  if (rc == 0)
    rc = rhnd->reply.integer;

  return rc;
}

int cr_push(REDIS rhnd, int left, const char *key, const char *val)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "%s %s %d\r\n%s\r\n", 
                              left==1?"LPUSH":"RPUSH", key, strlen(val), val);

  if (rc == 0) 
    rc = rhnd->reply.integer;

  return rc;
}

int credis_rpush(REDIS rhnd, const char *key, const char *val)
{
  return cr_push(rhnd, 0, key, val);
}

int credis_lpush(REDIS rhnd, const char *key, const char *val)
{
  return cr_push(rhnd, 1, key, val);
}

int credis_llen(REDIS rhnd, const char *key)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "LLEN %s\r\n", key);

  if (rc == 0) 
    rc = rhnd->reply.integer;

  return rc;
}

int credis_lrange(REDIS rhnd, const char *key, int start, int end, char ***valv)
{
  int rc;

  if ((rc = cr_sendfandreceive(rhnd, CR_MULTIBULK, "LRANGE %s %d %d\r\n", 
                               key, start, end)) == 0) {
    *valv = rhnd->reply.multibulk.bulks;
    rc = rhnd->reply.multibulk.len;
  }

  return rc;
}

int credis_lindex(REDIS rhnd, const char *key, int index, char **val)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "LINDEX %s %d\r\n", key, index);

  if (rc == 0) 
    *val = rhnd->reply.bulk;

  return rc;
}

int credis_lset(REDIS rhnd, const char *key, int index, const char *val)
{
  return  cr_sendfandreceive(rhnd, CR_INT, "LSET %s %d %s\r\n", key, index, val);
}

int credis_lrem(REDIS rhnd, const char *key, int count, const char *val)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "LREM %s %d %d\r\n", key, count, val);

  if (rc == 0)
    rc = rhnd->reply.integer;

  return rc;
}

static int cr_pop(REDIS rhnd, int left, const char *key, char **val)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "%s %s\r\n", 
                              left==1?"LPOP":"RPOP", key);

  if (rc == 0) 
    *val = rhnd->reply.bulk;

  return rc;
}

int credis_lpop(REDIS rhnd, const char *key, char **val)
{
  return cr_pop(rhnd, 1, key, val);
}

int credis_rpop(REDIS rhnd, const char *key, char **val)
{
  return cr_pop(rhnd, 0, key, val);
}

int credis_select(REDIS rhnd, int index)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "SELECT %d\r\n", index);
}

int credis_move(REDIS rhnd, const char *key, int index)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "MOVE %s %d\r\n", key, index);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      rc = -1;

  return rc;
}

int credis_flushdb(REDIS rhnd)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "FLUSHDB\r\n");
}

int credis_flushall(REDIS rhnd)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "FLUSHALL\r\n");
}

int credis_sort(REDIS rhnd, const char *query, char ***elementv)
{
  int rc;

  if ((rc = cr_sendfandreceive(rhnd, CR_MULTIBULK, "SORT %s\r\n", query)) == 0) {
    *elementv = rhnd->reply.multibulk.bulks;
    rc = rhnd->reply.multibulk.len;
  }

  return rc;
}

int credis_save(REDIS rhnd)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "SAVE\r\n");
}

int credis_bgsave(REDIS rhnd)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "BGSAVE\r\n");
}

int credis_lastsave(REDIS rhnd)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "LASTSAVE\r\n");

  if (rc == 0)
    rc = rhnd->reply.integer;

  return rc;
}

int credis_shutdown(REDIS rhnd)
{
  return cr_sendfandreceive(rhnd, CR_INLINE, "SHUTDOWN\r\n");
}

#define CR_NUMBER_OF_ITEMS 12

int credis_info(REDIS rhnd, REDIS_INFO *info)
{
  int rc = cr_sendfandreceive(rhnd, CR_BULK, "INFO\r\n");

  if (rc == 0) {
    char role[CREDIS_VERSION_STRING_SIZE];
    rc = sscanf(rhnd->reply.bulk,
                "redis_version:%"CR_VERSION_STRING_SIZE_STR"s\r\n"     \
                "uptime_in_seconds:%d\r\n"                             \
                "uptime_in_days:%d\r\n"                                \
                "connected_clients:%d\r\n"                             \
                "connected_slaves:%d\r\n"                              \
                "used_memory:%u\r\n"                                   \
                "changes_since_last_save:%lld\r\n"                     \
                "bgsave_in_progress:%d\r\n"                            \
                "last_save_time:%d\r\n"                                \
                "total_connections_received:%lld\r\n"                  \
                "total_commands_processed:%lld\r\n"                    \
                "role:%"CR_VERSION_STRING_SIZE_STR"s\r\n",
                info->redis_version,
                &(info->uptime_in_seconds),
                &(info->uptime_in_days),
                &(info->connected_clients),
                &(info->connected_slaves),
                &(info->used_memory),
                &(info->changes_since_last_save),
                &(info->bgsave_in_progress),
                &(info->last_save_time),
                &(info->total_connections_received),
                &(info->total_commands_processed),
                role);
    
    if (rc != CR_NUMBER_OF_ITEMS)
      return CREDIS_ERR_PROTOCOL; /* not enough input items returned */
    
    info->role = ((role[0]=='m')?CREDIS_SERVER_MASTER:CREDIS_SERVER_SLAVE);
  }
  
  return rc;
}

int credis_monitor(REDIS rhnd)
{
  return  cr_sendfandreceive(rhnd, CR_INLINE, "MONITOR\r\n");
}

int credis_slaveof(REDIS rhnd, const char *host, int port)
{
  if (host == NULL || port == 0)
    return  cr_sendfandreceive(rhnd, CR_INLINE, "SLAVEOF no one\r\n");
  else
    return  cr_sendfandreceive(rhnd, CR_INLINE, "SLAVEOF %s %d\r\n", host, port);
}


static int cr_setaddrem(REDIS rhnd, const char *cmd, const char *key, const char *member)
{
  int rc = cr_sendfandreceive(rhnd, CR_INT, "%s %s %d\r\n%s\r\n", 
                              cmd, key, strlen(member), member);

  if (rc == 0)
    if (rhnd->reply.integer == 0)
      return -1;

  return rc;
}

int credis_sadd(REDIS rhnd, const char *key, const char *member)
{
  return cr_setaddrem(rhnd, "SADD", key, member);
}

int credis_srem(REDIS rhnd, const char *key, const char *member)
{
  return cr_setaddrem(rhnd, "SREM", key, member);
}

int credis_sismember(REDIS rhnd, const char *key, const char *member)
{
  return cr_setaddrem(rhnd, "SISMEMBER", key, member);
}
