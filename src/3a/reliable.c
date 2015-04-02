#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include "rlib.h"

#include <stdbool.h>
#include "packet_list.c"

// TODO:
// - multiple connections

#define MAX_PACKET_DATA_SIZE 500

// The mapping from rel_t to conn_t is one-to-one; for every connection, there is one
// rel_t and one conn_t instance.
// rel_t also contains a linked list for traversing all connections
struct reliable_state {
	rel_t *next; /* Linked list for traversing all connections */
	rel_t **prev;
	conn_t *c; /* This is the connection object */
	/* Add your own data fields below this */
	packet_list* send_buffer;
	unsigned int next_seqno_to_send;

	packet_list* receive_buffer;
	unsigned int next_seqno_expected;

	const struct config_common *config;
};
rel_t *rel_list;

void send_ack(rel_t *r);
void resend_packets(rel_t *rel);

/* Creates a new reliable protocol session, returns NULL on failure.
* Exactly one of c and ss should be NULL. (ss is NULL when called
* from rlib.c, while c is NULL when this function is called from
* rel_demux.) */
// 1) if this is a new connection the conn_t is NULL
// 2) if the connection has already been created,
// then there is no need for the sockaddr_storage, so it will be NULL
// During startup, an initial connection is created for you, leading to 2)
// During runtime, if a new connection is created, then you have to deal with 1)
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss, const struct config_common *cc)
{
	rel_t *r;
	r = xmalloc (sizeof (*r));
	memset (r, 0, sizeof (*r));
	if (!c) {
		c = conn_create (r, ss);
		if (!c) {
		free (r);
		return NULL;
		}
	}
	r->c = c;
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list)
		rel_list->prev = &r->next;
	rel_list = r;
	/* Do any other initialization you need here */
	r->send_buffer = NULL;
	r->next_seqno_to_send = 1;
	r->receive_buffer = NULL;
	r->next_seqno_expected = 1;
	r->config = cc;
	return r;
}
void
rel_destroy (rel_t *r)
{
	if (r->next)
	r->next->prev = r->prev;
	*r->prev = r->next;
	conn_destroy (r->c);
	/* Free any other allocated memory here */
	while (r->send_buffer) {
		remove_head_packet(&r->send_buffer);
	}
	while (r->receive_buffer) {
		remove_head_packet(&r->receive_buffer);
	}
}

/* This function only gets called when the process is running as a
* server and must handle connections from multiple clients. You have
* to look up the rel_t structure based on the address in the
* sockaddr_storage passed in. If this is a new connection (sequence
* number 1), you will need to allocate a new conn_t using rel_create
* (). (Pass rel_create NULL for the conn_t, so it will know to
* allocate a new connection.)
*/
// Note: This is only called in server mode, i.e. when you supply the -s option when running
// This will add a new node to the linked list if this is a new connection
// if not, ???
void
rel_demux (const struct config_common *cc, const struct sockaddr_storage *ss, packet_t *pkt, size_t len)
{
}

// For receiving packets; these are either ACKs (for sending) or data packets (for receiving)
// For receiving: read in and buffer the packets so they can be consumed by by rel_output
// For sending: update how many unACKed packets there are
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	// first check validity
	if (!cksum(pkt, pkt->cksum))	return;
	
	//seq_obj *head = r->head;
	if(n == 8){ // Acks
		if (r->send_buffer_metadata.last_ack+1 == pkt->ackno)	r->send_buffer_metadata.last_ack++;
		return;
	} 
	else if (n > 12 && pkt->seqno > 0){ //receiver
		uint32_t next_byte = (int *) *(r->receive_buffer_metadata.next_byte_expected);
		char *copy = r->receive_buffer;

		if(r->receive_buffer_metadata.next_seqno_expected == pkt->seqno){ // correct order
			int b;
			int d = 0;
			for (b = next_byte; b < next_byte + n-12; b++){
				*(copy+b) = pkt->data[d];
				d++;
			}
			(uint32_t *) *(r->receive_buffer_metadata.next_byte_expected) += n - 12;
			(uint32_t *) *(r->receive_buffer_metadata.last_byte_received) += n - 12;
		
		} else { // not in order
			receiveBuf_t *receive_head = r->receive_ordering_buf;
			bool existing_chunk = false;
			while(receive_head != NULL){
				if(receive_head->tail == pkt->seqno){
					receive_head->tail++;
					receive_head->len = n - 12;
					strncat(receive_head->data, pkt->data, n - 12);
					existing_chunk = true;
				}
				receive_head = receive_head->next;
			}
			receive_head = r->receive_ordering_buf;
			if(!existing_chunk){
				while(receive_head->next!=NULL)		receive_head = receive_head->next;
				receive_head->next = malloc(sizeof(receiveBuf_t));
				receive_head->next->head = pkt->seqno;
				receive_head->next->tail = pkt->seqno + 1;
				receive_head->next->len = n - 12;
				strncat(receive_head->next->data, pkt->data, n-12);
			}
		}
		// now fill in the spaces
		bool gap_to_fill = true;
		uint32_t byte_to_fill;

		int o, tail_seqno;
		while(gap_to_fill){
			gap_to_fill = false;
			receiveBuf_t* receive_head = r->receive_ordering_buf;
			byte_to_fill = r->receive_buffer_metadata.next_byte_expected;
			while(receive_head != NULL){
				if(receive_head->head == byte_to_fill){
					copy = r->receive_buffer;
					strncpy(copy+byte_to_fill, receive_head->data, receive_head->len);
					(uint32_t*) *(r->receive_buffer_metadata.next_byte_expected) += receive_head->len;
					gap_to_fill = true;
					tail_seqno = receive_head->tail - 1;
				}
				receive_head = receive_head->next;
			}
		if(pkt->seqno > tail_seqno)
			(uint32_t*) *(r->receive_buffer_metadata.last_byte_received) += (pkt->seqno - tail_seqno)*500;
		else
			(uint32_t*) *(r->receive_buffer_metadata.last_byte_received) = 	(uint32_t*) *(r->receive_buffer_metadata.next_byte_expected);
				
		return;
	}
	return;
}

