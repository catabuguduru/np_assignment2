#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <math.h>

#define DEBUG
#include <protocol.h>

#define MAX_RETRIES 3

int sock;
struct calcMessage calcMsg;
struct calcProtocol response_message;
int retries = 0;
int last_message_type = 0;  // 0 for calcMessage, 1 for calcProtocol

// Function to send calcMessage
void send_calcMsg() {
    if (send(sock, &calcMsg, sizeof(calcMsg), 0) < 0) {
        perror("Failed to send calcMessage");
        exit(EXIT_FAILURE);
    }
}

// Function to send calcProtocol
void send_calcProt() {
    if (send(sock, &response_message, sizeof(response_message), 0) < 0) {
        perror("Failed to send calcProtocol");
        exit(EXIT_FAILURE);
    }
}

// Signal handler for SIGALRM
void handle_alarm(int sig) {
    if (retries < MAX_RETRIES) {
#ifdef DEBUG        
        printf("Timeout occurred, retransmitting... (%d/%d)\n", retries + 1, MAX_RETRIES);
#endif
        if (last_message_type == 0) {
            send_calcMsg();
        } else if (last_message_type == 1) {
            send_calcProt();
        }
        retries++;
    } else {
#ifdef DEBUG
        printf("No response from server after %d retries. Exiting.\n", retries);
#endif
        close(sock);
        exit(EXIT_FAILURE);
    }
}

// Function to set the timer for retransmissions
void set_timer() {
    struct itimerval alarmTime;
    alarmTime.it_interval.tv_sec = 2;
    alarmTime.it_interval.tv_usec = 0;
    alarmTime.it_value.tv_sec = 2;
    alarmTime.it_value.tv_usec = 0;

    setitimer(ITIMER_REAL, &alarmTime, NULL);
}

