# TCP
Custom TCP implementation

## Group Members
- Francesco Agosti
- Grace Chen
- TC Dong
- Michael Ren

## File Layout
- src/: Source files
  - 3a/: Part 3a source files
    - reliable.c: C source file for 3a implementation
    - rlib.c|h: C source and header files for 3a helper library
    - Makefile: Makefile to build 3a
    - reference: Reference implementation of 3a
    - stripsol: ???
    - tester: Tester binary
    - tester-src/: Tester source, in Haskell
  - 3b/: Part 3b source files
    - generator: Random file generator binary
    - relayer/: Relayer binary and config file
      - config.xml: Bundled relayer configuration (from zip of 3b)
      - config-standalone.xml: Configuration from project page
      - relayer: relayer binary
      - measure_bandwidth/: Relayer bandwidth measurement utility source files
        - readme: README
        - receiver.cpp: Receiver end of measurement utility
        - sender.cpp: Sender end of measurement utility
    - reliable/: Implementation of 3b
      - reliable.c: C source file for 3b implementation
      - rlib.c|h: C source and header files for 3b helper library
      - Makefile: Makefile to build 3b
      - stripsol: ???

NOTE: (rlib|reliable).(c|h) are not the same between 3a and 3b

## Part A: Sliding Window Protocol
### Implementation of helper file packet_list.c
Our packet\_list object, defined in packet\_list.c, basically holds a packet\_t and pointers to next and prev packet\_t objects, allowing us to create and maintain a list of packet\_t objects. We use this object to create our send and receive buffers.

packet\_list.c also holds several helper methods that aid the implementation of reliable.c. For example, there are various methods to handle the creation of a packet (new\_packet), the deletion/insertion of a packet from a buffer (remove\_head\_packet, insert\_packet\_after\_seqno, append\_packet), obtaining certain information from a packet buffer (packet\_list\_size, packet\_data\_size), and the serialization of the data across multiple packets into a single character buffer for output (serialize\_packet\_data).
### Implementation of reliable.c
#### rel_t object 
Besides the given objects, rel\_t cotains a send buffer (packet\_list *), a receive buffer (packet\_list *), unsigneds int next\_seqno\_to\_send and next\_seqno\_expected, and a struct config\_common object, defined in rlib.h.
#### rel_create 
rel\_create initializes an instance of a rel\_t object and sets all of the data fields within the rel\_t object accordingly. For example, the four eof flags (which we use to determine when to call rel\_output) are set to 0, the send and receive buffers are set to NULL, and next\_seqno\_expected is set to 1.
#### rel_destroy
rel\_destroy calls conn\_destroy on the rel\_t object passed in, and frees the send and receive buffers.
#### rel_recvpkt
rel\_recvpkt does various checks on the incoming packet, including packet length, whether the reported length matches the actual length, validity of ackno, and checksum. This method calls send\_ack, handle\_ack, rel\_read, rel\_output, and insert\_packet\_in\_order (from packet\_list.c) when appropriate. 
#### rel_read
rel\_read reads in a given input and breaks the input into chunks that are at most the maximum data size for a packet, creates a packet and sets its values for each chunk of data, calls conn\_sendpkt for each packet, and appends the packet to the send buffer using a function in packet\_list.c.
#### rel_output
rel\_output will call conn\_bufspace to obtain the maximum amount of data that can be outputted, before comparing that maximum to the size of the data that is currently stored in receive\_buffer. Whichever value is smaller is to be the amount of data copied from the receive\_buffer. rel\_output then calls serialize\_packet\_data from packet\_list.c to have the correct amount of data copied into a character buffer, which is then passed to conn\_output. rel\_output then updates the head pointer of the receive\_buffer; the data written to output no longer needs to be stored in the buffer.
#### rel_timer
rel\_timer calls resend\_packets (a helper function described below).
#### Helper Functions
##### enforce_destroy
enforce\_destroy checks the eof flags; if all are set, it calls rel\_destroy on the rel_t object passed in.
##### handle_ack
handle\_ack checks the seqno of the ACK passed in, and sets the eof flag indicating that the other end has finished receiving if the seqno is larger than the final seqno. This function then removes all packets in the send buffer that have become ackowledged.
##### resend_packets
resend\_packets iterates through the send buffer and calls conn\_sendpkt on each packet.
##### send_ack, is_eof_packet
Pretty straightforward.
##### handle_eof_packet
Calls conn\_output with a null pointer for the packet to be sent; sets the eof flag for having finished sending the file; and calls enforce\_destroy to see if it is appropriate to destroy the connection.
## Part B: Congestion Control
### Implementation of reliable.c
#### rel_read
