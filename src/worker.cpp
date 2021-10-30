#include <errno.h>
#include <linux/can.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <unistd.h>

#include "CanHacker.h"
#include "common.h"
#include "worker.h"

void *can2netThread(void *arg) {
  struct can2netJob *job = (struct can2netJob *)arg;
  struct listhead *head = job->head;
  CanHacker *canHacker = job->canHacker;

  int canSocket = job->canSocket;
  struct sockaddr_can *addr = job->addr;

  struct timeval *timeout_current = NULL;

  struct can_frame frame;
  struct iovec iov;
  iov.iov_base = &frame;
  char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];
  struct msghdr msg;
  msg.msg_name = addr;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = &ctrlmsg;

  for(;;) {

    fd_set rdfs;
    FD_ZERO(&rdfs);
    FD_SET(canSocket, &rdfs);

    if ((select(canSocket + 1, &rdfs, NULL, NULL, timeout_current)) <= 0) {
      perror("select");
      // running = 0;
      continue;
    }

    if (FD_ISSET(canSocket, &rdfs)) {

      iov.iov_len = sizeof(frame);
      msg.msg_namelen = sizeof(struct sockaddr_can);
      msg.msg_controllen = sizeof(ctrlmsg);
      msg.msg_flags = 0;

      int nbytes = recvmsg(canSocket, &msg, 0);
      if (nbytes < 0) {
        perror("read");
        return NULL;
      }

      if ((size_t)nbytes != CAN_MTU) {
        fputs("read: incomplete CAN frame\n", stderr);
        return NULL;
      }

      char buffer[CANET_SIZE];
      canHacker->createTransmit(&frame, buffer);
      struct entry *eachEntry;
      LIST_FOREACH(eachEntry, head, entries) {
        if (mq_send(eachEntry->queue, buffer, CANET_SIZE, 0) != 0) {
          perror("mq_send");
          return NULL;
        }
      }
    }

    fflush(stdout);
  }
  return NULL;
}

void *net2canThread(void *arg) {
  struct net2canJob *job = (struct net2canJob *)arg;
  CanHacker *canHacker = job->canHacker;

  char buffer[CANET_SIZE];
  ssize_t bytesRead;

  while ((bytesRead = mq_receive(job->netQueue, buffer, CANET_SIZE, NULL))) {
    struct can_frame frame;

    if (bytesRead < CANET_SIZE) {
      perror("Broken message. Length < 13. Skip");
      return NULL;
    }
    canHacker->parseTransmit(buffer, &frame);

    ssize_t nbytes = write(job->canSocket, &frame, sizeof(struct can_frame));
    if (nbytes < 0) {
      if (errno != ENOBUFS) {
        perror("write");
        return NULL;
      }
      perror("write");
      return NULL;
    } else if (nbytes < (ssize_t)CAN_MTU) {
      fprintf(stderr, "write: incomplete CAN frame\n");
      return NULL;
    }
  }

  return NULL;
}
