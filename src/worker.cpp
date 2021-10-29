#include <sys/queue.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <linux/can.h>

#include "common.h"
#include "worker.h"
#include "CanHacker.h"

void *can2netThread(void *arg)
{
    struct can2netJob *job = (struct can2netJob *)arg;
    struct listhead *head = job->head;
    CanHacker *canHacker = job->canHacker;

    struct can_frame frame;
    ssize_t bytes_read;

    while ((bytes_read = mq_receive(job->canQueue, (char *)&frame, CAN_MTU, NULL))) {

        puts("can2net: read frame from queue");

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

    return NULL;
}

void *net2canThread(void *arg)
{
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

        if (mq_send(job->canQueue, (char *)&frame, CAN_MTU, 0) != 0) {
            perror("mq_send");
            return NULL;
        }
    }

    return NULL;
}
