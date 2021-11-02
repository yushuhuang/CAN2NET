#include "worker.h"
#include "CanHacker.h"
#include "common.h"

void *can2netThread(void *arg) {
  struct can2netJob *job = (struct can2netJob *)arg;

  struct can_frame frame;
  ssize_t nbytes;
  while (nbytes = read(job->canSocket, &frame, sizeof(struct can_frame))) {
    if (nbytes < 0) {
      perror("can2netThread, read from can");
      return NULL;
    }
    if ((size_t)nbytes != CAN_MTU) {
      fputs("read from can: incomplete CAN frame\n", stderr);
      continue;
    }

    char buffer[CANET_SIZE];
    createTransmit(&frame, buffer);

    struct entry *eachEntry;
    LIST_FOREACH(eachEntry, job->netTxQueues, entries) {
      if (mq_send(eachEntry->queue, buffer, CANET_SIZE, 0) != 0) {
        perror("mq_send for netTxQueues");
        return NULL;
      }
    }
  }
  return NULL;
}

void *net2canThread(void *arg) {
  struct net2canJob *job = (struct net2canJob *)arg;

  char buffer[CANET_SIZE];
  ssize_t bytesRead;
  while ((bytesRead = mq_receive(job->netRxQueue, buffer, CANET_SIZE, NULL))) {
    if (bytesRead < 0) {
      perror("net2canThread, read from mq_receive");
      return NULL;
    }
    if (bytesRead < CANET_SIZE) {
      perror("read from mq_receive, broken message. Length < 13");
      continue;
    }

    struct can_frame frame;
    parseTransmit(buffer, &frame);

    ssize_t nbytes = write(job->canSocket, &frame, sizeof(struct can_frame));
    if (nbytes < 0) {
      perror("Error writing to canSocket");
      return NULL;
    } else if (nbytes < (ssize_t)CAN_MTU) {
      fprintf(stderr, "write: incomplete CAN frame\n");
      return NULL;
    }
  }

  return NULL;
}