// read in data using conn_input, break this data into packets, send the packets,
// and update how many unacked packets there are
/*
To get the data that you must transmit to the receiver, keep calling conn_input until it drains. conn_input reads data from standard input. If no data is available at the moment, conn_input will return 0. Do not loop calling conn_input if it returns 0, simply return. At the point data become available again, the library will call rel_read for ONCE, so you can read from conn_input again. When an EOF is received, conn_input will return -1. Also, do NOT try to buffer the data from conn_input more than expected. The sender's window is the only buffer you got. When the window is full, break from the loop even if there could still be available data from conn_input. When later some ack packets are received and some slots in sender's window become vacant again, call rel_read.
 
 
 struct packet {
	uint16_t cksum;
	uint16_t len;
	uint32_t ackno;
	uint32_t seqno;		 Only valid if length > 8
    char data[500];
    };
 typedef struct packet packet_t;
 */
void
rel_read (rel_t *s)
{


    int bytes_recv = 1;
    while (bytes_recv > 0) {
        packet_t new_packet;
        bytes_recv = conn_input(s->c, new_packet.data, MAX_PACKET_DATA_SIZE);

        
        new_packet.cksum = 0;
        new_packet.len = bytes_recv+12;
        //new_packet.ackno = ?
        //new_packet.seqno = ?
        
        //if LastByteWritten - LastByteAcked <= MaxSendBuffer and
        //   LastByteSent - LastByteAcked <= Advertised Window
        conn_sendpkt(s->c, &new_packet, sizeof(new_packet));
        //
        
        //else break;

        
        s->send_buffer_metadata.last_byte_sent++;
        s->send_buffer_metadata.last_byte_written++;
        //s->send_buffer_metadata.last_byte_acked;
        
        
    }
   //conn_input
    //conn_send_pckt
}

void send_ack(rel_t *r) {
	packet_t *ack = malloc(sizeof(packet_t));
	memset(ack, 0, sizeof(packet_t));
	ack->len = (uint16_t) sizeof(packet_t);
	// ack->ackno = (uint32_t) *r->receive_buffer_metadata.next_byte_expected; doesn't work ;___;
	ack->cksum = cksum((void *)ack, sizeof(packet_t));
	conn_sendpkt(r->c, ack, sizeof(packet_t));
	free(ack);
	return;
}

// Consume the packets buffered by rel_recvpkt; call conn_bufspace to see how much data
// you can flush, and flush using conn_output
// Once flushed, send ACKs out, since there is now free buffer space
void rel_output (rel_t *r) {
	int total = &(r->receive_buffer_metadata.last_byte_received) - &(r->receive_buffer_metadata.last_byte_read);
	if (total > 0) {
		int check = conn_bufspace(r->c);

		if (check < total) {
			printf("Insufficient output space you fool\n");
			return;
		}

		char *data = (char *)r->receive_buffer_metadata.last_byte_read;
		data++;
		int output = conn_output(r->c, data, total);
		if (output != 0) {
			printf("conn_output returned with value %d which I don't think is a good thing\n", output);
			return;
		}
		//update metadata of receive buffer:
		r->receive_buffer_metadata.last_byte_read += total;
	}
}
// Retransmit any unACKed packets after a certain amount of time
void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	if (rel_list) {
		resend_packets(rel_list);
	}
	rel_t *rel_list_fwd = rel_list;
	while (rel_list_fwd->next) {
		rel_list_fwd++;
		resend_packets(rel_list_fwd);
	}
	rel_t *rel_list_bwd = rel_list;
	while (rel_list_bwd->prev) {
		rel_list_bwd--;
		resend_packets(rel_list_bwd);
	}
}

void resend_packets(rel_t *rel) {
	send_buffer_metadata md = rel->send_buffer_metadata;
	int to_send = md.last_byte_sent - md.last_byte_acked;
	if (to_send < 0) {
		fprintf(stderr, "last byte acked is greater than last byte sent\n");
	}
	if (to_send <= 0) {
		return;
	}
	packet_list* packets = buffer_to_packets(md.last_byte_acked, to_send);
	packet_list* packets_iter = packets;
	while (packets_iter) {
		conn_sendpkt(rel->c, packets_iter->packet, packets_iter->packet->len);
		packets_iter = packets_iter->next;
	}
	while (packets) {
		free(packets->packet);
		packets = packets->next;
	}
}

