#ifndef PACKET_STRUCT
#define PACKET_STRUCT

// All members sent as single string but filedata might contain binary data
// Packet can contain maximum 100 bytes, split into multiple packets for larger data
struct packet {
      unsigned int total_frag;		// total number of fragments of file
      unsigned int frag_no;			// sequence number of fragment/packet in file starting 1...
      unsigned int size;			// size of data, 0 to 1000
      char* filename;
      char filedata[1000];
 };

#endif