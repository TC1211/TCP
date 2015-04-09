
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
#include <stdbool.h>
#include <limits.h>

#include "rlib.h"
#include "packet_list.c"
#include "constants.h"

#undef DEBUG

struct reliable_state {

	conn_t *c;			/* This is the connection object */

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

	unsigned int ssthresh;
	unsigned int congestion_window;
	unsigned int receive_window;

	unsigned int window_timer;
	unsigned int num_packets_sent;
	unsigned int num_packets_recvd;

	unsigned int consec_acks;
	unsigned int last_ack_recvd;

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
	fprintf(stderr, "%sPID: %d\n", indents, getpid());
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
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
		const struct config_common *cc)
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

	r->ssthresh = INT_MAX;
	r->congestion_window = INITIAL_SEND_WINDOW;
	r->receive_window = r->config->window;
	r->window_timer = 0;
	r->num_packets_sent = 0;
	r->num_packets_recvd = 0;

	r->consec_acks = 0;
	r->last_ack_recvd = 0;

	return r;
}

void
rel_destroy (rel_t *r)
{
	conn_destroy (r->c);

	/* Free any other allocated memory here */
	while (r->send_buffer) {
		remove_head_packet(&(r->send_buffer));
	}
	while (r->receive_buffer) {
		remove_head_packet(&(r->receive_buffer));
	}
}


