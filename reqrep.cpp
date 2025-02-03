/**
 *  Shows how the req/rep pattern works.
 * 
 * Usage:
 *    reqrep uri clients responses
 * Where:
 *    uri - is the URI of the communications endpoint
 *    clients - number of requesters.
 *    reponses - number of resonses after which we'll be telling the
 *        clients to exit.
 * 
 * @note this is not production code so we will segfault if a parameter is missing.
 * @note Since each REQ is paird with an REP, we know how to end:  just send
 * 'clients' number of EXIT reponses then join/shutdown.
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
 *  utility to receive a message string.
 * @param sock - socket to receive on.
 * @return std::string - message string received.
 */
static std::string
rcvString(void* sock) {

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
    

    zmq_msg_close(&msg);   
    return printme;
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
 *  Request thread
 * 
 * @param uri - uri to which we will connect.
 * @param ctx - shared context to make inproc work.
 * @param id  - The id of the requestor.
 */
static void
requester(std::string uri, void* ctx, int id) { 
    // Set up the request pipe:

    auto socket = checkError(
        zmq_socket(ctx, ZMQ_REQ),
        "Creating req socket."
    );
    checkError(
        zmq_connect(socket, uri.c_str()),
        "Connecting to server"
    );

    while (true) {
        std::stringstream strReq;
        strReq << "Request from " << id;
        std::string req(strReq.str());
        sendString(socket, req.c_str());
        std::string reply = rcvString(socket);
        std::cerr << id << " Response: " << reply << std::endl;
        if (reply == "BYE") break;
    }

    checkError(
        zmq_close(socket),
        "Could not close req socket."
    );
    // done.
}
// main is the replyer.

int main(int argc, char** argv) {
    std::string uri(argv[1]);
    int nclients = atoi(argv[2]);
    int nreplies = atoi(argv[3]);

    auto context = checkError(
        zmq_ctx_new(),
        "Creating context"
    );
    // Make my REP socket and set it up to accept connections:

    auto socket = checkError(
        zmq_socket(context, ZMQ_REP), 
        "Making reply socket"
    );
    checkError(
        zmq_bind(socket, uri.c_str()), 
        "Binding REP socket"
    );

    // Make the clients:

    std::vector<std::thread*> reqThreads;
    for (int i =0; i < nclients; i++) {
        reqThreads.push_back(new std::thread(requester, uri, context, i));
    }

    // Handle the number of requests we've obligated ourself to.

    for (int i = 0; i < nreplies; i++) {
        std::string req = rcvString(socket);
        std::cerr << "Request: " << req << std::endl;
        sendString(socket, "Keep going for now");
    }
    // Now reply with BYE for each requestor.

    for (int i =0; i < nclients; i++) {
        std::string req = rcvString(socket);
        sendString(socket, "BYE");
    }
    // Join the threads:

    for (auto p : reqThreads) {
        p->join();
        delete p;
    }

    // Sutdown:

    checkError(
        zmq_close(socket), "CLosing pull socket"
    );
    checkError(
        zmq_ctx_term(context), "terminating context"
    );

    return EXIT_SUCCESS;
}