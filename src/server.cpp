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
  struct listhead *head = serverJob->head;

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
    LIST_INSERT_HEAD(head, newEntry, entries);

    struct netTxJob *txJob = (struct netTxJob *)malloc(sizeof(struct netTxJob));
    txJob->socket = clientSocket;
    txJob->queue = netTxQueue;

    pthread_t txThreadId;
    if (pthread_create(&txThreadId, NULL, outputConnectionHandler,
                       (void *)txJob) < 0) {
      perror("could not create thread for outputConnectionHandler");
      return NULL;
    }

    // network rx
    struct netRxJob *rxJob = (struct netRxJob *)malloc(sizeof(struct netRxJob));
    rxJob->socket = clientSocket;
    rxJob->queue = serverJob->netRxQueue;

    pthread_t rxThreadId;
    if (pthread_create(&rxThreadId, NULL, inputConnectionHandler,
                       (void *)rxJob) < 0) {
      perror("could not create thread for inputConnectionHandler");
      return NULL;
    }

    // Now join the thread , so that we dont terminate before the thread
    // pthread_join( thread_id , NULL);
    puts("Handler assigned");
  }

  return NULL;
}

void *inputConnectionHandler(void *job_ptr) {
  struct netRxJob *job = (struct netRxJob *)job_ptr;

  char clientMessage[CANET_SIZE];

  ssize_t readSize;
  while ((readSize = recv(job->socket, clientMessage, CANET_SIZE, 0)) > 0) {
    if (mq_send(job->queue, clientMessage, readSize, 0) != 0) {
      perror("mq_send");
      return NULL;
    }
  }

  /*if (read_size == 0) {
      puts("Client disconnected");
      fflush(stdout);
  } else if(read_size == -1) {
      perror("recv failed");
  }*/

  if (close(job->socket)) {
    perror("close");
    return NULL;
  }

  free(job_ptr);

  return NULL;
}

void *outputConnectionHandler(void *job_ptr) {
  struct netTxJob *job = (struct netTxJob *)job_ptr;

  char clientMessage[CANET_SIZE];

  ssize_t bytes_read;
  while (
      (bytes_read = mq_receive(job->queue, clientMessage, CANET_SIZE, NULL))) {
    if (bytes_read == -1) {
      perror("mq_receive error");
      return NULL;
    }
    puts("netTx: read message from queue");
    write(job->socket, clientMessage, bytes_read);
  }

  if (mq_close(job->queue)) {
    perror("mq_close");
    return NULL;
  }

  if (close(job->socket)) {
    perror("close");
    return NULL;
  }

  free(job_ptr);

  return NULL;
}