// Function to check the destination host type
int check_desthost(char *Desthost) {
    struct sockaddr_in sa;
    struct sockaddr_in6 ipv6_sa;
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if (inet_pton(AF_INET, Desthost, &(sa.sin_addr)) == 1) {
        return 1;
    } else if (inet_pton(AF_INET6, Desthost, &(ipv6_sa.sin6_addr)) == 1) {
        return 2;
    } else if (getaddrinfo(Desthost, NULL, &hints, &res) == 0) {
        freeaddrinfo(res);
        return 3;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *input = argv[1];
    char *port_no = strrchr(input, ':');
    if (port_no == NULL) {
        printf("Only accepted input format is host:port\n");
        return 1;
    }

    *port_no = '\0';

    char *Desthost = input;
    char *Destport = port_no + 1;
    int port = atoi(Destport);
    printf("Host %s, and port %d.\n", Desthost, port);

#ifdef DEBUG
    printf("Connected to %s:%d and local.\n", Desthost, port);
#endif

    int address_type = check_desthost(Desthost);
    if (address_type == 0) {
        printf("Invalid IP address\n");
        return 1;
    }

    if (address_type == 1) {
        struct sockaddr_in client;
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("Cannot create socket with IPv4 address");
            return 1;
        }

        client.sin_family = AF_INET;
        client.sin_port = htons(port);
        client.sin_addr.s_addr = inet_addr(Desthost);

        if (connect(sock, (struct sockaddr *) &client, sizeof(client)) < 0) {
            perror("Connection failed");
            close(sock);
            return 1;
        }
    } else if (address_type == 2) {
        struct sockaddr_in6 client;
        memset(&client, 0, sizeof(client));

        sock = socket(AF_INET6, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("Cannot create socket with IPv6 address");
            return 1;
        }
        client.sin6_family = AF_INET6;
        client.sin6_port = htons(port);
        if (connect(sock, (struct sockaddr *) &client, sizeof(client)) < 0) {
            perror("Connection failed with the address");
            close(sock);
            return 1;
        }
    } else if (address_type == 3) {
        struct addrinfo hints, *res, *rp;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM;

        int status = getaddrinfo(Desthost, Destport, &hints, &res);
        if (status != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
            return 1;
        }

        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0) {
                continue;
            }

            if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
                printf("Connected to host %s\n", Desthost);
                break;
            }
            close(sock);
        }

        if (rp == NULL) {
            fprintf(stderr, "Check your DNS name, unable to attach to any address\n");
            freeaddrinfo(res);
            return 1;
        }
        freeaddrinfo(res);
    }
  
    // Prepare the calcMessage to be sent
    calcMsg.type = htons(22);
    calcMsg.message = htonl(0);
    calcMsg.protocol = htons(17);
    calcMsg.major_version = htons(1);
    calcMsg.minor_version = htons(0);

    // Set up signal handler for SIGALRM
    signal(SIGALRM, handle_alarm);

    // Send the calcMessage
    last_message_type = 0;  // calcMessage is the current message type
    retries = 0;
    set_timer();
    send_calcMsg();
   


    // Receive the response from the server
    struct itimerval zero_timer = {{0, 0}, {0, 0}};
    ssize_t received_bytes = recvfrom(sock, &response_message, sizeof(response_message), 0, NULL, NULL);

    if (received_bytes > 0) {
       setitimer(ITIMER_REAL, &zero_timer, NULL);
    }

    // Cancel the timer since we received a response
   
    

    // Check if the server responds with type = 2 and terminate
    if (ntohs(response_message.type) == 2) {
        printf("Server responded with NOT OK. Terminating.\n");
        close(sock);
        return EXIT_FAILURE;
    }

    uint32_t n = ntohl(response_message.arith);
    int32_t i1 = ntohl(response_message.inValue1);
    int32_t i2 = ntohl(response_message.inValue2);
    double f1 = response_message.flValue1;
    double f2 = response_message.flValue2;

    double fresult = 0.0;
    int iresult = 0;

    if (n == 1 || n == 2 || n == 3 || n == 4) {
        printf("Assignment: i1: %d, i2:%d, ", i1, i2);
        if (n == 1) {
            printf("add\n");
            iresult = i1 + i2;
        } else if (n == 2) {
            printf("sub\n");
            iresult = i1 - i2;
        } else if (n == 3) {
            printf("mul\n");
            iresult = i1 * i2;
        } else {
            printf("div\n");
            if (i2 != 0)
                iresult = i1 / i2;
            else
                printf("Division by zero error.\n");
        }

    } else if (n == 5 || n == 6 || n == 7 || n == 8) {
        printf("Assignment f1: %8.8g, f2:%8.8g, ", f1, f2);
        if (n == 5) {
            printf("fadd\n");
            fresult = f1 + f2;
        } else if (n == 6) {
            printf("fsub\n");
            fresult = f1 - f2;
        } else if (n == 7) {
            printf("fmul\n");
            fresult = f1 * f2;
        } else {
            printf("fdiv\n");
            if (f2 != 0)
                fresult = f1 / f2;
            else
                printf("Division by zero error.\n");
        }
    } else {
        printf("Unknown operation.\n");
        close(sock);
        return EXIT_FAILURE;
    }

    // Prepare and send the response message (calcProtocol)
    response_message.type = htons(2);
    response_message.flResult = fresult;
    response_message.inResult = htonl(iresult);

    last_message_type = 1;  // calcProtocol is the current message type
    retries = 0;
    send_calcProt();
    set_timer();

    // Receive the final response from the server
    memset(&calcMsg, 0, sizeof(calcMsg));
    received_bytes = recvfrom(sock, &calcMsg, sizeof(calcMsg), 0, NULL, NULL);
    set_timer();
    if (received_bytes > 0) {
       setitimer(ITIMER_REAL, &zero_timer, NULL);
    }

    // Reset the timer frequency to 0
   
   
    // Check the server's reply
    if (ntohl(calcMsg.message) == 1) {
        printf("Server reply: OK\n");
    } else if (ntohl(calcMsg.message) == 2) {
        printf("Server reply: NOT OK\n");
    }

    close(sock);
    return 0;
}
