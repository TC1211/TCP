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
#### rel_destroy
#### rel_recvpkt
#### rel_read
#### rel_output
rel\_output will call conn\_bufspace to obtain the maximum amount of data that can be outputted, before comparing that maximum to the size of the data that is currently stored in receive\_buffer. Whichever value is smaller is to be the amount of data copied from the receive\_buffer. rel\_output then calls serialize\_packet\_data from packet\_list.c to have the correct amount of data copied into a character buffer, which is then passed to conn\_output. rel\_output then updates the head pointer of the receive\_buffer; the data written to output no longer needs to be stored in the buffer.
#### rel_timer

## Part B: Congestion Control
