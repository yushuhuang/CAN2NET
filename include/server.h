#ifndef SERVER_H_
#define SERVER_H_

#include <mqueue.h>

struct netRxJob {
  int socket;
  mqd_t rxQueue;
  mqd_t txQueue;
  struct entry *txQueueElm;
  pthread_t txThreadId;
  struct netTxJob *txJob;
};

struct netTxJob {
  int socket;
  mqd_t txQueue;
};

struct ServerJob {
  int socket;
  mqd_t netRxQueue;
  struct listhead *netTxQueues;
};

void *serverThread(void *arg);

void *inputConnectionHandler(void *job_ptr);
void *outputConnectionHandler(void *job_ptr);

#endif /* SERVER_H_ */
