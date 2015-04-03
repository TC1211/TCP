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
#include "constants.h"

// TODO:
// - multiple connections


// The mapping from rel_t to conn_t is one-to-one; for every connection, there is one
// rel_t and one conn_t instance.
// rel_t also contains a linked list for traversing all connections
struct reliable_state {
	rel_t *next; /* Linked list for traversing all connections */
	rel_t **prev;
	conn_t *c; /* This is the connection object */
	/* Add your own data fields below this */
	/**
	 * This consists of the data that has not been acknowledged yet.
	 * Data that has been acknowledged is not included in the send buffer.
	 *
	 * The send buffer stores data in packets ordered ascending by their
	 * sequence number.
	 *
	 * The first half consists of the data that has been sent but not
	 * acked, and the second half consists of the data that has not
	 * been sent yet (and therefore not acknowledged)
	 */
	packet_list* send_buffer;
	/**
	 * The sequence number of the next packet to send in the send buffer
	 */
	unsigned int next_seqno_to_send;

	/**
	 * This consists of the data that has not been read by the application yet.
	 * Data that has been read by the application is not included in the receive buffer.
	 *
	 * The receive buffer stores data in packets ordered ascending by their
	 * sequence number.
	 *
	 * The first half consists of the contiguous data that has been received,
	 * and the second half consists of data that is not yet contiguous
	 */
	packet_list* receive_buffer;
	/**
	 * The sequence number of the lowest packet that could be received next in
	 * the receive buffer
	 */
	unsigned int next_seqno_expected;

	/**
	 * The configuration parameters passed from the user
	 */
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
// For receiving: read in and buffer the packets so they can be consumed by by rel_output;
// send ACKs for the packets buffered
// For sending: update how many unACKed packets there are
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
	// first check validity
//	if (!cksum(pkt, pkt->cksum))	return;
//	
//	//seq_obj *head = r->head;
//	if(n == 8){ // Acks
//		if (r->send_buffer_metadata.last_ack+1 == pkt->ackno)	r->send_buffer_metadata.last_ack++;
//		return;
//	} 
//	else if (n > 12 && pkt->seqno > 0){ //receiver
//		uint32_t next_byte = (int *) *(r->receive_buffer_metadata.next_byte_expected);
//		char *copy = r->receive_buffer;
//
//		if(r->receive_buffer_metadata.next_seqno_expected == pkt->seqno){ // correct order
//			int b;
//			int d = 0;
//			for (b = next_byte; b < next_byte + n-12; b++){
//				*(copy+b) = pkt->data[d];
//				d++;
//			}
//			(uint32_t *) *(r->receive_buffer_metadata.next_byte_expected) += n - 12;
//			(uint32_t *) *(r->receive_buffer_metadata.last_byte_received) += n - 12;
//		
//		} else { // not in order
//			receiveBuf_t *receive_head = r->receive_ordering_buf;
//			bool existing_chunk = false;
//			while(receive_head != NULL){
//				if(receive_head->tail == pkt->seqno){
//					receive_head->tail++;
//					receive_head->len = n - 12;
//					strncat(receive_head->data, pkt->data, n - 12);
//					existing_chunk = true;
//				}
//				receive_head = receive_head->next;
//			}
//			receive_head = r->receive_ordering_buf;
//			if(!existing_chunk){
//				while(receive_head->next!=NULL)		receive_head = receive_head->next;
//				receive_head->next = malloc(sizeof(receiveBuf_t));
//				receive_head->next->head = pkt->seqno;
//				receive_head->next->tail = pkt->seqno + 1;
//				receive_head->next->len = n - 12;
//				strncat(receive_head->next->data, pkt->data, n-12);
//			}
//		}
//		// now fill in the spaces
//		bool gap_to_fill = true;
//		uint32_t byte_to_fill;
//
//		int o, tail_seqno;
//		while(gap_to_fill){
//			gap_to_fill = false;
//			receiveBuf_t* receive_head = r->receive_ordering_buf;
//			byte_to_fill = r->receive_buffer_metadata.next_byte_expected;
//			while(receive_head != NULL){
//				if(receive_head->head == byte_to_fill){
//					copy = r->receive_buffer;
//					strncpy(copy+byte_to_fill, receive_head->data, receive_head->len);
//					(uint32_t*) *(r->receive_buffer_metadata.next_byte_expected) += receive_head->len;
//					gap_to_fill = true;
//					tail_seqno = receive_head->tail - 1;
//				}
//				receive_head = receive_head->next;
//			}
//		if(pkt->seqno > tail_seqno)
//			(uint32_t*) *(r->receive_buffer_metadata.last_byte_received) += (pkt->seqno - tail_seqno)*500;
//		else
//			(uint32_t*) *(r->receive_buffer_metadata.last_byte_received) = 	(uint32_t*) *(r->receive_buffer_metadata.next_byte_expected);
//				
//		return;
//	}
//	return;
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

    while (1) {
        packet_list* packet_node = new_packet();
        packet_t* packet = packet_node->packet;
        uint16_t bytes_recv = conn_input(s->c, packet->data, MAX_PACKET_DATA_SIZE);
        
        if (bytes_recv <= 0) { //special behavior required for EOF (-1)?
            break;
        }
        
        uint16_t len = bytes_recv + PACKET_METADATA_LENGTH;
        packet->cksum = 0;
        packet->len = htons(len);
        packet->ackno = 0; // this value is undefined for sending (for data packets).
        packet->seqno = htonl(s->next_seqno_to_send);
        packet->cksum = cksum(&packet, len);
        
        
        //if our buffer isn't full DO
        append_packet(&s->send_buffer, packet_node);
        //else break
        
        if (s->config->window >= s->next_seqno_to_send - s->next_seqno_expected) { //if we don't overflow the window
            conn_sendpkt(s->c, packet, sizeof(packet));
            s->next_seqno_to_send++;
        }
    }

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
void rel_output (rel_t *r) {
	int check = conn_bufspace(r->c);
	int total = packet_data_size(r->receive_buffer);
	if (check == 0) {
		printf("Not enough space in output\n");
		return;
	}
	size_t size = (size_t) (check < total) ? check : total;
	char *buf = malloc(size);
	packet_list *list = r->receive_buffer;
	int packets_written;
	int last_packet_offset;
	serialize_packet_data(buf, size, list, &packets_written, &last_packet_offset);
	
	conn_output(r->c, buf, (int) size);
	if (last_packet_offset != 0) {
		packets_written--;
	}
	int i = 0;
	for (i = 0; i < packets_written; i++) {
		remove_head_packet(&r->receive_buffer);
	}
	return;
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
	packet_list* packets_iter = rel->send_buffer;
	while (packets_iter && packets_iter->packet
			&& packets_iter->packet->seqno < rel->next_seqno_to_send) {
		conn_sendpkt(rel->c, packets_iter->packet, packets_iter->packet->len);
		packets_iter = packets_iter->next;
	}
}
