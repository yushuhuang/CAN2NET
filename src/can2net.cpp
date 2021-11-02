#include "common.h"
#include "server.h"
#include "worker.h"
#include <linux/can/raw.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>

const char *can_id = "vcan0";
const int serverPort = 20100;

struct listhead netTxQueues;

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
  strcpy(ifr.ifr_name, can_id);

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

int main() {
  int err;

  // init queues
  LIST_INIT(&netTxQueues);

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

  // worker threads
  struct net2canJob net2canJob;
  net2canJob.netRxQueue = netRxQueue;
  net2canJob.canSocket = canSocket;

  pthread_t net2canThreadID;
  err = pthread_create(&net2canThreadID, NULL, &net2canThread, &net2canJob);
  if (err != 0) {
    printf("Can't create net2can thread :[%s]", strerror(err));
  } else {
    printf("net2can thread created\n");
  }

  struct can2netJob can2netJob;
  can2netJob.netTxQueues = &netTxQueues;
  can2netJob.canSocket = canSocket;

  pthread_t can2netThreadID;
  err = pthread_create(&can2netThreadID, NULL, &can2netThread, &can2netJob);
  if (err != 0) {
    printf("Can't create can2net thread :[%s]", strerror(err));
  } else {
    printf("can2net thread created\n");
  }

  // server main thread
  int serverTxQueueCount = 0;
  struct sockaddr_in clientSocketAddress;
  int clientSocket;
  size_t socketAddrLength = sizeof clientSocketAddress;
  while ((clientSocket =
              accept(serverSocket, (struct sockaddr *)&clientSocketAddress,
                     (socklen_t *)&socketAddrLength))) {
    if (clientSocket < 0) {
      perror("accept failed");
      break;
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
      break;
    }

    struct entry *newEntry = (struct entry *)malloc(sizeof(struct entry));
    newEntry->queue = netTxQueue;
    LIST_INSERT_HEAD(&netTxQueues, newEntry, entries);

    struct netTxJob *txJob = (struct netTxJob *)malloc(sizeof(struct netTxJob));
    txJob->socket = clientSocket;
    txJob->txQueue = netTxQueue;

    pthread_t txThreadId;
    if (pthread_create(&txThreadId, NULL, outputConnectionHandler,
                       (void *)txJob) < 0) {
      perror("creating outputConnectionHandler");
      break;
    }

    // network rx
    struct netRxJob *rxJob = (struct netRxJob *)malloc(sizeof(struct netRxJob));
    rxJob->socket = clientSocket;
    rxJob->rxQueue = netRxQueue;
    rxJob->txQueue = netTxQueue;
    rxJob->txQueueElm = newEntry;
    rxJob->txThreadId = txThreadId;
    rxJob->txJob = txJob;

    pthread_t rxThreadId;
    if (pthread_create(&rxThreadId, NULL, inputConnectionHandler,
                       (void *)rxJob) < 0) {
      perror("creating inputConnectionHandler");
      break;
    }

    puts("Handlers assigned");
  }

  close(canSocket);
  close(serverSocket);

  if (mq_close(netRxQueue)) {
    perror("mq_close");
    return -1;
  }

  struct entry *eachEntry;
  LIST_FOREACH(eachEntry, &netTxQueues, entries) {
    if (mq_close(eachEntry->queue)) {
      perror("mq_close");
      return -1;
    }
  }

  return 0;
}
