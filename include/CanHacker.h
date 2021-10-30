#ifndef CANHACKER_H_
#define CANHACKER_H_

#include "common.h"
#include <linux/can.h>

class CanHacker {
public:
  CanHacker();
  virtual ~CanHacker();

  int parseTransmit(char (&buffer)[CANET_SIZE], struct can_frame *frame);
  int createTransmit(struct can_frame *frame, char (&buffer)[CANET_SIZE]);
};

#endif /* CANHACKER_H_ */