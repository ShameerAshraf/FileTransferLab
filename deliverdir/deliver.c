#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>

#include "packetStruct.h"
#include "deliver.h"
#define MAXBUFLEN 1500
#define ALPHA 0.125
#define BETA 0.25


int main (int argc, char *argv[]) {
    
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    if (argc != 3) {
        fprintf(stderr, "usage: deliver <server address> <server port number>\n");
        exit(1);
    }

    // Ask user for filename
    char command [256];
    char filename [256];
    char processInput[512];

    // User input and error-checking
    AGAIN: printf("Enter the filename using format: ftp <filename>\n");
    if (fgets(processInput, sizeof processInput, stdin) == NULL) goto AGAIN;
    else if (processInput[0] == '\n') goto AGAIN;
    else sscanf(processInput, "%s %s", command, filename);
    if (strcmp(command, "") == 0) goto AGAIN;
    if (strcmp(filename, "") == 0) goto AGAIN;

    // Check file exists
    char messageSendOne [10];
    bool exists = checkFileExists(filename);
    if (exists) {
        strcpy(messageSendOne, command);  // Set message to input
    }
    else {      // Exit here, add functionality for server to timeout and exit if 1st msg isnt received
        printf("Exiting\n");
        exit(0);
    }
    
    // Set all values to zero, especially for sin_zero[]
    // padding elements in sockaddr_in
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    
    // Returns servinfo as linked list of struct addrinfo
    // arguments (hostname or IP, port number or service e.g. html,ftp,telnet, 
    // addrinfo with manually filled values, returned linked list with addrinfos)
    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1; 
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("deliver: socket");
            continue; 
        }
        break; 
    }
    
    if (p == NULL) {
        fprintf(stderr, "deliver: failed to create socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    // // // Start timer for round trip time, Includes processing time of request at server // // //
    struct timeval start, end;
    double t1, t2;
    if(gettimeofday(&start,NULL)) {
    printf("time failed\n");
    exit(1);
    }


    // Send inital message to server
    if ((numbytes = sendto(sockfd, messageSendOne, strlen(messageSendOne), 0, p->ai_addr, p->ai_addrlen)) == -1) {
        perror("deliver: sendto");
        exit(1); 
    }
    
    //freeaddrinfo(servinfo);
        
        // Setup variables to rcvfrom() at same socket sockfd used to sendto()
        // sockfd already bound by sendto()
        socklen_t addr_len;
        struct sockaddr_storage their_addr;
        char buf[MAXBUFLEN];
        addr_len = sizeof their_addr;
        char s[INET6_ADDRSTRLEN];

        // Timeout in case Server not set up before deliver called
        struct pollfd ufd[1];   // contains socket descriptor, description of events to check/occuring at socket
        ufd[0].fd = sockfd; // set socket descriptor to our socket in use
        ufd[0].events = POLLIN;
        int pollTemp = poll(ufd, 1, 10000); // wait for events on the socket, 10s timeout

        if (pollTemp == -1) {
            perror("poll");
        }
        else if (pollTemp == 0) {
            printf("Timeout occured. No data received from server (server was not setup)\nClosing socket\n");
            close(sockfd);
            exit(1);
        }
        else {  // Receive "yes" or "no" from server
            if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            exit(1);
            }
        }


    // // // End time for round trip time // // //
    if(gettimeofday(&end,NULL)) {
    printf("time failed\n");
    exit(1);
    }
    t1+=start.tv_sec+(start.tv_usec/1000000.0);
    t2+=end.tv_sec+(end.tv_usec/1000000.0);
    //calculate and print SampleRTT
    double sampleRTT = 1000*(t2-t1);
    printf("First Round Trip Time = %g ms\n", sampleRTT);


    // Open the file and calculate packets required
    FILE *fptr;
    fptr = fopen(filename, "rb");
    
    int sz = fsize(filename);
    int total_frag = (sz / 1000) + 1;
    
    // Distribute segments into packets
    //struct packet dataToSend[total_frag];
    struct packet * dataToSend = NULL;
    dataToSend = (struct packet*) malloc((total_frag)*sizeof(struct packet));
    for (int j = 0; j < total_frag; j++) {
        struct packet basic;
        basic.frag_no = j + 1;
        basic.filename = filename;
        size_t k = fread(basic.filedata, sizeof(char), 1000, fptr);
        basic.size = k;
        basic.total_frag = total_frag;
        dataToSend[j] = basic;
    }

    // String variable for storing packet data to transmit
    char *packetInString = NULL;    
    packetInString = (char*) malloc((1500)*sizeof(char)); /*+1 for '\0' character */
  
    // If message received is yes, begin File Transfer
    char expectedMessage [10] = "yes";
    bool checked = false;
    for (int i = 0; i < numbytes; i++) {
        if (buf[i] == expectedMessage[i]) checked = true;
        else {
            checked = false;
            break;
        }
    }
    // Clear buffer just in case
    bzero(buf, sizeof(buf));

    // Set up EstimatedRTT, DevRTT using SampleRTT from above
    // Set up timeoutInterval arbitrarily
    double estimatedRTT = sampleRTT;
    double devRTT = sampleRTT;
    int timeoutInterval = 5000;
    // bool to turn off timer of retransmission
    bool retransmitted = false;
    int numRetransmissions = 0;
    // Set up variables for RTT measurement for packets
    struct timeval packetSentTime, ackReceivedTime;
    double tSent, tReceived;


    if (checked) {
        printf("A file transfer can start.\n"); 
        // Start converting packets into strings and send with ACK
        for (int i = 0; i < total_frag; i++) {
            packetToString(&dataToSend[i], &packetInString);

            TRYAGAIN:
            if ((numbytes = sendto(sockfd, packetInString, MAXBUFLEN-1, 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
                perror("deliver: sendto");
                exit(1); 
            }
            
            // Record start time for this packet
            if(gettimeofday(&packetSentTime,NULL)) {
            printf("time failed\n");
            exit(1);
            }
            
            int pollTemp = poll(ufd, 1, timeoutInterval); // wait for ACK , timoutInterval ms timeout then send again
            if (pollTemp == -1) {
                perror("poll");
            }
            else if (pollTemp == 0) {
            	retransmitted = true;		  // Retransmission, no timer calculations
            	numRetransmissions++;
            	if (numRetransmissions > 3) {
            		printf("Retransmitted 4 times. Exiting client\n");
            		exit(1);
            	}
            	printf("Retransmitting packet %d\n", i);
                goto TRYAGAIN;                // Timed out, resend packet!
            }
            else {
                if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
                perror("recvfrom");
                exit(1);
                }
                retransmitted = false;
                numRetransmissions = 0;
            }

            // Record end time for this packet
            if(gettimeofday(&ackReceivedTime,NULL)) {
            printf("time failed\n");
            exit(1);
            }
            tSent = packetSentTime.tv_sec+(packetSentTime.tv_usec/1000000.0);
            tReceived = ackReceivedTime.tv_sec+(ackReceivedTime.tv_usec/1000000.0);
            
            // Timeout calculation for next trip
            if (!retransmitted) {
            	sampleRTT = 1000*(tReceived - tSent);
            	estimatedRTT = ((1 - ALPHA) * estimatedRTT) + (ALPHA * sampleRTT);
            	devRTT = ((1 - BETA) * devRTT) + (BETA * fabs(sampleRTT - estimatedRTT));
            	timeoutInterval = (int) estimatedRTT + (4 * devRTT);
            }
            // Timeout interval falls below milliseconds, poll timeout cannot go below that
            // Fixed by setting minimum to 2ms
            if (timeoutInterval < 2) timeoutInterval = 2;

            int ACK_NACK;
            ACK_NACK = atoi(buf);
            //if (ACK_NACK == 20) printf("Round Trip Time for packet %d = %d ms\n", ACK_NACK, timeoutInterval);
            //if (ACK_NACK == 50) printf("Round Trip Time for packet %d = %d ms\n", ACK_NACK, timeoutInterval);
            //if (ACK_NACK == 100) printf("Round Trip Time for packet %d = %d ms\n", ACK_NACK, timeoutInterval);
            bzero(buf, sizeof(buf));
            if (ACK_NACK == i + 1) continue;
            else goto TRYAGAIN;
        }
    }
    else {
        printf("Exiting\n");   // close socket and exit
    }
    
    // Free memory allocated and close socket
    free(packetInString);
    free(dataToSend);
    close(sockfd);
    
    return 0; 
}

// type cast pointer to struct socaddr_in* or struct socaddr_in6 *
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
}
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Check existence of file
bool checkFileExists(const char * pathname) {
    bool exists = false;
    if (access(pathname, F_OK) != -1) {
        return exists = true;   // File exists
    }
    else {
        return exists = false;  // File does not exist
    }
}

// Convert packet to single string to send
void packetToString(struct packet *outPacket, char** temp) {
    
    char *result = *temp;
    strcpy(result, "");
    char holder [7];
    
    sprintf(holder, "%d", outPacket->total_frag);
    strcat(result, holder);
    strcat(result, ":");
    
    sprintf(holder, "%d", outPacket->frag_no);
    strcat(result, holder);
    strcat(result, ":");
    
    sprintf(holder, "%d", outPacket->size);
    strcat(result, holder);
    strcat(result, ":");
    
    strcat(result, outPacket->filename);
    strcat(result, ":");
    
    char target = '\0';
    result = strchr(result, target);
    for (int i = 0; i < outPacket->size; i++) {
        *result = outPacket->filedata[i];
        ++result;
    }

    return;
}


off_t fsize(const char *filename) {
    struct stat st; 

    if (stat(filename, &st) == 0)
        return st.st_size;
    fprintf(stderr, "Cannot determine size of %s: %s\n",
            filename, strerror(errno));

    return -1; 
}
