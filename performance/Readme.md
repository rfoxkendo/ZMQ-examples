This directory (performance) contains software that can
gather performance data for the zeroMQ communications patterns.

A few groundrules:
*  None of the software is production quality so expect segfaults if
parameters are missing.
*  Associated with each executable is a script that will gather statistics
into a file for the tcp, ipc and inproc transports.  for tcp, the url used
will be tcp://127.0.0.1:3000 for ipc; ipc:///tmp/comm-type e.g. for req/rep
ipc:///reqrep  similarly for inproc but without the /tmp part of the path.
* In bi-directional communication patterns; two performance measures are done. 
    *   The message size is sent to the receiver and a small response is given.
    *   A small message is sent to the receiver and a message of the specified size, replied.


The programs and their associated automation scripts:

*  pair - pairtimings. Times the pair communications pattern
The pairtimings script writes to pairtimngs.txt. pair usage is:
```bash
pair uri nummsgs size
```
* push - pushtimings. Times the push/pull communications pattern.
The pushtimings script writes to pushtimings.txt. push usage is:
```bash
push uri nummsgs numpullers msgsize
```
* pubsub - pubsubtimings. Times the publication/subscription communication pattern.
The pubsubtimings script writes to pubsubtimings.txt. Usage of the pubusb progfam is:
```bash
pubsub uri nummsgs numsubscribers msgsize
```
*  req - reqtimings - times the req/rep pattern.  reqtimings times many cases and writes to reqtimins.txt
req  usage is:
```bash
req uri nummsgs bigmsgsize
```

