#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "server.h"

void *serverThread(void *arg) {
  struct ServerJob *serverJob = (struct ServerJob *)arg;
  int serverSocket = serverJob->socket;
  struct listhead *netTxQueues = serverJob->netTxQueues;

  int serverTxQueueCount = 0;
  struct sockaddr_in clientSocketAddress;
  int clientSocket;
  size_t socketAddrLength = sizeof clientSocketAddress;
  while ((clientSocket =
              accept(serverSocket, (struct sockaddr *)&clientSocketAddress,
                     (socklen_t *)&socketAddrLength))) {
    if (clientSocket < 0) {
      perror("accept failed");
      return NULL;
    }

    puts("Connection accepted");

    // network tx
    struct mq_attr txAttr;
    txAttr.mq_flags = 0;
    txAttr.mq_maxmsg = 10;
    txAttr.mq_msgsize = CANET_SIZE;
    txAttr.mq_curmsgs = 0;

    char serverTxQueueName[80];
    sprintf(serverTxQueueName, SERVER_TX_QUEUE_NAME, serverTxQueueCount++);
    mqd_t netTxQueue =
        mq_open(serverTxQueueName, O_CREAT | O_RDWR, 0644, &txAttr);
    if (netTxQueue == (mqd_t)-1) {
      perror("opening serverTxQueue");
      return NULL;
    }

    struct entry *newEntry = (struct entry *)malloc(sizeof(struct entry));
    newEntry->queue = netTxQueue;
    LIST_INSERT_HEAD(netTxQueues, newEntry, entries);

    struct netTxJob *txJob = (struct netTxJob *)malloc(sizeof(struct netTxJob));
    txJob->socket = clientSocket;
    txJob->txQueue = netTxQueue;

    pthread_t txThreadId;
    if (pthread_create(&txThreadId, NULL, outputConnectionHandler,
                       (void *)txJob) < 0) {
      perror("creating outputConnectionHandler");
      return NULL;
    }

    // network rx
    struct netRxJob *rxJob = (struct netRxJob *)malloc(sizeof(struct netRxJob));
    rxJob->socket = clientSocket;
    rxJob->rxQueue = serverJob->netRxQueue;
    rxJob->txQueue = netTxQueue;
    rxJob->txQueueElm = newEntry;
    rxJob->txThreadId = txThreadId;
    rxJob->txJob = txJob;

    pthread_t rxThreadId;
    if (pthread_create(&rxThreadId, NULL, inputConnectionHandler,
                       (void *)rxJob) < 0) {
      perror("creating inputConnectionHandler");
      return NULL;
    }

    puts("Handlers assigned");
  }

  return NULL;
}

void *inputConnectionHandler(void *job_ptr) {
  struct netRxJob *rxjob = (struct netRxJob *)job_ptr;

  char clientMessage[CANET_SIZE];
  ssize_t msg_size;
  while ((msg_size = recv(rxjob->socket, clientMessage, CANET_SIZE, 0)) > 0) {
    if (mq_send(rxjob->rxQueue, clientMessage, msg_size, 0) != 0) {
      perror("inputConnection, sending to netRxQueue");
      break;
    }
  }

  // FIXME
  // should we mutex lock entry

  if (msg_size < 0)
    perror("inputConnection, receiving from client");
  if (msg_size == 0)
    puts("Client disconnected");

  // cleanup
  if (mq_close(rxjob->txQueue) < 0)
    perror("inputConnection, closing netTxQueue");

  if (pthread_cancel(rxjob->txThreadId) != 0)
    perror("inputConnection, canceling outputConnection thread");

  if (close(rxjob->socket) < 0)
    perror("inputConnection, closing client socket");

  LIST_REMOVE(rxjob->txQueueElm, entries);
  free(rxjob->txQueueElm);
  free(rxjob->txJob);
  free(job_ptr);
  return NULL;
}

void *outputConnectionHandler(void *job_ptr) {
  struct netTxJob *job = (struct netTxJob *)job_ptr;

  char clientMessage[CANET_SIZE];
  ssize_t msg_size;
  while (
      (msg_size = mq_receive(job->txQueue, clientMessage, CANET_SIZE, NULL))) {
    if (msg_size < 0) {
      perror("outputConnection, receiving from netTxQueue");
      break;
    }
    if (write(job->socket, clientMessage, msg_size) < 0)
      perror("outputConnection, sending to client");
  }

  if (mq_close(job->txQueue) < 0)
    perror("outputConnection, closing netTxQueue");

  if (close(job->socket) < 0)
    perror("outputConnection, closing client socket");

  free(job_ptr);
  return NULL;
}
