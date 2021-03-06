/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <fcntl.h> /* Added for the nonblocking socket */

#define BUFSIZE 1024
#define BACKLOG 10
#define S1READY 0x01

void * connection_handling(void *);
void * listen_thread(void *);

enum state {
    notSet, Set
};

//Stores the filedescriptors. listen on sock_fd
int listener;

void tserver_init(char * interface, char *port) {
    //Linked lists. Hints is to store our settings. servinfo is to collect
    //information about a particular host name.
    struct addrinfo hints, *servinfo, *p;

    //Initialize hints.
    //Some fields we need to set.
    //All the other fields in the structure pointed to by hints must
    //contain either 0 or a NULL pointer,  as  appropriate.
    //there needs to be zero's for the "getaddrinfo" function.
    memset(&hints, 0x00, sizeof (hints));

    //The  hints  argument  points to an addrinfo structure that specifies
    //criteria for selecting the socket address
    //structures returned in the list pointed to by res

    //It can use both IPv4 or IPv6
    hints.ai_family = AF_UNSPEC;
    //For streaming socket. Write SOCK_DGRAM for datagram.
    hints.ai_socktype = SOCK_STREAM;
    //Use my IP
    hints.ai_flags = AI_PASSIVE;

    int rv;
    if ((rv = getaddrinfo(interface, port, &hints, &servinfo)) != 0) {
        //gai_strerror returns error code from getaddrinfo.
        printf("getaddrinfo: %s\n", gai_strerror(rv));
        syslog(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        //socket creates an endpoint for communication. 
        //Returns descriptor. -1 on error
        if ((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
                == -1) {
            printf("Cannot create socket\n");
            syslog(LOG_ERR, "Cannot create socket");
            continue;
        }

        //Indicates that the rules used in validating addresses supplied
        //in a bind(2) call should allow reuse of local addresses.  For
        //AF_INET sockets this means that a socket may bind, except when
        //there is an active listening socket bound to the address.
        //It makes it possible to close the server and restart immidiately.
        int enable = 1;
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable,
                sizeof (int)) < 0) {
            perror("setsockopt(SO_REUSEADDR) failed");
        }
        
        //every packet with destination servinfo->ai_addr should be forwarded to
        //listener.socket needs to be associated with a port on local machine.
        //bind sets errno to the error if it fails.
        if (bind(listener, p->ai_addr, p->ai_addrlen) == -1) {
            close(listener);
            syslog(LOG_ERR, "Cannot bind");
            printf("Cannot bind\n");
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);

    //If the linked list has reached the end without binding.
    if (p == NULL) {
        syslog(LOG_ERR, "Sockfd Could not associate with port");
        exit(1);
    }

    //Listen for connections on a socket.
    //sockfd is marked as passive, one used to accept incoming connection
    //requests using accept.
    //listen also sets errno on error.
    if (listen(listener, BACKLOG) == -1) {
        printf("Error on listen\n");
        syslog(LOG_ERR, "Error on listen");
        exit(1);
    }

    // Change the socket into non-blocking state
    int status = fcntl(listener, F_SETFL, O_NONBLOCK);

    if (status != 0) {
        exit(1);
    }

    syslog(LOG_INFO, "Return status from fcntl: %d", status);

    printf("server: waiting for connections...\n");
    syslog(LOG_INFO, "server: waiting for connections...");

    // Collect tid's here
    pthread_t threadid;

    //&threads = unique identifier for created thread.
    //connection_handling = start routine
    // (void*) argument for our start routine, you can send one as a
    //void pointer.
    int rc = pthread_create(&threadid, NULL, listen_thread, NULL);
    if (rc) {
        printf("Couldn't create listen thread\n");
        syslog(LOG_ERR, "Couldn't create listen thread");
        exit(-1);
    }
    pthread_join(threadid, NULL);
}

void * listen_thread(void * p) {

    // master file descriptor list    
    fd_set master;

    //maximum file descriptor number
    int fdmax;

    // clear the master and temp sets 
    FD_ZERO(&master);

    // add the listener to the master set
    FD_SET(listener, &master);

    //keep track of the biggest file descriptor
    // so far, it's this one        
    fdmax = listener;

    intptr_t new_fd;
    //Endless loop that awaits connections
    while (1) {

        if (select(fdmax + 1, &master, NULL, NULL, NULL) == -1) {
            perror("Server-select() error in listener");
            exit(1);
        }

        //check to see if there is a new connection ready to be accepted
        //on the listener thread.
        if (FD_ISSET(listener, &master)) {
            //storage for the size of the connected address.
            // connector's address information
            struct sockaddr_storage their_addr;
            socklen_t sin_size = sizeof (their_addr);

            //intptr_t is an integer with the same size 
            //as a pointer on the system
            new_fd = accept(listener, (struct sockaddr *) &their_addr,
                    &sin_size);
        }

        //it should never enter here, since FD_ISSET is used as filter.
        if (new_fd == EAGAIN || new_fd == EWOULDBLOCK) {
            // The socket is marked nonblocking and no connections are
            //present to be accepted
            printf("EAGAIN\n");
            continue;
        }

        if (new_fd == -1) {
            printf("Could not accept\n");
            syslog(LOG_ERR, "Could not accept");
            continue;
        }

        printf("Accepted incoming connection\n");
        syslog(LOG_INFO, "Accepted incomming connection");

        // Collect tid's here
        pthread_t threads;

        //&threads = unique identifier for created thread.
        //connection_handling = start routine
        // (void*) argument for our start routine, you can send one as a
        //void pointer. We send the file descriptor for the new connection.
        int rc = pthread_create(&threads, NULL, connection_handling,
                (void*) new_fd);
        if (rc) {
            printf("Couldn't create thread\n");
            syslog(LOG_ERR, "Couldn't create thread: %d", rc);
            exit(1);
        }

    }

}

int waittowrite(int s1) {
    fd_set fds;
    int rc, result;

    /* Create a descriptor set containing our two sockets.  */
    FD_ZERO(&fds);
    FD_SET(s1, &fds);

    rc = select(sizeof (fds)*8, NULL, &fds, NULL, NULL);
    if (rc == -1) {
        perror("select failed");
        return -1;
    }

    result = 0;
    if (rc > 0) {
        if (FD_ISSET(s1, &fds)) {
            result |= S1READY;
            printf("Ready to write\n");
            syslog(LOG_INFO, "Ready to write");
        }
    }

    return result;
}

int waittoread(int s1) {
    fd_set fds;
    int rc, result;

    /* Create a descriptor set containing our two sockets.  */
    FD_ZERO(&fds);
    FD_SET(s1, &fds);

    rc = select(sizeof (fds)*8, &fds, NULL, NULL, NULL);
    if (rc == -1) {
        perror("select failed");
        return -1;
    }

    result = 0;
    if (rc > 0) {
        if (FD_ISSET(s1, &fds)) {
            result |= S1READY;
            printf("Ready to read\n");
            syslog(LOG_INFO, "Ready to read");
        }
    }
    return result;
}

void * connection_handling(void * new_fd) {
    enum state done = notSet;

    //Cast back fd to an integer.
    intptr_t fd = (intptr_t) new_fd;

    char buf_out[BUFSIZE];

    if (waittowrite(fd) > 0) {
        //No worries about strcpy, there are plenty of space
        //in the input buffer.
        strcpy(buf_out, "CONNECTED!\n");
        send(fd, buf_out, strlen(buf_out), 0);
        printf("Sent: %s\n", buf_out);
    }
    int n;
    while (!done) {
        char buf_in[BUFSIZE];

        memset(buf_in, 0x00, strlen(buf_in));

        if (waittoread(fd) > 0) {
            n = recv(fd, buf_in, sizeof (buf_in), 0);

            //n=0 , when the peer has performed an orderly shutdown.
            //Therefore we need to include n=0.
            if (n <= 0) {
                printf("Could not read from socket\n");
                syslog(LOG_ERR, "Could not read from socket");
                done = Set;
                break;
            }

            //End of file
            if (buf_in[0] == 0x04) {
                done = Set;
                printf("Peer has performed orderly shutdown\n");
                //break out of the loop and test on the main while loop
                break;
            }

            printf("Received %d bytes: %s\n", n, buf_in);
            syslog(LOG_INFO, "Received %d bytes: %s", n, buf_in);

            if (waittowrite(fd) > 0) {
                //echo the input string back to the client
                n = send(fd, buf_in, n, 0);

                if (n < 0) {
                    printf("Could not write to socket\n");
                    syslog(LOG_ERR, "Could not write to socket");

                    done = Set;
                    //break out of the loop and test on the main while loop
                    break;
                }
                printf("Sent %d bytes: %s\n", n, buf_in);
            }
        }
    }
    close(fd);
    return NULL;
}