void
rel_demux (const struct config_common *cc,
		const struct sockaddr_storage *ss,
		packet_t *pkt, size_t len)
{
	//leave it blank here!!!
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
	int ackno = (int) ntohl(ack_packet->ackno);
	if (ackno == rel->last_ack_recvd) {
		rel->consec_acks++;
		if (rel->consec_acks >= 4) {
			rel->ssthresh = rel->congestion_window / 2;
			rel->congestion_window = rel->ssthresh;
		}
	} else {
		rel->last_ack_recvd = ackno;
		rel->consec_acks = 0;
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
	ack->rwnd = htonl(r->receive_window - packet_list_size(r->receive_buffer));
	ack->cksum = cksum((void *)ack, ack_packet_size);
	conn_sendpkt(r->c, (packet_t *)ack, ack_packet_size);
	free(ack);
	return;
}
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
	if (((int) n) != ntohs(pkt->len)) {
		fprintf(stderr, "%d: Packet advertised size is not equal to real size\n", getpid());
		return;
	}
	if ((int) n < ACK_PACKET_LENGTH
			|| (int) n > MAX_PACKET_SIZE) {
		fprintf(stderr, "%d: Real length is bad\n", getpid());
		return;
	}
	if (ntohl(pkt->ackno) < 1
			|| ntohl(pkt->ackno) > r->next_seqno_to_send) {
		fprintf(stderr, "%d: Ackno %d doesn't make sense\n", getpid(), ntohl(pkt->ackno));
		return;
	}
	uint16_t packet_length = ntohs(pkt->len);
	if (packet_length < ACK_PACKET_LENGTH
			|| packet_length > MAX_PACKET_SIZE) {
		fprintf(stderr, "%d: Bad advertised packet length\n", getpid());
		return;
	}
	uint16_t stored_checksum = pkt->cksum;
	pkt->cksum = 0;
	uint16_t computed_checksum = cksum(pkt, packet_length);
	if (computed_checksum != stored_checksum) {
		fprintf(stderr, "%d: Checksum failed for packet of length %d, ackno %d, seqno %d\n",
				getpid(), packet_length, ntohl(pkt->ackno), ntohl(pkt->seqno));
		return;
	}
	//if(ntohs(pkt->len) != (uint16_t) n)	return;
	//printf("recv len: %u calc len:%u n:%u\n", ntohs(pkt->len), (uint16_t)check_pkt_data_len(pkt->data), (uint16_t)n);



	// Ack packet
	if(packet_length == ACK_PACKET_LENGTH){
		handle_ack(r, (struct ack_packet*) pkt);
		rel_read(r);
	}
	// Data packet
	else if (packet_length >= DATA_PACKET_METADATA_LENGTH
			&& packet_length <= MAX_PACKET_SIZE
			&& ntohl(pkt->seqno) >= r->next_seqno_expected){
		//if (ntohs(pkt->len)-12 != check_pkt_data_len(pkt->data))	return;
#ifdef DEBUG
		fprintf(stderr, "INSERTING %d\n", ntohl(pkt->seqno));
#endif
		if (ntohl(pkt->seqno) < 1) {
			fprintf(stderr, "%d: Seqno %d doesn't make sense\n", getpid(), ntohl(pkt->seqno));
			return;
		}
		packet_list* to_insert = new_packet();
		memcpy(to_insert->packet, pkt, packet_length);

		insert_packet_in_order(&(r->receive_buffer), to_insert);

		if (ntohl(pkt->seqno) == r->next_seqno_expected) {
			r->next_seqno_expected++;
		}

		send_ack(r, r->next_seqno_expected);
		handle_ack(r, (struct ack_packet*) pkt);

		if (packet_length == DATA_PACKET_METADATA_LENGTH) {
//#ifdef DEBUG
			fprintf(stderr, "RECEIVE EOF PACKET\n");
//#endif
			r->eof_other_side = 1;
		}
		r->num_packets_recvd++;
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

void aimd(rel_t *r) {
	r->congestion_window++;
}

void slow_start_check(rel_t *r) {
	if ((2 * r->congestion_window) < r->ssthresh) {
		r->congestion_window = 2 * (r->congestion_window) * (r->num_packets_recvd) / (r->num_packets_sent);
	} else {
		aimd(r);
	}
}

void
rel_read (rel_t *s)
{
	if(s->c->sender_receiver == RECEIVER)
	{
		if (s->eof_conn_input) {
			return;
		} else {
			s->eof_conn_input = 1;
			s->final_seqno = s->next_seqno_to_send;
			packet_list *eof = new_packet();
			memset(eof, 0, sizeof(packet_list));
			
			int packet_length = DATA_PACKET_METADATA_LENGTH;
			eof->packet->len = htons(packet_length);
			eof->packet->ackno = htonl(s->next_seqno_expected);
			eof->packet->seqno = htonl(s->next_seqno_to_send);
			eof->packet->rwnd = htonl(s->receive_window - packet_list_size(s->receive_buffer));
			uint16_t checksum = cksum(eof->packet, packet_length);
			eof->packet->cksum = checksum;
			s->next_seqno_to_send++;

			conn_sendpkt(s->c, eof->packet, packet_length);
			append_packet(&(s->send_buffer), eof);
			s->num_packets_sent++;
			return;
		}
	}
	else //run in the sender mode
	{
#ifdef DEBUG
		fprintf(stderr, "\n");
		fprintf(stderr, "--- Start read --------------------------------\n");
		print_rel_state(s, 1);
		fprintf(stderr, "-----------------------------------------------\n");
		fprintf(stderr, "\n");
#endif
		if (!s
				|| s->eof_conn_input) {
			return;
		}
//		int window_size = s->config->window;
		int compare = s->receive_window - packet_list_size(s->receive_buffer);
		int min = s->congestion_window < compare ? s->congestion_window : compare;
		while (packet_list_size(s->send_buffer) < min) {
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
			packet_node->packet->rwnd = htonl(s->receive_window - packet_list_size(s->receive_buffer));
			uint16_t checksum = cksum(packet_node->packet, packet_length);
			packet_node->packet->cksum = checksum;
			s->next_seqno_to_send++;

			conn_sendpkt(s->c, packet_node->packet, packet_length);
			append_packet(&(s->send_buffer), packet_node);
			s->num_packets_sent++;
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
		//same logic as lab 1
	}
}

bool is_eof_packet(packet_t* packet) {
	if (!packet) {
		return false;
	}
	if (ntohs(packet->len) == DATA_PACKET_METADATA_LENGTH) {
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

void
rel_output (rel_t *r)
{
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
			&& r->receive_buffer->packet->data
			&& ntohl(r->receive_buffer->packet->seqno) < r->next_seqno_expected) {
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
	if (packets_iter && packets_iter->packet) {
		//timeout
		rel->ssthresh = 0.5 * rel->ssthresh;
		rel->congestion_window = INITIAL_SEND_WINDOW;
		
	}
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

void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */
	/* Retransmit any packets that need to be retransmitted */
	if (rel_list) {
		resend_packets(rel_list);
		rel_list->window_timer += rel_list->config->timer;
		if (rel_list->window_timer >= rel_list->config->timeout) {
			slow_start_check(rel_list);
			rel_list->window_timer = 0;
			rel_list->num_packets_sent = 0;
			rel_list->num_packets_recvd = 0;
		}
	}
	/*
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
	*/
}
