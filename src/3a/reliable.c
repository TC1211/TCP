
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

// TODO:
//    - arbitrarily long receive / send buffer
//    - multiple connections

// Buffers are fixed at 10 megabytes for now
#define DEFAULT_BUFFER_SIZE (10 * 1024 *1024)

typedef struct {
	void *last_byte_acked;
	void *last_byte_sent;
	void *last_byte_written;
} send_buffer_metadata;

typedef struct {
	void *last_byte_read;
	void *next_byte_expected;
	void *last_byte_received;
} receive_buffer_metadata;

// The mapping from rel_t to conn_t is one-to-one; for every connection, there is one
// rel_t and one conn_t instance.
// rel_t also contains a linked list for traversing all connections
struct reliable_state {
	rel_t *next;			/* Linked list for traversing all connections */
	rel_t **prev;

	conn_t *c;			/* This is the connection object */

	/* Add your own data fields below this */

	void *send_buffer;
	size_t send_buffer_size;
	send_buffer_metadata send_buffer_metadata;

	void *receive_buffer;
	size_t receive_buffer_size;
	receive_buffer_metadata receive_buffer_metadata;

	// config
	//   - window size
	//   - timer interval
	//   - timeout interval
	//   - whether single connection or not
	//
	const struct config_common *config;
};
rel_t *rel_list;





/* Creates a new reliable protocol session, returns NULL on failure.
 * Exactly one of c and ss should be NULL.  (ss is NULL when called
 * from rlib.c, while c is NULL when this function is called from
 * rel_demux.) */
// 1) if this is a new connection the conn_t is NULL
// 2) if the connection has already been created,
// then there is no need for the sockaddr_storage, so it will be NULL
// During startup, an initial connection is created for you, leading to 2)
// During runtime, if a new connection is created, then you have to deal with 1)
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
	r->next = rel_list;
	r->prev = &rel_list;
	if (rel_list)
		rel_list->prev = &r->next;
	rel_list = r;

	/* Do any other initialization you need here */

	r->send_buffer = malloc(DEFAULT_BUFFER_SIZE);
	r->send_buffer_size = DEFAULT_BUFFER_SIZE;
	r->send_buffer_metadata.last_byte_acked = r->send_buffer;
	r->send_buffer_metadata.last_byte_sent = r->send_buffer;
	r->send_buffer_metadata.last_byte_written = r->send_buffer;

	r->receive_buffer = malloc(DEFAULT_BUFFER_SIZE);
	r->receive_buffer_size = DEFAULT_BUFFER_SIZE;
	r->receive_buffer_metadata.last_byte_read = r->receive_buffer;
	r->receive_buffer_metadata.next_byte_expected = r->receive_buffer;
	r->receive_buffer_metadata.last_byte_received = r->receive_buffer;

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

	free(r->send_buffer);
	free(r->receive_buffer);
}


/* This function only gets called when the process is running as a
 * server and must handle connections from multiple clients.  You have
 * to look up the rel_t structure based on the address in the
 * sockaddr_storage passed in.  If this is a new connection (sequence
 * number 1), you will need to allocate a new conn_t using rel_create
 * ().  (Pass rel_create NULL for the conn_t, so it will know to
 * allocate a new connection.)
 */
// Note: This is only called in server mode, i.e. when you supply the -s option when running
// This will add a new node to the linked list if this is a new connection
// if not, ???
void
rel_demux (const struct config_common *cc,
		const struct sockaddr_storage *ss,
		packet_t *pkt, size_t len)
{
}

// For receiving packets; these are either ACKs (for sending) or data packets (for receiving)
// For receiving: read in and buffer the packets so they can be consumed by by rel_output
// For sending: update how many unACKed packets there are
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
}

// read in data using conn_input, break this data into packets, send the packets,
// and update how many unacked packets there are
void
rel_read (rel_t *s)
{
}

// Consume the packets buffered by rel_recvpkt; call conn_bufspace to see how much data
// you can flush, and flush using conn_output
// Once flushed, send ACKs out, since there is now free buffer space
void
rel_output (rel_t *r)
{
}

// Retransmit any unACKed packets after a certain amount of time
void
rel_timer ()
{
	/* Retransmit any packets that need to be retransmitted */

}
