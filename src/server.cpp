#include "server.h"
#include "common.h"

void *inputConnectionHandler(void *job_ptr) {
  struct netRxJob *rxjob = (struct netRxJob *)job_ptr;

  char clientMessage[CANET_SIZE];
  ssize_t msg_size;
  while ((msg_size = read(rxjob->socket, clientMessage, CANET_SIZE)) > 0) {
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

  pthread_mutex_lock(&mutex);
  LIST_REMOVE(rxjob->txQueueElm, entries);
  pthread_mutex_unlock(&mutex);

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
