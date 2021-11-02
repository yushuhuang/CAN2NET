# CAN2NET

```plaintext
        Can2netThread  (a list of)   ServerThread(Output)
CanBus ---------------> netTxQueue -----------------------> send
        net2canThread                ServerThread(Input)
CanBus <--------------- netRxQueue ------------------------ recv
```

## TODO

1. support for multiple can
2. mv prints to logger
3. rewrite server main thread using select
