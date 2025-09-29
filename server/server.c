#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>


#define PORT "9034"


/**
 * convert socket to IP address string
 * @param addr struct addr_in or struct addr_in6
 */
const char *inet_ntop2(void *addr, char *buf, size_t size) {
    struct sockaddr_storage *sas = addr;
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;
    void *src;

    switch (sas->ss_family) {
        case AF_INET:
            sa4 = addr;
            src = &(sa4->sin_addr);
            break;
        case AF_INET6:
            sa4 = addr;
            src = &(sa6->sin6_addr);
            break;
        default: return NULL;
    }
    return inet_ntop(sas->ss_family, src, buf, size);
}

//returns a listening socket
int get_listener_socket() {
    //want get adrinfo
    struct addrinfo hints, *res, *p;
    int listener_socket;
    int yes = 1;
    int rv;


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; //IPv4, AF_INET6 for, AF_UNSPEC for any
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // use either IPv4 or IPv6


    if ((rv = getaddrinfo(NULL, PORT, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(rv));
    }

    //create a listener socket

    for (p = res; p != NULL; p = p->ai_next) {
        listener_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener_socket <= 0) {
            continue;
        }

        //socket option:
        //SO_REUSEADDR: reuse even if address is already in use
        setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        //bind socket if everything is a success
        if (bind(listener_socket, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener_socket);
            continue;
        }

        // if we got to this point, everything was a success
        break;
    }

    //we didn't find a successful socket to bind to
    if (p == NULL) {
        printf("Failed to bind to socket.\n");
        return -1;
    }

    //free memory as its no longer in use
    freeaddrinfo(res);

    return listener_socket;
}


int main(void) {
    printf("Hello, SERVER!\n");

    //allow for 5 connections for now, can realloc if needed
    int fd_size = 5;
    int fd_count = 0;
    struct pollfd *pfds = malloc(sizeof(*pfds) * fd_size);

    int listener_socket = get_listener_socket();

    //double check the socket
    if (listener_socket == -1) {
        printf("INVALID LISTENER SOCKET \n");
        return -1;
    }

    //add the listener to set
    //report ready to read on incoming connection
    pfds[0].fd = listener_socket;
    pfds[0].events = POLLIN;

    fd_count = 1; //accounting for the listener socket

    puts("Waiting for connections...");

    //main loop
    for (;;) {
        //poll for clients
        //this can be slow, so look into event based netoworking later
        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        //loop through our current connections looking for data to read
        for (int i = 0; i < fd_count; i++) {
            //check if theres something from a socket
            //pollin - alert me when data is ready to recv() on this socket
            // pollhup alert me when the remote closed the connection
            if (pfds[i].revents == (POLLIN | POLLHUP)) {
                //if its the listener we got a new connection
                if (pfds[i].fd == listener_socket) {
                    struct sockaddr_storage remoteaddr; //client address
                    socklen_t addrlen;
                    int newfd; // new accept() socket
                    char remoteIP[INET6_ADDRSTRLEN];
                    addrlen = sizeof remoteaddr;

                    newfd = accept(listener_socket, (struct sockaddr *) &remoteaddr, &addrlen);

                    //validate newfd
                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        //check for size overflow
                        if (fd_count == fd_size) {
                            fd_size *= 2; // double the size
                            pfds = realloc(pfds, sizeof(pfds) * fd_size);
                        }

                        //set new info
                        pfds[fd_count].fd = newfd;
                        pfds[fd_count].events = POLLIN; // check its ready to read
                        pfds[fd_count].revents = 0;
                        //increase size for next client
                        fd_count++;

                        printf("New connection from %s on socket %d\n",
                               inet_ntop2(&remoteaddr, remoteIP, sizeof remoteIP), newfd);
                    }
                } else {
                    //otherwise its just a client doing something
                    char buffer[256]; // client buffer data

                    //read from client
                    int nbytes = recv(pfds[i].fd, &buffer, sizeof(buffer), 0);

                    int sender_fd = pfds[i].fd;

                    if (nbytes <= 0) {
                        // error or connection closed
                        if (nbytes == 0) {
                            //connection closed
                            printf("server: socket %d hug up\n", sender_fd);
                        } else {
                            perror("recv");
                        }

                        close(pfds[i].fd); // close connection

                        //copy the last socket onto this socket
                        pfds[i] = pfds[fd_count - 1];
                        fd_count--;
                    } else {
                        // we got valid data from client
                        printf("server: received from fd: %d: %.%s\n", sender_fd, nbytes, buffer);

                        //send message to everyone
                        for (int j = 0; j < fd_count; j++) {
                            int dest_fd = pfds[j].fd;

                            //send to everyone except the listener and the sender themselves
                            if (dest_fd != sender_fd && dest_fd != sender_fd) {
                                if (send(dest_fd, &buffer, nbytes, 0) == -1) {
                                    perror("send");
                                };
                            }
                        }
                    }
                }
            };
        }
    }


    free(pfds);

    return 0;
}
