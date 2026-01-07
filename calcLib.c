#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <strings.h>
#include <sys/types.h>
#include <netdb.h>
/* Here we use " as the calcLib.c and calcLib.h files are in the same folder, and are to be BUILT
   to into a library, that will be included in other files. 

   This is a C lib, and will be built as such.
   
*/ 
#include "calcLib.h"


/* array of char* that points to char arrays.  */ 
char *arith[]={"add","div","mul","sub","fadd","fdiv","fmul","fsub"};

/* Used for random number */
time_t myData_seedValue;

int initCalcLib(void){
  /* Init the random number generator with a seed, based on the current time--> should be randomish each time called */
  srand((unsigned) time(&myData_seedValue));
  return(0);
}

int initCalcLib_seed(unsigned int seed){
  /* 
     Init the random number generator with a FIXED seed, will allow us to grab random numbers 
     in the same sequence all the time. Good when debugging, bad when running live. 

     This is 'messy' for more details see https://en.wikipedia.org/wiki/Pseudorandom_number_generator. 

     DO NOT USE rand() for production, wher you NEED good random numbers. 
  */
  
  myData_seedValue=seed;
  srand(seed);
  return(0);
}
  
char *randomType(void){
  int Listitems=sizeof(arith)/(sizeof(char*)); 
  /* Figure out HOW many entries there are in the list.
     First we get the total size that the array of pointers use, sizeof(arith). Then we divide with 
     the size of a pointer (sizeof(char*)), this gives us the number of pointers in the list. 
  */
  int itemPos=rand() % Listitems;
  /* As we know the number of items, we can just draw a random number and modulo it with the number 
     of items in the list, then we will get a random number between 0 and the number of items in the list 
     
     Using that information, we just return the string found at that position arith[itemPos];
  */
  return(arith[itemPos]);
  
};

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
int connect_sock(int address_type, char*Desthost, char *Destport, int port, int is_server){
   int sock=-1;
   
    if (address_type == 0) {
        printf("Invalid IP address type\n");
        return -1;
    }
   if (address_type == 1) { // IPv4
        struct sockaddr_in addr;
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("Cannot create IPv4 socket");
            return -1;
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, Desthost, &addr.sin_addr) <= 0) {
            perror("Invalid IPv4 address");
            close(sock);
            return -1;
        }

        if (is_server) {
            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                perror("Bind failed");
                close(sock);
                return -1;
            }
        } else {
            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                perror("Connect failed");
                close(sock);
                return -1;
            }
        }

    } else if (address_type == 2) { // IPv6
        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));

        sock = socket(AF_INET6, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("Cannot create IPv6 socket");
            return -1;
        }

        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, Desthost, &addr6.sin6_addr) <= 0) {
            perror("Invalid IPv6 address");
            close(sock);
            return -1;
        }

        if (is_server) {
            if (bind(sock, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
                perror("Bind failed");
                close(sock);
                return -1;
            }
        } else {
            if (connect(sock, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
                perror("Connect failed");
                close(sock);
                return -1;
            }
        }

    } else if (address_type == 3) { // Hostname
        struct addrinfo hints, *res, *rp;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        int status = getaddrinfo(Desthost, Destport, &hints, &res);
        if (status != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
            return -1;
        }

        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0) continue;

            if (is_server) {
                if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
            } else {
                if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
            }

            close(sock);
            sock = -1;
        }

        if (rp == NULL) {
            fprintf(stderr, "Unable to attach to any address\n");
            freeaddrinfo(res);
            return -1;
        }

        freeaddrinfo(res);
    }

    return sock;
}


int randomInt(void){
  /* Draw a random interger between o and RAND_MAX, then modulo this with 100 to get a random 
     number between 0 and 100. */
  
  return( rand()%100 );
};


double randomFloat(void){
  /* The same as for the interber, but for a double, and without the modulo. We cant use 
     the module approach as it would generate integers, which we do not want. */
  double x = (double)rand()/(double)(RAND_MAX/100.0);
  return(x);
};


