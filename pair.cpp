/**
 * This program illustrates the zeroMQ pair pattern.
 * This pattern is a channel between peers (either side can
 * initiate communicatinos and no reply is required).
 * 
 * In our case the main thread sends a message to a subthread
 * which is the peer and the subthead then sends a couple of
 * messages back.
 * 
 * Usage:
 *    pair uri
 * Where
 *    uri is the end point uri that will be bound/connected.
 * 
 * @note Not production code so omitting the URI will likely segfault.
 */
#include <thread>
#include <zmq.h>
#include <iostream>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <string.h>
#include <unistd.h>

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
 *  utility to receive and print a message string.
 * @param sock - socket to receive on.
 * 
 */
static void
rcvAndPrint(void* sock) {

    // Get the message and require it to be a single part.
    zmq_msg_t msg;
    checkError(zmq_msg_init(&msg), "Initializing message");

    checkError(
        zmq_recvmsg(sock, &msg, 0),
        "Receiving message part."
    );
    int more;
    size_t morelen(sizeof(more));
    checkError(
        zmq_getsockopt(sock, ZMQ_RCVMORE, &more, &morelen),
        "Failed to get more flag"
    );
    if (more) {
        std::cerr << "Thought I was getting a single part message, got a multipart!\n";
        exit(EXIT_FAILURE);
    }
    // Fetch the string from the message and output to stdout:
    // Assumes the payload is a cstring.
    std::string printme(reinterpret_cast<char*>(checkError(
        zmq_msg_data(&msg),
        "Failed to get message data."
    )));
    std::cout << printme << std::endl; 

    zmq_msg_close(&msg);   

}

/**
 * sendString
 *    Send a string to a socket:alignas
 * @param sock - socket on which to send it.
 * @param msg = c ztring to send.
 * @note sadly, zmq_msg_init_data requires a void* which means a const cast
 * since we are capable of sending quoted strings.
 * @note since we dont' know the lifetime requirements of the data we're sending
 * we won't do a a zero copy of the data:
 */
static void
sendString(void* sock, const char* mesg) {
    zmq_msg_t msg;
    checkError(
        zmq_msg_init_size(&msg, strlen(mesg) + 1),
        "Failed to allocate message copy storage."
    );
    strcpy(reinterpret_cast<char*>(zmq_msg_data(&msg)), mesg);   // Copy message in.
    
    checkError(
        zmq_sendmsg(sock, &msg, 0),    // I think this does a close eventually.
        "Sending string message"
    );

}

/**
 * This is the function that runs the peer thread.
 * It does the bind half of the connection as it will be
 * started first.
 * 
 * @param uri - uri to bind to.
 * @param ctx = Context - must be shared between main and us for inproc to work.
 * 
 * Reads a message - outputs it and sends two messages to the peer;
 * then reads a last messages which tells us to exit.
 * 
 */
static void
peer(const char* uri, void* ctx) { 
    
    // Set up as a pair peer and listen for connections:

    auto sock = checkError(
        zmq_socket(ctx, ZMQ_PAIR),
        "Thread creating pair socket."
    );
    checkError(
        zmq_bind(sock, uri),
        "Binding socket to endpoint"
    );
    // Now get/print message from the peer:

    rcvAndPrint(sock);

    // Send a pair of messages to the peer.

    sendString(sock, "Hello there");
    sendString(sock, "Good bye");

    // Get the final message from the peer.

    rcvAndPrint(sock);

    // Shutdown.

    checkError(
        zmq_close(sock),
        "Closing thread socket."
    );

    

}
/**
 *  main 
 *    spins off the thread.
 *    Creates a pair socket, binding to the thread.
 *    Sends the peer a messages.
 *    Receives/prints two messages from the peer.
 *    Sends a goodnight message to the peer
 *    Joins the peer
 *    Shuts down the socket and  ZMQ.
 */
int main(int argc, char** argv) {
    const char* uri = argv[1];

    auto ctx = checkError(
        zmq_ctx_new(),
        "Main thread creating a zmq context"
    );

    // Start the peer:

    auto peerThread = std::thread(peer, uri, ctx);
    sleep(1);       // let it get to the bind....

    // Set up the pair thread via connect:

    auto sock = checkError(
        zmq_socket(ctx, ZMQ_PAIR),
        "Thread creating pair socket."
    );
    checkError(
        zmq_connect(sock, uri),
        "Connecting to peer thread"
    );
    // Send a message:

    sendString(sock, "Hello main thread to peeer");

    // Receive two messages from the peer:

    rcvAndPrint(sock);
    rcvAndPrint(sock);

    // send the goodbye:

    sendString(sock, "Bye-y'all");


    // Wait for peer to exit:

    peerThread.join();

    //shutdown:

    checkError(
        zmq_close(sock),
        "Closing main thread socket"
    );
    checkError(
        zmq_ctx_term(ctx),
        "Shutting down the context"
    );
    return EXIT_SUCCESS;
}