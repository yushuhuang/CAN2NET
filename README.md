
```
         CanRxThread                  Can2netThread  (a list of)   ServerThread(Output)
CanBus ---------------> CanRxQueue -----------------> netTxQueue -----------------------> send
         CanTxThread                  net2canThread                ServerThread(Input)
CanBus <--------------- CanTxQueue <----------------- netRxQueue ------------------------ recv
```

## TODO
1. can we merge CanRxThread and Can2netThread and the other way.
2. support for multiple can