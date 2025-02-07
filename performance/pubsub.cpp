/** 
 * Times publish/subscribe.
 * Note that since subscription filtering is done in the 
 * subscriber, after the message has been received, we just
 * subscsribe to all messages.
 * 
 * Usage:
 *    pubsub uri nummsgs numsubscribers size
 * 
 * Where:
 *    uri - is the communications endpoint URI.
 *    nummsgs - are the minimum number of publications that will be done.
 *    numsubscdribers - the number of subscsribers to spin off.
 *    size - the size of the published message.
 * 
 * @note this is not production quality code so a missing parameter will probably result
 * in a segfault and a stupidly set one undefined behavior (e.g. numsubscsribers <- 0)
 * 
 * Therefore termination is done very much like push; where the publisher is the
 * pusher and the subscribers are the pullers:
 * 
 * - The publisher sends nummsgs to whomever gets them.
 * - After this it sends messages with the first byte nonzero
 * which is interpreted as a done message by the subscribers.
 * THe publisher, in that loop polls for the done latch to indicate
 * all subscriblers are done.    When the done latch is satisfied,
 * the publisher finishes timing and arrives at the exit latch.
 * when the exit latch is done everything is torn down and
 * reports are made.
 * - When the subscribers see a message that has the first byte nonzero,
 * it sets the done latch and, polls for done latch completion
 * in a loop that does reads with no waits.
 * - Afer the done latch is set it arrives at the exit latch.
 * 
 * I think this will work with no deadlocks and the number of
 * messages (including the number of done messages sent) will 
 * be timed.  Timing will start prior to the first message and
 * end when the done latch was set; as that indicates that all
 * sent messages that can be received have been.
 * 
 * @note - observationally, with high rates of pub/sub on sockets (unix and tcp), 
 * delivery seems to be pretty lossy.
 * 
 */

#include <thread>
#include <latch>
#include <zmq.h>
#include <iostream>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <chrono>

// check error for int returns.
static int checkError(int status, const char* doing) {
    if (status < 0) {
        std::cerr << "Failed " << doing << " " 
            << zmq_strerror(zmq_errno()) << std::endl;
        exit(EXIT_FAILURE);
    }
    return status;
}
// check error for pointer returns:

static void* checkError(void* p, const char* doing) {
    if (!p) {
        std::cerr << "Failed " << doing << " "
            << zmq_strerror(zmq_errno()) << std::endl;
        exit(EXIT_FAILURE);
    }
    return p;
}

/**
 * send a message (not necessarily a string) to the 
 * peer:
 * 
 * @param socket - socket to carry the message.
 * @param msg    - Pointer to the message.
 * @param nBytes - size of the message
 */
static void
send(void* socket, void* data, size_t len) {
    checkError(
        zmq_send(socket, data, len, 0),
        "Sending data on socket."
    );
}
/**
 * ignoremsg
 *    Receive a message and ignore it.
 * 
 * @param socket - socket that receives the message.
 * @param flags - flags for recvmsg - defaults to zero.
 * @return int - value of the first byte of the message.
 * @note - we ensure the message is a single part message.
 * @note we allow errnos ofor EAGAIN but then the return
 * value is 0.
 */
static int
ignore(void* socket, int flags = 0) {
    zmq_msg_t msg;
    checkError(zmq_msg_init(&msg), "Initializing message");

    int status = zmq_recvmsg(socket, &msg, flags);
    if (status < 0 && zmq_errno() == EAGAIN) {
        return 0;
    }
    checkError(
        status,
        "Receiving message part."
    );
    const uint8_t* pData = reinterpret_cast<uint8_t*>(checkError(
        zmq_msg_data(&msg),
        "Getting message data pointer"
    ));
    int result = *pData;
    checkError(zmq_msg_close(&msg), "Freeing message"); // free msg
    int more;
    size_t morelen(sizeof(more));
    checkError(
        zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &morelen),
        "Failed to get more flag"
    );
    if (more) {
        std::cerr << "Thought I was getting a single part message, got a multipart!\n";
        exit(EXIT_FAILURE);
    }
    return result;
}
/**
 *  setBuffering
 *    Set send/receive buffers to 2MBytes.
 * @param socket
 * 
 */
static void
setBuffering(void* socket) {
    int maxSize = 1024*1024*2;     // 2mbytes.
    checkError(
        zmq_setsockopt(socket, ZMQ_SNDBUF, &maxSize, sizeof(int)),
        "Setting send buffer size"
    );
    checkError(
        zmq_setsockopt(socket, ZMQ_RCVBUF, &maxSize, sizeof(int)),
        "Setting receive buffer size"
    );
    // They claim we should not need this but...

    int64_t maxMsg = 1024*1024*2;
    checkError(
        zmq_setsockopt(socket, ZMQ_MAXMSGSIZE, &maxMsg, sizeof(maxMsg)),
        "Setting max message size"
    );
    
}


