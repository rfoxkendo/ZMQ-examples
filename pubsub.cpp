/**
 * example of pubsub commmunications.
 * Usage:
 *    pubsub uri nsub msgs
 * 
 * Where
 *    uri is the communications endoint URI.
 *    nsub are the number of subscribers.
 *    msgs are the number of publications.
 * 
 * Clients subscribe to one of 
 *    ODD, EVEN, or ALL depending on their id:
 *   *  id % 3 == 0 'ALL'
 *   *  id % 3 == 1 'ODD'
 *   *  id % 3 == 2 'EVEN'
 * 
 * Subscribers keep processing messages until the part of the message after the
 * selector is "BYE" at which time they exist.alignas
 * 
 * The publisher (main) will send alternatively ALL, ODD, EVEN messages until
 * nmsgs were sent at which time it will send
 * ALL BYE, ODD BYE and EVEN BYE messages to end all subscribers.alignas
 * 
 * publisher will then  join the subscriber threads and then shutdown
 * the system.
 */
/**
 *  Shows how the req/rep pattern works.
 * 
 * Usage:
 *    reqrep uri clients responses
 * Where:
 *    uri - is the URI of the communications endpoint
 *    clients - number of requesters.
 *    responses - number of resonses after which we'll be telling the
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
// split a string into words:

static std::vector<std::string>
splitLine(const std::string& line) {
    std::stringstream strLine(line);
    std::vector<std::string> result;

    std::string word;
    while (strLine >> word) {
        result.push_back(word);
    }
    return result;
}

// Empty string subsribes to all.

static const char* subscriptions[3] = {
    "", "ODD", "EVEN"
};
/**
 *  subscriber:
 * 
 * @param uri - URI of communication endpoint.
 * @param ctx - ZMQ context.
 * @param id  - ID of thread (sort of).
 * 
 */
static void subscriber(std::string uri, void* ctx, int id) {
    auto subscription = subscriptions[id % 3];    // We'll need this.
    // Set up the subscription.
    auto socket = checkError(
        zmq_socket(ctx, ZMQ_SUB),
        "Opening subscription socket"
    );
    checkError(
        zmq_setsockopt(socket, ZMQ_SUBSCRIBE, subscription, strlen(subscription)),
        "Setting subscription"
    );
    checkError(
        zmq_connect(socket, uri.c_str()),
        "Connecting to publisher."
    );
    while(true) {
        // Get messages until the second word is EXIT

        std::string message = rcvString(socket);
        std::cerr << id << " got " << message << std::endl;
        auto words = splitLine(message);
        if (words[1] == "EXIT") break;  // Word 0 is our subscription string.
    }
    // shutdown:

    checkError(
        zmq_close(socket),
        "Closing subscription socket."
    );

}

// The publisher and subscriber spinner offer.

int main(int argc, char** argv) {
    std::string uri(argv[1]);
    int subscribers = atoi(argv[2]);
    int responses = atoi(argv[3]);

    // create the publisher so the subscribers have something
    // to connect with:
    auto context = checkError(
        zmq_ctx_new(),
        "Creating shared zmq context"
    );
    auto socket = checkError(
        zmq_socket(context, ZMQ_PUB),
        "Creating publisher socket."
    );
    checkError(
        zmq_bind(socket, uri.c_str()),
        "Binding publisher."
    );

    // spin off the sbuscribers:

    std::vector<std::thread*> subscriberThreads;
    for (int i = 0; i < subscribers; i++) {
        subscriberThreads.push_back(new std::thread(subscriber, uri, context, i));
    }
    sleep(1);     // wait for them to all be receiving.

    for (int i = 0; i < responses; i++) {
        std::stringstream strResponse;
        strResponse << ((i % 2 == 0 )? "EVEN" : "ODD") << " message number " << i;
        std::string message = strResponse.str();

        sendString(socket, message.c_str());
    }
    // Now get everyone to exit... send an odd and an even exit message.
    sendString(socket, "EVEN EXIT");   // All also get this and exit.
    sendString(socket, "ODD EXIT");    // Stop the odd ones.

    // join:

    for (auto p : subscriberThreads) {
        p->join();
        delete p;
    }

    // Shutdown:

    checkError(
        zmq_close(socket),
        "Closing publisher socket"
    );
    checkError(
        zmq_ctx_term(context),
        "Closing ZMQ context"
    );
    return EXIT_SUCCESS;
}
