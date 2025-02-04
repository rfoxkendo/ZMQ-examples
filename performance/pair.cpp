/**
 * pair.cpp
 *    This program measures timngs for the ZMQ pair communication pattern.
 * Although in pair, there's no predefined discplie of sender/receiver, this
 * program has a single sender (the main thread) sendign messages to the
 * receiver thread which then replies  back:alignas
 *
 * Usage:
 *    pair uri  nummsgs  size
 * 
 * Where:
 *     uri - is the communication end point URI, the main binds.
 *     nummsgs - is the number of send/receive pairs done.
 *     size - is the size of the 'large' message.
 * 
 * Two sets of timings are done, which I'd hope would be close to
 * the same:
 * 
 * *  main sends messages of ```size``` and gets back single byte
 * messages.
 *  * main sends messages 1 byte long and gets back 'size' messages.
 * 
 * To purify the timings, the messages received are not even removed 
 * from the zmq_msg.  We also use zero copy messages for the sender.
 * 
 * Termination is simple as both peers know the number of messages
 * that will be exchanged and communication is assumed reliable.
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
 * @note - we ensure the message is a single part message.
 */
static void
ignore(void* socket) {
    zmq_msg_t msg;
    checkError(zmq_msg_init(&msg), "Initializing message");

    checkError(
        zmq_recvmsg(socket, &msg, 0),
        "Receiving message part."
    );
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
}
/**
 * peer
 *    The peer thread for the pair.
 * @param uri - uri to zmq_connect to.
 * @param ctx - shared ZMQ context.
 * @param nmsgs - Number of messages to exchange.
 * @param size - Size of the messages we will return.
 * @note see the comments in the top of the file for more
 * information about how this works.
 */
static void 
peer(std::string uri, void* ctx, int nmsgs, int size) {
    // Set up my  communications path;

    auto socket = checkError(
        zmq_socket(ctx, ZMQ_PAIR),
        "Creating thread's pair socket."
    );
    checkError(
        zmq_connect(socket, uri.c_str()),
        "Connecting to peer."
    );
    char* msg = new char[size];
    // exchange messages:

    for (int i = 0; i < nmsgs; i++) {
        ignore(socket);
        send(socket, msg, size);
    }
    delete []msg;
    checkError(
        zmq_close(socket),
        "Closing thread's socket."
    );
}

/**
 *  run
 *     Runs one of the timings.alignas
 * @param uri - communications endoint uri.
 * @param context - ZMQ context on which communication is done.
 * @param nummsgs - Number send/receive pairs.
 * @param mainsize - Size of the messages we will send.
 * @param thrsize - size of the messags the thread will send us.
 * @return double precision seconds the send/recieves took.
 */
static double
run(std::string uri, void* context, int nummsgs, int mainsize, int thrsize) {
    // Setup our side of the pair and bind

    auto socket = checkError(
        zmq_socket(context, ZMQ_PAIR),
        "Creating main thread socket"
    );
    checkError(
        zmq_bind(socket, uri.c_str()),
        "Binding socket in main thread."
    );
    // Start the peer thread:

    std::thread peerThread(peer, uri, context, nummsgs, thrsize);

    // Time the message exchange -> join:
    char* sendmsg = new char[mainsize];    // Allocate only once.
    auto start = std::chrono::high_resolution_clock::now();
    for (int i =0; i < nummsgs; i++) {
        send(socket, sendmsg, mainsize);         // send
        ignore(socket);                          // reply.
    }
    peerThread.join();                           // so all is done.
    auto end = std::chrono::high_resolution_clock::now();

    // Shutdown the communication from our side:

    checkError(
        zmq_close(socket),
        "Closing socket in main thread"
    );

    // Compute the duration:

    auto chronoDuration = end - start;    // Some funky object that needs conversion:
    double ms = 
        (double)std::chrono::duration_cast<std::chrono::milliseconds>(chronoDuration)
        .count();                       // Milliseconds.
    return ms/1000.0;            // seconds.
}
/**
 * main
 *   We are a peer and time the message exchanges.
 */
int main(int argc, char** argv) {
    std::string uri(argv[1]);
    int nummsgs = atoi(argv[2]);
    int size  =  atoi(argv[3]);

    auto context = checkError(
        zmq_ctx_new(),
        "Making ZMQ context"
    );

    double duration1 = run(uri, context, nummsgs, size, 1); // 'big' send, small return.
    double duration2 = run(uri, context, nummsgs, 1, size); // small send, 'big' return.


    checkError(
        zmq_ctx_term(context),
        "Terminating ZMQ context"
    );

    // Compute/print timings for big sends:

    std::cout << "Big sends small replies\n";
    std::cout << "Time    :  " << duration1 << std::endl;
    std::cout << "Msgs/sec:  " << (double)nummsgs/duration1 << std::endl;
    std::cout << "KB/sec  :  " << (double)size*(double)nummsgs/duration1 << std::endl;

    // ditto for small sends:

    std::cout << "Small sends, big replies\n";
    std::cout << "Time    :  " << duration2 << std::endl;
    std::cout << "Msgs/sec:  " << (double)nummsgs/duration2 << std::endl;
    std::cout << "KB/sec  :  " << (double)size*(double)nummsgs/duration2 << std::endl;

}