/**
 *  subscriber:
 *     -  Set up the subscription to the publisher.
 *     - Process messages until we see an end message.
 *     - Do the dance to handle shutdown and exit.
 * @param uri - URI that represents the communication end point.
 * @param ctx - ZMQ context object pointer.
 * @param done - references a latch that we will signal when we get the done message
 * @param exitlatch - references a latch that we will signal to know when it's ok to
 * tear down the subscription and exit.
 * @note  This function is normally a thread.
 */
static void
subscriber(std::string uri, void* ctx, std::latch& done, std::latch& exitlatch) {
    // set up as a subscriber:

    auto socket = checkError(
        zmq_socket(ctx, ZMQ_SUB),
        "Creating subscriber socket."
    );
    setBuffering(socket);
    const char* sub = "";
    checkError(
        zmq_setsockopt(socket, ZMQ_SUBSCRIBE, sub, 0),
        "Setting up subscription"
    );
    checkError(
        zmq_connect(socket, uri.c_str()), 
        "Connecting to publisher."
    );

    // Get messages until there's a non-zero first byte:
    int got(0);
    while(ignore(socket) == 0) {
        got++;
    }
    // start the dance to complete..signal done and recieve
    // until all have done that:

    done.count_down();
    while(!done.try_wait()) {
        ignore(socket, ZMQ_DONTWAIT);   // Ignore messages until all are done.
    }

    // at this point no more messges will be sent so we can:

    exitlatch.arrive_and_wait();      // All, including the sender are here.

    // shutdown:

    checkError(
        zmq_close(socket),
        "Closing subscsriber socket."
    );



}
/**
 *  main - the publisher.
 */
int main(int argc, char** argv) {
    std::string uri(argv[1]);
    int minmsgs = atoi(argv[2]);
    int numsubs = atoi(argv[3]);
    int msgsize = atoi(argv[4]);

    // Set up ZMQ and the publication socket>

    auto context = checkError(
        zmq_ctx_new(),
        "Creating ZMQ context"
    );
    auto socket = checkError(
        zmq_socket(context, ZMQ_PUB),
        "Creating publication socket."
    );
    setBuffering(socket);
    checkError(
        zmq_bind(socket, uri.c_str()), 
        "Binding the publisher to the endpoint"
    );
    // Now we can start the subscsribers.

    std::latch  done(numsubs);
    std::latch  exitlatch(numsubs+1);
    std::vector<std::thread*> subscribers;
    for (int i =0; i < numsubs; i++) {
        subscribers.push_back(
            new std::thread(subscriber, uri, context, std::ref(done), std::ref(exitlatch))
        );
    }
    sleep(1);                       // Wait for them all to start.

    // Time the sends until all subscribers are ready to exit:

    int sent(0);
    char* msg = new char[msgsize];
    *msg = 0;                                   // Not a done.

    auto start = std::chrono::high_resolution_clock::now();
    for (int i =0; i < minmsgs; i++) {
        send(socket, msg, msgsize);
        sent++;
    }
    // end messages until donlatch is readh:

    *msg = 0xff;                              // done mesg.
    while(!done.try_wait()) {
        send(socket, msg, msgsize);
        sent++;
    }
    auto end = std::chrono::high_resolution_clock::now();  // All msgs received.

    // Synchronize the shutdown of the threads:

    exitlatch.arrive_and_wait();
    delete []msg;
        for (auto p : subscribers) {
        p->join();
        delete p;
    }

    /// Subscriber sockets are now closed.

    checkError(
        zmq_close(socket),
        "Closing publication sockewt"
    );
    checkError(
        zmq_ctx_term(context),
        "Terminating ZMQ Context."
    );


    // report the results.

    auto duration = end - start;
    double ms = (double)(std::chrono::duration_cast<std::chrono::milliseconds>(duration)
        .count());
    double secs = ms/1000.0;
    double kb   = double(sent)*double(msgsize)/1024.0;

    std::cout << "Seconds:   " << secs << std::endl;
    std::cout << "Pubs:      " << sent << std::endl;
    std::cout << "Msgs/sec:  " << (double)sent/secs << std::endl;
    std::cout << "kb/sec:    " << kb/secs << std::endl;

    return EXIT_SUCCESS;

}