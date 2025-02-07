/**
 * req
 *    This program times the REQ/REP communication pattern.  Note that
 * this pattern is many to one, you can have several REQs making requests of 
 * a single replier.  However, since handling these requests is serialized from the
 * application point of view, we only have one REQuestor in our timings.
 * 
 * Usage:
 *    req uri numreq bigsize
 * Where:
 *    uri is the URI of the communications endpoint
 *    numreq  is the number of requests that will be done
 *    bigsize is the size of the 'big' message.
 * 
 * Two sets of timings are done for each run:
 *    *   Req has size 'bigsize' and replies are a single byte.
 *    *   Req is a single byte and replies are 'bigsize'.
 * 
 * Since each req is delivered reliably and requires a response, 
 * there's not trickiness needed to synchronize the ending.
 * 
 * @note Thisis not production code so a missing parameter is going to likely 
 * segfault and a wonky one will do undefined things (e.g. bigsize <- 0) 
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
 * ignore
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
 * replier
 *    Fields requests from the REQ side and responds.
 * The last req will have a non-zero first byte.
 * 
 * @param uri - URI we will binding for the requestor.
 * @param ctx - ZMQ context needed to create the socket.
 * @param size - Size of the response we send.   The contents is nothing
 *             in particular.
 */
static void
replier(std::string uri, void* ctx, int size) {
    auto socket = checkError(
        zmq_socket(ctx, ZMQ_REP),
        "Making replier socket."
    );
    setBuffering(socket);
    checkError(
        zmq_bind(socket, uri.c_str()), 
        "binding replier socket"
    );
    // ready to go:
    char* replymsg = new char[size];
    
    while(ignore(socket) == 0) {
        
        send(socket, replymsg, size);
        
    }
    // THe last reply for the request that  made ignore true:

    send(socket, replymsg, size);

    // cleanup:

    delete []replymsg;
    checkError(
        zmq_close(socket),
        "Cloing rep socket."
    );

}
/**
 * requestor
 *    Does a timing set.  Note that communication is setup and torn down
 * @param uri - Communications end point to use.
 * @param nreq - Number of requests that will be sent.
 * @param reqsize - size of the request.
 * @param repsize - size of the reply.
 * @returns double -the number of seconds in the timed part.
 */
static double
requestor(std::string uri, int nreq, int reqsize, int repsize) {
    auto context = checkError (
        zmq_ctx_new(), 
        "Making shared ZMQ context object."
    );

    // Start the REP thread which does the listen:

    std::thread replythread(replier, uri, context, repsize);
    sleep(1);                            // So it can be listening:

    auto socket = checkError(
        zmq_socket(context, ZMQ_REQ),
        "Making request socket"
    );
    checkError(
        zmq_connect(socket, uri.c_str()), 
        "Connecting to the replier"
    );

    char* request  = new char[reqsize];
    *request = 0;

    // Start timing and doing the REQ/REP dance:

    auto start = std::chrono::high_resolution_clock::now();
    for (int i =0; i < nreq; i++) {
        send(socket, request, reqsize);
        ignore(socket);
        
        if (i == (nreq-2)) {
            // next one is the last one:
            *request = 0xff;
        }
    }
    replythread.join();
    auto end = std::chrono::high_resolution_clock::now();

    // Tear down zmq:

    checkError(
        zmq_close(socket),
        "Closing request socket"
    );
    checkError(
        zmq_ctx_term(context),
        "Terminating ZMQ context"
    );

    // Comput/return the timng in seconds:

    auto duration = end - start;
    double ms = 
        (double)std::chrono::duration_cast<std::chrono::milliseconds>(duration)
        .count();
    return ms/1000.0;                    // Seconds.
}
// Main is the requestor that way we can control the flow.

int main(int argc, char** argv) {
    std::string uri(argv[1]);
    int nreq = atoi(argv[2]);
    int bigsize = atoi(argv[3]);

    double bigsecs = requestor(uri, nreq, bigsize, 1);
    double smallsecs = requestor(uri, nreq, 1, bigsize);

    // Compute the timings:

    double kb = (double)bigsize*(double)nreq/1024.0;   // Kb transfererd.

    std::cout << "Request size " << bigsize << " Reply size 1 byte\n";
    std::cout << "Seconds:     " << bigsecs << std::endl;
    std::cout << "Req/sec:     " << (double)nreq/bigsecs << std::endl;
    std::cout << "KB/sec:      " << kb/bigsecs << std::endl;

    std::cout << "Request size 1 reply size " << bigsize << std::endl;
    std::cout << "Seconds:     " << smallsecs << std::endl;
    std::cout << "Req/sec:     " << (double)nreq/smallsecs << std::endl;
    std::cout << "KB/sec:      " << kb/smallsecs << std::endl;
}