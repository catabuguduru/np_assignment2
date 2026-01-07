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
#include <calcLib.h>
#include <protocol.h>
#include <time.h>
using namespace std;

#define DEBUG
#define MAX_JOBS 100

struct Job {
    uint32_t id;
    int type; // 1 = float, 2 = int
    struct sockaddr_in6 client_addr; // Use sockaddr_in6 to handle both IPv4 and IPv6
    uint32_t iresult;
    double fresult;
    time_t start_time;
    int active;
};

int sock;
struct calcMessage calcMsg; 
struct calcProtocol proto; 
struct Job job_list[MAX_JOBS];

/* Global address storage for send_calcMsg and send_calcProt */
struct sockaddr_storage current_client_addr;
socklen_t current_addr_len = sizeof(current_client_addr);

void send_calcMsg() {
    if (sendto(sock, &calcMsg, sizeof(calcMsg), 0, (struct sockaddr *)&current_client_addr, current_addr_len) < 0) {
        perror("Failed to send calcMessage");
    }
}

void send_calcProt() {
    if (sendto(sock, &proto, sizeof(proto), 0, (struct sockaddr *)&current_client_addr, current_addr_len) < 0) {
        perror("Failed to send calcProtocol");
    }
}

void calc(int idx) {
    char *arith = randomType();
    uint32_t op_id = rand() % 1000000;
    
    proto.type = htons(1);
    proto.id = htonl(op_id);
    proto.major_version = htons(1);
    proto.minor_version = htons(0);

    job_list[idx].id = op_id;
    job_list[idx].start_time = time(NULL);
    job_list[idx].active = 1;
    // Store the client address in the job slot
    memcpy(&job_list[idx].client_addr, &current_client_addr, current_addr_len);

    if (arith[0] == 'f') {
        job_list[idx].type = 1; 
        double f1 = randomFloat(); double f2 = randomFloat();
        proto.flValue1 = f1; proto.flValue2 = f2;
        if (strcmp(arith, "fadd") == 0) { proto.arith = htonl(5); job_list[idx].fresult = f1 + f2; }
        else if (strcmp(arith, "fsub") == 0) { proto.arith = htonl(6); job_list[idx].fresult = f1 - f2; }
        else if (strcmp(arith, "fmul") == 0) { proto.arith = htonl(7); job_list[idx].fresult = f1 * f2; }
        else if (strcmp(arith, "fdiv") == 0) { proto.arith = htonl(8); job_list[idx].fresult = f1 / f2; }
    } else {
        job_list[idx].type = 2;
        int i1 = randomInt(); int i2 = randomInt();
        proto.inValue1 = htonl(i1); proto.inValue2 = htonl(i2);
        if (strcmp(arith, "add") == 0) { proto.arith = htonl(1); job_list[idx].iresult = i1 + i2; }
        else if (strcmp(arith, "sub") == 0) { proto.arith = htonl(2); job_list[idx].iresult = i1 - i2; }
        else if (strcmp(arith, "mul") == 0) { proto.arith = htonl(3); job_list[idx].iresult = i1 * i2; }
        else if (strcmp(arith, "div") == 0) { proto.arith = htonl(4); job_list[idx].iresult = (i2 != 0) ? i1 / i2 : 0; }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: host:port\n"); return 1; }
    
    char *input = strdup(argv[1]);
    char *port_no = strrchr(input, ':');
    if (port_no == NULL) return 1;
    *port_no = '\0';

    initCalcLib();
    for(int i=0; i<MAX_JOBS; i++) job_list[i].active = 0;

    // connect_sock handles DNS and IPv4/IPv6 resolution
    sock = connect_sock(check_desthost(input), input, port_no + 1, atoi(port_no + 1), 1); 
  
    while (1) {
        uint8_t buffer[sizeof(struct calcProtocol)];
        current_addr_len = sizeof(current_client_addr);

        ssize_t received_bytes = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&current_client_addr, &current_addr_len);
        if (received_bytes < 0) continue;

       

        // --- PART 1: Handle Initial client message (calcMessage) ---
        if (received_bytes == sizeof(struct calcMessage)) {
            struct calcMessage *msg = (struct calcMessage*)buffer;      
            if (ntohs(msg->type) == 22 && ntohs(msg->protocol) == 17) {
                int idx = -1;
                for(int i=0; i<MAX_JOBS; i++) {
                    if(!job_list[i].active) { idx = i; break; }
                }
                if (idx != -1) {
                    calc(idx); 
                    send_calcProt(); 
                }
            } else {
                calcMsg.type = htons(2);
                calcMsg.message = htonl(2);
                send_calcMsg();
            }
        } 
        // --- PART 2: Handle Client Result (calcProtocol) ---
        else if (received_bytes == sizeof(struct calcProtocol)) {
            struct calcProtocol *res = (struct calcProtocol*)buffer;
            uint32_t rid = ntohl(res->id);
            int found = 0;
            time_t now = time(NULL);
            //printf("Result received for ID: %u\n", rid);
            
            for (int i = 0; i < MAX_JOBS; i++) {
                if (job_list[i].active && job_list[i].id == rid) {
                    int elapsed = now - job_list[i].start_time;
                    //printf("Found Job in slot %d. Elapsed time: %d seconds\n", i, elapsed);
                    if (elapsed > 10) {
                      //  printf("Applying REJECTION for lost client.\n");
                        job_list[i].active = 0;
                        break; 
                    }

                    double diff = 1.0;
                    if (job_list[i].type == 2) { 
                        // Cast to long to avoid unsigned subtraction ambiguity
                        diff = (double)labs((long)job_list[i].iresult - (long)ntohl(res->inResult));
                    } else { 
                        diff = abs(job_list[i].fresult - res->flResult);
                    }

                    memset(&calcMsg, 0, sizeof(calcMsg));
                    calcMsg.type = htons(2);
                    calcMsg.message = (diff < 0.0001) ? htonl(1) : htonl(2);
                    send_calcMsg();
                    
                    job_list[i].active = 0; 
                    found = 1;
                    break;
                }
            }
            if (!found) {
                memset(&calcMsg, 0, sizeof(calcMsg));
                calcMsg.type = htons(2);
                calcMsg.message = htonl(2); 
                send_calcMsg();
            }
        }
    }

    close(sock);
    return 0;
}