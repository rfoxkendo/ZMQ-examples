
This repository are simple examples for the ZMQ communications
system as well as software to obtain timings for it.

### The programs are:

*  pair.cpp - Illlustrates the pair pattern.  Single parameter:  An end point URI.
*  push.cpp - Illustrates the push/pull pattern.  parameters: uri, npullers, nmsgs
*  reqrep.cpp - Illustrates request/reply pattern, parameters uri, nclients, nreplies
*  pubsub.cpp - Illustrates publish/subscribe pattern. Parameters uri, nsubscribers, npublications.

Note:  nanomsg and its related nng have two pattersn that are not directly supported by zmq:
*  bus - everyone can send everyone receives what's sent.
*  survey/respond - a surveyor sends to all responders, some of which may respond.

Note all programs are threaded so that the communicating partners are threads within the program.
Note: For TCP uris at least on my WSL instance on my laptop I need to specify the IP addresses rather than
hostnames e.g. ```tcp://127.0.0.1:3000``` works but ```tcp:localhost:3000``` does not.


See 

[The performance directory](performance/Readme.md) for
information about the timings programs.