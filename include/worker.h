#ifndef WORKER_H_
#define WORKER_H_

#include <mqueue.h>

#include "CanHacker.h"

struct can2netJob {
  struct listhead *head;
  CanHacker *canHacker;
  int canSocket;
  struct sockaddr_can *addr;
};

struct net2canJob {
  mqd_t netQueue;
  CanHacker *canHacker;
  int canSocket;
};

void *can2netThread(void *arg);

void *net2canThread(void *arg);

#endif /* #ifndef WORKER_H_ */
