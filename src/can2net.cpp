#include <arpa/inet.h>
#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <mqueue.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <unistd.h>

#include "CanHacker.h"
#include "common.h"
#include "server.h"
#include "worker.h"

const char *cmdlinename = "vcan0";
const int serverPort = 20100;
static volatile int running = 1;

struct listhead head;

CanHacker canHacker;

void sigterm(int signo) { running = 0; }

int initServer() {
  int serverSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (serverSocket == -1) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in adr_srvr;
  memset(&adr_srvr, 0, sizeof adr_srvr);
  adr_srvr.sin_family = AF_INET;
  adr_srvr.sin_port = htons(serverPort);
  adr_srvr.sin_addr.s_addr = INADDR_ANY;

  int opt = 1;
  if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) == -1) {
    perror("setsockopt");
    return -1;
  }

  if (bind(serverSocket, (struct sockaddr *)&adr_srvr, sizeof adr_srvr) == -1) {
    perror("bind(2)");
    return -1;
  }

  if (listen(serverSocket, 10) == -1) {
    perror("listen(2)");
    return -1;
  }

  return serverSocket;
}

int initCan(struct sockaddr_can *addr) {
  int canSocket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (canSocket < 0) {
    perror("socket");
    return -1;
  }

  struct ifreq ifr;
  strcpy(ifr.ifr_name, cmdlinename);

  if (ioctl(canSocket, SIOCGIFINDEX, &ifr) < 0) {
    perror("SIOCGIFINDEX");
    return -1;
  }

  if (!ifr.ifr_ifindex) {
    perror("invalid interface");
    return 1;
  }

  addr->can_family = AF_CAN;
  addr->can_ifindex = ifr.ifr_ifindex;

  if (bind(canSocket, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
    perror("bind");
    return -1;
  }

  return canSocket;
}

int main(int argc, char **argv) {
  signal(SIGTERM, sigterm);
  signal(SIGHUP, sigterm);
  signal(SIGINT, sigterm);

  /* initialize the queue attributes */

  /* create the message queue */
  int err;

  // init queues
  LIST_INIT(&head);

  // init sockets
  struct sockaddr_can addr;
  int canSocket = initCan(&addr);
  if (canSocket == -1) {
    return -1;
  }

  int serverSocket = initServer();
  if (serverSocket == -1) {
    return -1;
  }

  // init threads
  // network rx queue
  struct mq_attr rxAttr;
  rxAttr.mq_flags = 0;
  rxAttr.mq_maxmsg = 10;
  rxAttr.mq_msgsize = CANET_SIZE;
  rxAttr.mq_curmsgs = 0;

  mqd_t netRxQueue =
      mq_open(SERVER_RX_QUEUE_NAME, O_CREAT | O_RDWR, 0644, &rxAttr);
  if (netRxQueue == (mqd_t)-1) {
    perror("mq_open");
    return -1;
  }

  // workers
  struct net2canJob net2canJob;
  net2canJob.netQueue = netRxQueue;
  net2canJob.canHacker = &canHacker;
  net2canJob.canSocket = canSocket;

  pthread_t net2canThreadID;
  err = pthread_create(&net2canThreadID, NULL, &net2canThread, &net2canJob);
  if (err != 0) {
    printf("Can't create net2can thread :[%s]", strerror(err));
  } else {
    printf("net2can thread created successfully\n");
  }

  struct can2netJob can2netJob;
  can2netJob.head = &head;
  can2netJob.canHacker = &canHacker;
  can2netJob.canSocket = canSocket;
  can2netJob.addr = &addr;

  pthread_t can2netThreadID;
  err = pthread_create(&can2netThreadID, NULL, &can2netThread, &can2netJob);
  if (err != 0) {
    printf("Can't create can2net thread :[%s]", strerror(err));
  } else {
    printf("can2net thread created successfully\n");
  }

  struct ServerJob serverJob;
  serverJob.socket = serverSocket;
  serverJob.netRxQueue = netRxQueue;
  serverJob.head = &head;

  pthread_t serverThreadID;
  err = pthread_create(&serverThreadID, NULL, &serverThread, &serverJob);
  if (err != 0) {
    printf("Can't create server thread :[%s]", strerror(err));
  } else {
    printf("Server thread created successfully\n");
  }

  while (running) {
    sleep(1);
  }

  if (pthread_cancel(serverThreadID)) {
    perror("pthread_cancel");
    return -1;
  }

  close(canSocket);
  close(serverSocket);

  if (mq_close(netRxQueue)) {
    perror("mq_close");
    return -1;
  }

  struct entry *eachEntry;
  LIST_FOREACH(eachEntry, &head, entries) {
    if (mq_close(eachEntry->queue)) {
      perror("mq_close");
      return -1;
    }
  }

  return 0;
}
