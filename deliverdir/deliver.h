#ifndef DELIVER_H
#define DELIVER_H

// type cast pointer to struct socaddr_in* or struct socaddr_in6 *
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa);

// Check existence of file
bool checkFileExists(const char * pathname);

// Convert packet to single string to send
void packetToString(struct packet *outPacket, char** temp);

// Get size of file
off_t fsize(const char *filename);

#endif