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

#undef DEBUG

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
	 * The next sequence number to send with a packet
	 */
	unsigned int next_seqno_to_send;
	unsigned int final_seqno;

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
	size_t receive_buffer_data_offset;

	/**
	 * The configuration parameters passed from the user
	 */
	const struct config_common *config;

	//flags for calling rel_destroy; check if all equal 1, and if so, call rel_destroy
	uint8_t eof_other_side;
	uint8_t eof_conn_input;
	uint8_t eof_all_acked;
	uint8_t eof_conn_output;
};
rel_t *rel_list;

void print_rel_state(rel_t* rel, int indent_level) {
	char indents[indent_level + 1];
	if (indent_level > 0) {
		int i;
		for (i = 0; i < indent_level; i++) {
			indents[i] = '\t';
		}
		indents[indent_level] = 0;
	}
	else {
		indents[0] = 0;
	}
	fprintf(stderr, "%sNext seqno to send: %d\n", indents, rel->next_seqno_to_send);
	fprintf(stderr, "%sFinal seqno: %d\n", indents, rel->final_seqno);
	fprintf(stderr, "%sSend buffer:\n", indents);
	print_packet_list(rel->send_buffer, 2);
	fprintf(stderr, "%sNext seqno expected: %d\n", indents, rel->next_seqno_expected);
	fprintf(stderr, "%sReceive buffer:\n", indents);
	print_packet_list(rel->receive_buffer, 2);
	fprintf(stderr, "%sEOF flags: %d, %d, %d, %d\n", indents,
			rel->eof_other_side,
			rel->eof_conn_input,
			rel->eof_all_acked,
			rel->eof_conn_output
			);
}

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
	// wtf / s > 1
	/*
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list)
		rel_list->prev = &r->next;
	rel_list = r;
	*/
	// advance the list pointer forward
	r->next = NULL;
	if (rel_list) {
		r->prev = &rel_list;
		rel_list->next = r;
	}
	else {
		r->prev = NULL;
	}
	rel_list = r;
	/* Do any other initialization you need here */
	r->send_buffer = NULL;
	r->next_seqno_to_send = 1;
	r->final_seqno = -1;
	r->receive_buffer = NULL;
	r->next_seqno_expected = 1;
	r->receive_buffer_data_offset = 0;
	r->config = cc;
	r->eof_other_side = 0;
	r->eof_conn_input = 0;
	r->eof_all_acked = 0;
	r->eof_conn_output = 0;
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
		remove_head_packet(&(r->send_buffer));
	}
	while (r->receive_buffer) {
		remove_head_packet(&(r->receive_buffer));
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

void enforce_destroy(rel_t* rel) {
	if (rel->eof_other_side && rel->eof_conn_input &&
		rel->eof_all_acked && rel->eof_conn_output) {
        
		fprintf(stderr, "DESTROYING\n");

		rel_destroy(rel);
	}
}

int handle_ack(rel_t* rel, struct ack_packet* ack_packet) {
	if (!rel) {
		return -1;
	}
#ifdef DEBUG
	fprintf(stderr, "RECEIVE ACK %d\n", ntohl(ack_packet->ackno));
#endif
	if (ntohl(ack_packet->ackno) > rel->final_seqno) {
		rel->eof_all_acked = 1;
#ifdef DEBUG
	fprintf(stderr, "All sent are acked\n");
#endif
	}
	while (rel->send_buffer
			&& ntohl(rel->send_buffer->packet->seqno) < ntohl(ack_packet->ackno)) {
		remove_head_packet(&rel->send_buffer);
	}
	//assert(ntohl(rel->send_buffer->packet->seqno) >= ntohl(ack_packet->ackno));
	return 0;
}

void send_ack(rel_t *r, uint32_t ackno) {
#ifdef DEBUG
	fprintf(stderr, "SEND ACK %d\n", ackno);
#endif
	size_t ack_packet_size = sizeof(struct ack_packet);
	struct ack_packet* ack = (struct ack_packet*) malloc(ack_packet_size);
	memset(ack, 0, ack_packet_size);
	ack->len = htons(ack_packet_size);
	ack->ackno = htonl(ackno);
	ack->cksum = cksum((void *)ack, ack_packet_size);
	conn_sendpkt(r->c, (packet_t *)ack, ack_packet_size);
	//assert(sent == 8);
	//assert(ntohs(ack->len) == 8);
	//assert(ntohl(ack->ackno) == ackno);
	free(ack);
	return;
}

uint16_t check_pkt_data_len(char * data){
	data+=MAX_PACKET_DATA_SIZE;
	uint16_t i;
	for(i = MAX_PACKET_DATA_SIZE-1; i>=0;i--){
		if(data[i]!=' '){
			break;
		}
		printf("%c ",data[i]);
	}
	return i;
}
// For receiving packets; these are either ACKs (for sending) or data packets (for receiving)
// For receiving: read in and buffer the packets so they can be consumed by by rel_output;
// send ACKs for the packets buffered
// For sending: update how many unACKed packets there are
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
#ifdef DEBUG
	fprintf(stderr, "\n");
	fprintf(stderr, "--- Start recvpkt -----------------------------\n");
	print_rel_state(r, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
	uint16_t packet_length = ntohs(pkt->len);
	uint16_t stored_checksum = pkt->cksum;
	pkt->cksum = 0;
	uint16_t computed_checksum = cksum(pkt, packet_length);
	if (computed_checksum != stored_checksum) {
		fprintf(stderr, "Checksum failed for packet of length %d\n", packet_length);
		return;
	}
	//if(ntohs(pkt->len) != (uint16_t) n)	return;
	//printf("recv len: %u calc len:%u n:%u\n", ntohs(pkt->len), (uint16_t)check_pkt_data_len(pkt->data), (uint16_t)n);
	
	// Ack packet
	if(packet_length == 8){
		handle_ack(r, (struct ack_packet*) pkt);
		rel_read(r);
	}
	// Data packet
	else if (packet_length >= 12
			&& packet_length <= MAX_PACKET_SIZE
			&& ntohl(pkt->seqno) >= r->next_seqno_expected){
		//if (ntohs(pkt->len)-12 != check_pkt_data_len(pkt->data))	return;
#ifdef DEBUG
		fprintf(stderr, "INSERTING %d\n", ntohl(pkt->seqno));
#endif
		packet_list* to_insert = new_packet();
		memcpy(to_insert->packet, pkt, packet_length);

		insert_packet_in_order(&(r->receive_buffer), to_insert);

		int next_seqno_candidate = last_consecutive_sequence_number(r->receive_buffer) + 1;
		if (next_seqno_candidate > r->next_seqno_expected) {
			r->next_seqno_expected = next_seqno_candidate;
		}

		send_ack(r, r->next_seqno_expected);
		handle_ack(r, (struct ack_packet*) pkt);

		if (packet_length == 12) {
//#ifdef DEBUG
			fprintf(stderr, "RECEIVE EOF PACKET\n");
//#endif
			r->eof_other_side = 1;
		}
		rel_output(r);
	}
	//enforce_destroy(r);
#ifdef DEBUG
	fprintf(stderr, "--- End recvpkt -------------------------------\n");
	print_rel_state(r, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
	return;
}

void
rel_read (rel_t *s)
{
#ifdef DEBUG
	fprintf(stderr, "\n");
	fprintf(stderr, "--- Start read --------------------------------\n");
	print_rel_state(s, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
	if (!s) {
		return;
	}
	int window_size = s->config->window;
	while (packet_list_size(s->send_buffer) < window_size) {
		int should_break = 0;
		packet_list* packet_node = new_packet();
		int bytes_read = conn_input(s->c, packet_node->packet->data, MAX_PACKET_DATA_SIZE);
		if (bytes_read == 0) {
			break;
		}
		if(bytes_read < 0){
			should_break = 1; //need to send eof
			s->eof_conn_input = 1;
			s->final_seqno = s->next_seqno_to_send;
			bytes_read = 0;
		}
		packet_node->packet->cksum = 0;
		int packet_length = DATA_PACKET_METADATA_LENGTH + bytes_read;
		packet_node->packet->len = htons(packet_length);
		packet_node->packet->ackno = htonl(s->next_seqno_expected);
		packet_node->packet->seqno = htonl(s->next_seqno_to_send);
		uint16_t checksum = cksum(packet_node->packet, packet_length);
		packet_node->packet->cksum = checksum;
		s->next_seqno_to_send++;

		conn_sendpkt(s->c, packet_node->packet, packet_length);
		append_packet(&(s->send_buffer), packet_node);

		if (should_break) {
			break;
		}
	}
	//enforce_destroy(s);
#ifdef DEBUG
	fprintf(stderr, "--- End read ----------------------------------\n");
	print_rel_state(s, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
}

bool is_eof_packet(packet_t* packet) {
	if (!packet) {
		return false;
	}
	if (ntohs(packet->len) == 12) {
		return true;
	}
	return false;
}

bool handle_eof_packet(rel_t* rel) {
	if (!rel
			|| !(rel->receive_buffer)
			|| !(rel->receive_buffer->packet)) {
		return false;
	}
	if (is_eof_packet(rel->receive_buffer->packet)) {
		conn_output(rel->c, NULL, 0);
		rel->eof_conn_output = 1;
		enforce_destroy(rel);
		return true;
	}
	return false;
}

// Consume the packets buffered by rel_recvpkt; call conn_bufspace to see how much data
// you can flush, and flush using conn_output
void rel_output (rel_t *r) {
#ifdef DEBUG
	fprintf(stderr, "\n");
	fprintf(stderr, "--- Start output ------------------------------\n");
	print_rel_state(r, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
	if (r->eof_conn_output) {
		return;
	}
	int bufspace = conn_bufspace(r->c);
	int data_written = 0;
	while (data_written < bufspace
			&& r->receive_buffer
			&& r->receive_buffer->packet
			&& !(handle_eof_packet(r))
			&& r->receive_buffer->packet->data) {
		int to_write = ntohs(r->receive_buffer->packet->len)
				- DATA_PACKET_METADATA_LENGTH
				- r->receive_buffer_data_offset;
		if (to_write <= 0) {
			break;
		}
		bool truncated = false;
		if (data_written + to_write > bufspace) {
			to_write = bufspace - data_written;
			truncated = true;
		}
		char* start_of_data = r->receive_buffer->packet->data + r->receive_buffer_data_offset;
		conn_output(r->c, start_of_data, to_write);
		if (truncated) {
			r->receive_buffer_data_offset += to_write;
		}
		else {
			remove_head_packet(&r->receive_buffer);
		}
		data_written += to_write;
	}
#ifdef DEBUG
	fprintf(stderr, "--- End output --------------------------------\n");
	print_rel_state(r, 1);
	fprintf(stderr, "-----------------------------------------------\n");
	fprintf(stderr, "\n");
#endif
}

void resend_packets(rel_t *rel) {
/*
#ifdef DEBUG
	fprintf(stderr, "--- Resending packets -------------------------\n");
	print_rel_state(rel);
	fprintf(stderr, "-----------------------------------------------\n");
#endif
*/
	packet_list* packets_iter = rel->send_buffer;
	while (packets_iter && packets_iter->packet) {
/*
#ifdef DEBUG
		fprintf(stderr, "\n--- Resending packet %d ---\n", ntohl(packets_iter->packet->seqno));
#endif
*/
		conn_sendpkt(rel->c, packets_iter->packet, ntohs(packets_iter->packet->len));
		packets_iter = packets_iter->next;
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
		rel_list_fwd = rel_list_fwd->next;
		resend_packets(rel_list_fwd);
	}
	rel_t *rel_list_bwd = rel_list;
	while (rel_list_bwd->prev && *(rel_list_bwd->prev)) {
		rel_list_bwd = *(rel_list_bwd->prev);
		resend_packets(rel_list_bwd);
	}
}
