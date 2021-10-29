#include "can.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <mqueue.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>

#include "common.h"

//int messagesSent = 0;

void* canRxThread(void *arg)
{
    struct canRxJob *job = (struct canRxJob *)arg;

    int canSocket = job->socket;
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

    while (1) {

        fd_set rdfs;
        FD_ZERO(&rdfs);
        FD_SET(canSocket, &rdfs);

        if ((select(canSocket+1, &rdfs, NULL, NULL, timeout_current)) <= 0) {
            perror("select");
            //running = 0;
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

            if (mq_send(job->queue, (char *)&frame, CAN_MTU, 0) != 0) {
                perror("mq_send");
                return NULL;
            }
        }

        fflush(stdout);

    }

    return NULL;
}


void* canTxThread(void *arg)
{
    struct canTxJob *job = (struct canTxJob *)arg;

    struct can_frame frame;
    ssize_t bytes_read;

    while ((bytes_read = mq_receive(job->queue, (char *)&frame, CAN_MTU, NULL))) {

        ssize_t nbytes = write(job->socket, &frame, sizeof(struct can_frame));
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
