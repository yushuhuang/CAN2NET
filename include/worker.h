#ifndef WORKER_H_
#define WORKER_H_

#include "CanHacker.h"
#include <mqueue.h>

struct can2netJob {
  struct listhead *netTxQueues;
  int canSocket;
};

struct net2canJob {
  mqd_t netRxQueue;
  int canSocket;
};

void *can2netThread(void *arg);

void *net2canThread(void *arg);

#endif /* #ifndef WORKER_H_ */
