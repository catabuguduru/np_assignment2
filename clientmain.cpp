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

//#define DEBUG
#include <protocol.h>
#include <calclib.h>
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
    int address_type = check_desthost(Desthost);
    sock = connect_sock(address_type, Desthost, Destport,port,0);
    if(sock < 1){
        printf("failed to connect to the address\n");
        return EXIT_FAILURE;
    }
#ifdef DEBUG
    printf("Connected to %s:%d and local.\n", Desthost, port);
#endif
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
    send_calcMsg();
    set_timer();
   
    // Receive the response from the server
    struct itimerval zero_timer = {{0, 0}, {0, 0}};
    ssize_t received_bytes = recvfrom(sock, &response_message, sizeof(response_message), 0, NULL, NULL);

    if (received_bytes > 0) {
       setitimer(ITIMER_REAL, &zero_timer, NULL);
    } 

    // Check if the server responds with type = 2 and terminate
    if (ntohs(response_message.type) == 2) {
        printf("Server responded with NOT OK\n");
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
    int type;

    if (n == 1 || n == 2 || n == 3 || n == 4) {
        type = 1;
        printf("Assignment: ");
        if (n == 1) {
            printf("add %d %d\n ", i1, i2);
            iresult = i1 + i2;
        } else if (n == 2) {
            printf("sub %d %d\n ", i1, i2);
            iresult = i1 - i2;
        } else if (n == 3) {
            printf("mul %d %d\n ", i1, i2);
            iresult = i1 * i2;
        } else {
            printf("div %d %d\n ", i1, i2);
            if (i2 != 0)
                iresult = i1 / i2;
            else
                printf("Division by zero error.\n");
        }

    } else if (n == 5 || n == 6 || n == 7 || n == 8) {
        type = 2;
        printf("Assignment ");
        if (n == 5) {
            printf("fadd %8.8g %8.8g\n", f1, f2);
            fresult = f1 + f2;
        } else if (n == 6) {
            printf("fsub %8.8g %8.8g\n", f1, f2);
            fresult = f1 - f2;
        } else if (n == 7) {
            printf("fmul %8.8g %8.8g\n", f1, f2);
            fresult = f1 * f2;
        } else {
            printf("fdiv %8.8g %8.8g\n", f1, f2);
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
    //sleep(15); //test timeout functionality
    send_calcProt();
    set_timer();

    // Receive the final response from the server
    memset(&calcMsg, 0, sizeof(calcMsg));
    received_bytes = recvfrom(sock, &calcMsg, sizeof(calcMsg), 0, NULL, NULL);
    if (received_bytes > 0) {
       setitimer(ITIMER_REAL, &zero_timer, NULL);
    }
  
    // Check the server's reply
   
    if (type == 1) {
    if (ntohl(calcMsg.message) == 1) {
        printf("OK (myresult=%d)\n", iresult);
    } else if (ntohl(calcMsg.message) == 2) {
        printf("NOT OK\n");
    } 
   } else if (type == 2) {
        if (ntohl(calcMsg.message) == 1) {
        printf("OK (myresult=%8.8g)\n", fresult);
    } else if (ntohl(calcMsg.message) == 2) {
        printf("NOT OK\n");
    } 
   }
   

    close(sock);
    return 0;
}
