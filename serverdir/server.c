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

#include "packetStruct.h"
#include "server.h"
#define MAXBUFLEN 1500


int main (int argc, char *argv[]) {

	if (argc != 2) {
		fprintf(stderr, "usage: server <UDP listen port>\n");
		exit(1);
	}
	else if (argc == 2) {
		; // Ignore
	}

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof hints);
    
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4, AF_INET6 to force IPv6
    hints.ai_socktype = SOCK_DGRAM; // Use socket_datagram
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
	}
    
    // Loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue; 
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
        	close(sockfd); 
        	perror("server: bind");
			continue; 
		}
		break; 
	}
    
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind socket\n");
        return 2;
    }
    
    freeaddrinfo(servinfo);

    addr_len = sizeof their_addr;

    // Timeout in case deliver has not located file and exited the program
    struct pollfd ufd[1];	// contains socket descriptor, description of events to check/occuring at socket
    ufd[0].fd = sockfd;	// set socket descriptor to our socket in use
    ufd[0].events = POLLIN;
    int pollTemp;
    
    pollTemp = poll(ufd, 1, 30000);	// wait for events on the socket, 30s timeout

    if (pollTemp == -1) {
    	perror("poll");
    }
    else if (pollTemp == 0) {
    	printf("Timeout occured. No data received from deliver within 30s (file not found by deliver)\nClosing socket\n");
    	close(sockfd);
    	exit(1);
    }
    else {
    	if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
	   }
    }
    
    // Analyize request
    char expectedMessage [10] = "ftp";
    bool checked = false;
    for (int i = 0; i < numbytes; i++) {
    	if (buf[i] == expectedMessage[i]) checked = true;
    	else {
    		checked = false;
    		break;
    	}
    }

    // Process request
    if (checked) {	// 1 if ftp, 0 if false
    	// ftp request received -  reply yes
    	char messageSendOne [10] = "yes";
    	if ((numbytes = sendto(sockfd, messageSendOne, strlen(messageSendOne), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
        perror("server: sendto");
        exit(1); 
    	}
    }
    else {
    	// Unknown request - reply no
    	char messageSendOne [10] = "no";
    	if ((numbytes = sendto(sockfd, messageSendOne, strlen(messageSendOne), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
        perror("server: sendto");
        exit(1); 
    	}
    }


    // Wait for first packet, then open filestream with /filename/
    int frag_no = -1;
    int total_frag = -1;
    int dataSize = 0;
    while (frag_no != 1) {
    pollTemp = poll(ufd, 1, 10000); // wait for events on the socket, 10s timeout

    if (pollTemp == -1) {
        perror("poll");
    }
    else if (pollTemp == 0) {
        printf("Timeout occured. No packet data received from deliver\nClosing socket\n");
        close(sockfd);
        exit(1);
    }
    else {
        if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
    }
    }

        char *ptr = buf;
        struct packet first;
        stringToPacket(&first, &ptr);

        // Set variables for use in processing and outputting to file
        total_frag = first.total_frag;
        frag_no = first.frag_no;
        dataSize = first.size;

        if (frag_no == 1) break;
    }

    // Skip 3 semicolons
    int semis = 0; char *ptr;
    for (int i = 0; i < numbytes; i++) {
        if (buf[i] == ':') semis++;
        if (semis == 3) {
            ptr = &buf[i];
            break;
        } 
    }

    ptr++;
    const char ch = ':';
    char *endptr;        
    endptr = strchr(ptr, ch);
    char filename [100];
    int i = 0;
    while(ptr != endptr) {
        filename[i] = *ptr;
        i++; ptr++;
    }
    filename[i] = '\0';
    

    // Open the file to write to
        FILE *fptr;
        fptr = fopen(filename, "wb");
        
        
    // Read and write filedata
    ptr++;
    char filedata[1000];
    for (int j = 0; j < dataSize; j++) {
        filedata[j] = *ptr;
        ptr++;
    }
    ptr = NULL;
        
    // Since data is arriving in the correct order, we can write directly
    // to the output file
    fwrite(filedata, dataSize, 1, fptr);
    // Clear the buffer and filedata array just in case
    memset(filedata, '\0', 1000*sizeof(char));
    bzero(buf, sizeof(buf));

    int frag_received = 1;
    char ACK[10];
    sprintf(ACK, "%d", frag_received);
    if ((numbytes = sendto(sockfd, ACK, strlen(ACK), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
        perror("server: sendto");
        exit(1); 
    }
    
    // Track received packets
    bool *writtenToFile = NULL;
    writtenToFile = (bool*) malloc((dataSize)*sizeof(bool));
    for (int i = 0; i < dataSize; i++) writtenToFile[i] = false;
    writtenToFile[frag_received - 1] = true;

    
    // Until all packets have arrived    
    while (frag_received != total_frag) {
        RECV_AGAIN:
        if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
        }
        
        struct packet temp;
        ptr = buf;
        stringToPacket(&temp, &ptr);

        // If packet was already written to file before
        if (writtenToFile[temp.frag_no - 1] == true){ 
            printf("Packet already received\n");
            goto RECV_AGAIN;
        }

        // Skip 4 semicolons
        int semis = 0; char *ptr;
        for (int i = 0; i < numbytes; i++) {
            if (buf[i] == ':') semis++;
            if (semis == 4) {
                ptr = &buf[i];
                break;
            } 
        }

        ptr++;
        for (int k = 0; k < temp.size; k++) {
            filedata[k] = *ptr;
            ptr++;
        }

        fwrite(filedata, temp.size, 1, fptr);
        // Clear buffer and filedata array just in case
        memset(filedata, '\0', 1000*sizeof(char));
        bzero(buf, sizeof(buf));
        ptr = NULL;

        // Update info on received packets
        writtenToFile[frag_received - 1] = true;

        // Send ACK for received upto frag_received
        frag_received++;
        sprintf(ACK, "%d", frag_received);
        //if (frag_received == 2) goto SKIPTHIS;
        if ((numbytes = sendto(sockfd, ACK, strlen(ACK), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
            perror("server: sendto");
            exit(1); 
        }
        //SKIPTHIS: ;
    }   // End of while loop

    // Close the file and socket
    fclose(fptr);
    free(writtenToFile);
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

// Convert string to Packet
void stringToPacket(struct packet *temp, char** inString) {
        temp->total_frag = strtol(*inString, inString, 10);
        (*inString)++;
        
        temp->frag_no = strtol(*inString, inString, 10);
        (*inString)++;
        
        temp->size = strtol(*inString, inString, 10);
        (*inString)++;

        return;
}
