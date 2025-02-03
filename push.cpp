/**
 * demonstrates the push/pull pattern of communications.
 * The main program pushes and threads pull.
 * 
 * Usage:
 *     push uri npull nmsgs
 * 
 * Where:
 *     uri - is the uri on which communication is done.
 *     npull - is the number of  pull clients among which
 *             the pushe are distributed.
 *     nmsgs - is the number of messagse that will be sent before 
 * trying to shutdown everything.
 * 
 * @note  
 *    Shutdown is a bit tricky as we can't direct messages to individual
 * clients to know that communication is done and sends when there are
 * no pullers connected will block.  So we do the following;
 * 
 * * Create a latch with the number of pullers as its 'size'
 * * When a puller recieves its go-away message, it will arrive at the latch.
 * * After all pullers have arrived at the latch; they will wait at a latch 
 *   of size pullers+1. they will then exit.
 * * The main program sends nmsgs
 * * Once nmsgs are sent, it will push exit messages until the first latch
 * has completed.
 * *  The pusher will then arrive at the second latch and all will close/shutdown.
 * 
 * In this way, the pusher is never sending messags when there are no receivers connected.
 * and it knows when all receivers are ready to exit.alignas
 *
 * @note this is not production code, omitting any parameters will, most likely
 * cause a segfault. 
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
 * puller
 * 
 * @param uri - uri to connect to.
 * @param ctx  ZMQ context shared among the threads.
 * @param id  - My puller id - my output will be prefaced with this
 * @param exitlatch - Latch to await after asked to exit.
 * @param donelatch - Latch to await at after exitlatch was complete.
 * 
 */
static void
puller(std::string uri, void* ctx, int id, std::latch& exitlatch, std::latch& donelatch) {

    // Connect to the pusher:

    void* sock = checkError(
        zmq_socket(ctx, ZMQ_PULL),
        "Failed to set up pull socket."
    );

    checkError(
        zmq_connect(sock, uri.c_str()),
        "Failed to connect puller to pusher."
    );

    while (true) {
        auto msg = rcvString(sock);
        std::cerr << "Puller # " << id << " " << msg << std::endl;
        if (msg == "EXIT") break;
    }
    exitlatch.arrive_and_wait();

    // At this point all of the pullers are ready to stop but not the
    // pusher:

    donelatch.arrive_and_wait();

    // Now we can disconnect from the pusher because it's stopped sending.

    checkError(
        zmq_close(sock),
        "Puller failed to close socket."
    );

}
/**
 * main - the pusher. See file comments for how this works.
 */
int main (int argc, char** argv) {
    std::string uri(argv[1]);
    int nPullers = atoi(argv[2]);
    int nMessages = atoi(argv[3]);

    // Make the context and bound push socket:

    void* ctx = checkError(
        zmq_ctx_new(),
        "Failed to make shared zmq context"
    );
    auto  sock = checkError(
        zmq_socket(ctx, ZMQ_PUSH),
        "Failed to make push socket"
    );
    checkError(
        zmq_bind(sock, uri.c_str()),
        "Failed to bind push socket"
    );

    //  We can spin off the threads:

    std::latch exitlatch(nPullers);
    std::latch donelatch(nPullers+1);
    std::vector<std::thread*> pullThreads;

    for (int i =0; i < nPullers; i++) {
        pullThreads.push_back(new std::thread(puller, uri, ctx, i, std::ref(exitlatch), std::ref(donelatch)));
    }

    // Push nMessages to the pullers:

    for (int i =0; i < nMessages; i++) {
        std::stringstream msgStream;
        msgStream << "Push number " <<  i;
        std::string message=msgStream.str();
        sendString(sock, message.c_str());
    }
    // push EXIT messages until the exitlatch is made:

    while(!exitlatch.try_wait()) {
        sendString(sock, "EXIT");
        usleep(500);                // So we don't flood our output queue.
    }
    // Arrive at the donelatch so the threads can disconnect

    donelatch.arrive_and_wait();
    for (auto t: pullThreads) {
        t->join();
        delete t;
    }

    // Tear down everything:

    checkError(
        zmq_close(sock),
        "Closing push socket"
    );
    checkError(
        zmq_ctx_term(ctx),
        "Terminating socket."
    );
    return EXIT_SUCCESS;

}
