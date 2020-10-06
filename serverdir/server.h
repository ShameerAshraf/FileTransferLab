#ifndef SERVER_H
#define SERVER_H

// type cast pointer to struct socaddr_in* or struct socaddr_in6 *
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa);

// Convert string to Packet
void stringToPacket(struct packet *temp, char** inString);

#endif