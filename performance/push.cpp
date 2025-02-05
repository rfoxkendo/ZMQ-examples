/**
 *  push.cpp  
 *    Contains timing code for the push/pull pattern.
 * This pattern is a single sender muti-receiver pattern.
 * As such it's useful to time this for a range of receivers.
 * Therefor, usage is:
 * 
 *     push uri nummsgs numclients msgSize
 * Where:
 *   uri - is  the communications endoint URI.
 *   nummsgs - is  the minimum number of messagse that will be pushed
 *            (see completion below).
 *  numclients - is the number of clients that can receive pushes.
 *  msgsize - is the size of all messages that will be pushed.
 *
 * @note this is not production code so not specifying all parameters
 *    probably results in a segfault.
 *  
 * Completion:
 *    The push example in the directory above pointed out some
 * interesting issues with how to synchronize exit. Specifically,
 * when pullers started just waiting on a barrier, the pusher
 * could wind up blocking with full output queues when sending
 * exit messages.  Therefore;
 * 
 * - The pusher sends nummsgs to whomever gets them.
 * - After this it sends messages with the first byte nonzero
 * which is interpreted as a done message by the pullers.
 * THe pusher, in that loop polls for the done latch to indicate
 * all pulllers are done.    When the done latch is satisfied,
 * the pusher finishes timing and arrives at the exit latch.
 * when the exit latch is done everything is torn down and
 * reports are made.
 * - When the pullers see a message that has the first byte nonzero,
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
 * puller
 *    Thread that is one puller.
 * 
 * @param uri - URI of the communictaionts endpoint.
 * @param ctx - ZMQ shared context.
 * @param done - Latch to signal when we've got the 'first' done msg.
 * @param exitlatch - Latch to signel we're ready to teardown.
 */
static void 
puller(std::string uri, void* ctx, std::latch& done, std::latch& exitlatch) {
     // Set up to pull from  uri

     void * socket = checkError(
        zmq_socket(ctx, ZMQ_PULL),
        "Creating pull socket."
     );
     setBuffering(socket);
     checkError(
        zmq_connect(socket, uri.c_str()), 
        "Connecting to pusher."
     );  

     // Receieve messages with wait until the done message.
     while(ignore(socket) == 0) {

     }
     done.count_down();   // We're done.

    // Recieve/drop messgaes with no wait until 
    // done...ignore errors on the rcvmsg.

     while(!done.try_wait()) {
        ignore(socket, ZMQ_DONTWAIT);
     }
     // Ready to tear down when everyone else is:

     exitlatch.arrive_and_wait();

     checkError(
        zmq_close(socket),
        "Closing pull socket."
     );
}

// entry point, main is the pusher.

int main (int argc, char**argv) {
    std::string uri(argv[1]);
    int nummsgs = atoi(argv[2]);
    int numclients = atoi(argv[3]);
    int msgsize = atoi(argv[4]);

    // Set up the pusher:

    auto ctx = checkError(
        zmq_ctx_new(),
        "Creating context"
    );
    auto socket = checkError(
        zmq_socket(ctx, ZMQ_PUSH),
        "Creating push socket"
    );
    setBuffering(socket);
    checkError(
        zmq_bind(socket, uri.c_str()),
        "Binding push to URI"
    );

    // start the threads.

    std::latch done(numclients);
    std::latch exitlatch(numclients+1);   //pusher waits here too.
    std::vector<std::thread*> pullers;
    for (int i =0; i < numclients; i++) {
        pullers.push_back(
            new std::thread(puller, uri, ctx, std::ref(done), std::ref(exitlatch))
        );
    }

    char* message = new char[msgsize];
    *message = 0;       // not an exit msg.
    int sent(0);        // total sends.
    // start timing and sending messages:

    auto start = std::chrono::high_resolution_clock::now();
    while(sent < nummsgs) {    // Non exit messages
        send(socket, message, msgsize);
        sent++;
    }
    // send done messages until the done latch is satisfied:
    *message = 0xff;         // Done messages.
    while(!done.try_wait()) {
        send(socket, message, msgsize);
        sent++;               // count these too.
    }
    auto end = std::chrono::high_resolution_clock::now();
    exitlatch.arrive_and_wait();      // Wait for all of us before tearing down:

    // Tear down the communications:

    checkError(
        zmq_close(socket),
        "Tearing down the push socket"
    );
    checkError(
        zmq_ctx_term(ctx),
        "Terminating ZMQ context."
    );
    // other cleanup:

    for (auto p : pullers) {
        p->join();
        delete p;
    }
    delete []message;

    // Comput timings/statistics.
    auto duration= end - start;
    double ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(duration)
        .count();

    double secs = ms/1000.0;
    double kb = (double)(sent)*(double)(msgsize)/1024.0;

    std::cout << "Seconds:    " << secs << std::endl;
    std::cout << "Messages:   " << sent << std::endl;
    std::cout << "msgs/sec:   " << (double)sent/secs << std::endl;
    std::cout << "kb/sec      " << kb/secs << std::endl;

    // success:

    return EXIT_SUCCESS;
}