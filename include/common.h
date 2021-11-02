#ifndef COMMON_H_
#define COMMON_H_

#include <errno.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>

#define CAN_TX_QUEUE_NAME "/cantxqueue"
#define CAN_RX_QUEUE_NAME "/canrxqueue"
#define SERVER_RX_QUEUE_NAME "/serverrxqueue"
#define SERVER_TX_QUEUE_NAME "/servertxqueue-%d"

#define CANET_SIZE 13

extern pthread_mutex_t mutex;

LIST_HEAD(listhead, entry);
struct entry {
  mqd_t queue;
  LIST_ENTRY(entry) entries;
};

#endif /* #ifndef COMMON_H_ */
