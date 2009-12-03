/* credis-test.c -- a sample test application using credis (C client library 
 * for Redis)
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "credis.h"


long timer(int reset) 
{
  static long start=0; 
  struct timeval tv;

  gettimeofday(&tv, NULL);

  /* return timediff */
  if (!reset) {
    long stop = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;
    return (stop - start);
  }

  /* reset timer */
  start = ((long)tv.tv_sec)*1000 + tv.tv_usec/1000;

  return 0;
}


int main(int argc, char **argv) {
  REDIS redis = credis_connect(NULL, 0, 2000);
  char *val;
  int rc;

  if (argc == 2) {
    int i;
    long t;
    int num = atoi(argv[1]);
    printf("Sending %d 'set' commands ...\n", num);
    timer(1);
    for (i=0; i<num; i++) {
      if (credis_set(redis, "kalle", "qwerty") != 0)
        printf("get returned error\n");
    }
    t = timer(0);
    printf("done! Took %.3f seconds, that is %ld commands/second\n", ((float)t)/1000, (num*1000)/t);
    exit(0);
  }

  printf("Testing a number of credis functions. To perform a simplistic set-command\n"\
         "benchmark run `%s <num>', where <num> is the number\n"\
         "of set-commands to send.\n\n", argv[0]);

  rc = credis_ping(redis);
  printf("ping returned: %d\n", rc);

  rc = credis_set(redis, "kalle", "kula");
  printf("set kalle=kula returned: %d\n", rc);

  rc = credis_get(redis, "kalle", &val);
  printf("get kalle returned: %s\n", val);

  rc = credis_getset(redis, "kalle", "buhu", &val);
  printf("getset kalle=buhu returned: %s\n", val);

  rc = credis_get(redis, "kalle", &val);
  printf("get kalle returned: %s\n", val);

  rc = credis_set(redis, "adam", "aaa");
  rc = credis_set(redis, "bertil", "bbbbbbb");
  rc = credis_set(redis, "caesar", "cccc");
  rc = credis_get(redis, "adam", &val);
  printf("get adam returned: %s\n", val);
  rc = credis_get(redis, "bertil", &val);
  printf("get bertil returned: %s\n", val);
  rc = credis_get(redis, "caesar", &val);
  printf("get caesar returned: %s\n", val);

  rc = credis_info(redis, &val);
  printf("info returned %d\n", rc);

  rc = credis_lastsave(redis);
  printf("lastsave returned: %d\n", rc);

  credis_close(redis);

  return 0;
}
