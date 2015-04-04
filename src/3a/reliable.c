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
	 * This consists of the data that has been sent but not acknowledged.
	 * Data that has been acknowledged is not included in the send buffer.
	 *
	 * The send buffer stores data in packets ordered ascending by their
	 * sequence number.
	 */
	packet_list* send_buffer;

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

void send_ack(rel_t *r, uint32_t ackno);
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
	if (!cksum(pkt, pkt->cksum))	return;
	
	if(pkt->len == 8){ // Acks, remove the entry
		packet_list* send_buffer_head = r->send_buffer; // this assume that the pointer directly points to the true head
		while(send_buffer_head->packet->seqno != pkt->seqno && send_buffer_head !=NULL){
			remove_head_packet(&send_buffer_head);
		}
	} 
	else if (pkt->len >= 12 && pkt->seqno > 0){ //receiver
		if(r->config->window > packet_list_size(r->receive_buffer) && pkt->seqno>=r->next_seqno_expected){ //enforce window size and next to receive pointer
			insert_packet_in_order(&(r->receive_buffer), pkt->seqno);
			if(pkt->seqno == r->next_seqno_expected)	r->next_seqno_expected++;
			send_ack(r, r->next_seqno_expected);
		}
	}
	return;
}


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
        	packet->seqno = htonl(s->seqno_track);
        	packet->cksum = cksum(&packet, len);
        
        
        	//if our buffer isn't full DO
        	append_packet(&s->send_buffer, packet_node);
        	//else break

		s->seqno_track++;
	}

	packet_list *start = get_packet_by_seqno(s->send_buffer, s->next_seqno_to_send);
	
/*    	while (1) {
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
        
		if (s->config->window >= s->next_seqno_to_send - s->next_seqno_expected) 			{ //if we don't overflow the window
        		conn_sendpkt(s->c, packet, sizeof(packet));
            		s->next_seqno_to_send++;
        	}
    	}
*/
}

void send_ack(rel_t *r, uint32_t ackno) {
	packet_t *ack = malloc(sizeof(packet_t));
	memset(ack, 0, sizeof(packet_t));
	ack->len = (uint16_t) 8;
	ack->ackno = (uint32_t) ackno;
	ack->cksum = cksum((void *)ack, sizeof(ack));
	conn_sendpkt(r->c, ack, sizeof(ack));
	free(ack);
	return;
}

// Consume the packets buffered by rel_recvpkt; call conn_bufspace to see how much data
// you can flush, and flush using conn_output
void rel_output (rel_t *r) {
	int check = conn_bufspace(r->c);
	int total = packet_data_size(r->receive_buffer, r->next_seqno_expected);
	if (check == 0) {
		printf("Not enough space in output\n");
		return;
	}
	size_t size = (size_t) (check < total) ? check : total;
	char *buf = malloc(size);
	packet_list *list = r->receive_buffer;
	int packets_written;
	int last_packet_offset;
	serialize_packet_data(buf, size, r->next_seqno_expected, list,
			&packets_written, &last_packet_offset);

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
		rel_list_fwd = rel_list_fwd->next;
		resend_packets(rel_list_fwd);
	}
	rel_t *rel_list_bwd = rel_list;
	while (rel_list_bwd->prev && *(rel_list_bwd->prev)) {
		rel_list_bwd = *(rel_list_bwd->prev);
		resend_packets(rel_list_bwd);
	}
}

void resend_packets(rel_t *rel) {
	packet_list* packets_iter = rel->send_buffer;
	while (packets_iter && packets_iter->packet) {
		conn_sendpkt(rel->c, packets_iter->packet, packets_iter->packet->len);
		packets_iter = packets_iter->next;
	}
}
