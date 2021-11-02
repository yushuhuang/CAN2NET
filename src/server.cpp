#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
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
      perror("mq_open");
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
      perror("could not create thread for outputConnectionHandler");
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
      perror("could not create thread for inputConnectionHandler");
      return NULL;
    }

    puts("Handler assigned");
  }

  return NULL;
}

void *inputConnectionHandler(void *job_ptr) {
  struct netRxJob *job = (struct netRxJob *)job_ptr;

  char clientMessage[CANET_SIZE];

  ssize_t readSize;
  while ((readSize = recv(job->socket, clientMessage, CANET_SIZE, 0)) > 0) {
    if (mq_send(job->rxQueue, clientMessage, readSize, 0) != 0) {
      perror("mq_send");
      return NULL;
    }
  }

  // FIXME
  // should we mutex lock entry

  // Client disconnected
  if (readSize <= 0) {
    if (readSize < 0)
      perror("recv from client failed");
    puts("Client disconnected");
    if (mq_close(job->txQueue) < 0) {
      perror("mq_close for txqueue");
    }
    LIST_REMOVE(job->txQueueElm, entries);
    pthread_cancel(job->txThreadId);
    free(job->txJob);
    return NULL;
  }

  if (close(job->socket) < 0) {
    perror("close input client socket");
    return NULL;
  }
  free(job_ptr);
  return NULL;
}

void *outputConnectionHandler(void *job_ptr) {
  struct netTxJob *job = (struct netTxJob *)job_ptr;

  char clientMessage[CANET_SIZE];

  ssize_t bytes_read;
  while ((bytes_read =
              mq_receive(job->txQueue, clientMessage, CANET_SIZE, NULL))) {
    if (bytes_read < 0) {
      perror("mq_receive error");
      return NULL;
    }
    write(job->socket, clientMessage, bytes_read);
    puts("sending message");
  }

  if (mq_close(job->txQueue) < 0) {
    perror("mq_close");
    return NULL;
  }
  if (close(job->socket) < 0) {
    perror("close output client socket");
    return NULL;
  }
  free(job_ptr);
  return NULL;
}
