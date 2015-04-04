#include <stdio.h>
#include <assert.h>

#include "rlib.h"
#include "packet_list.c"

void test_serialize() {
	packet_list* packet_a = new_packet();
	strncpy(packet_a->packet->data, "first", 5);
	packet_a->packet->len = DATA_PACKET_METADATA_LENGTH + 5;
	packet_list* packet_b = new_packet();
	strncpy(packet_b->packet->data, "second", 6);
	packet_b->packet->len = DATA_PACKET_METADATA_LENGTH + 6;
	append_packet(&packet_a, packet_b);
	// 11 total bytes of data
	assert(packet_data_size(packet_a, -1) == 11);

	char buffer[8];
	int packets_written;
	int offset;
	serialize_packet_data(buffer, 8, -1, packet_a, &packets_written, &offset);

	assert(strncmp("firstsec", buffer, 8) == 0);
	//printf("%.8s", buffer);
	assert(packets_written == 2);
	assert(offset == 3);

	serialize_packet_data(buffer, 5, -1, packet_a, &packets_written, &offset);
	assert(packets_written == 1);
	assert(offset == 0);

	serialize_packet_data(buffer, 2, -1, packet_a, &packets_written, &offset);
	assert(packets_written == 1);
	assert(offset == 2);
}

void test_get_by_seqno() {
	packet_list* packet_a = new_packet();
	packet_a->packet->seqno = 1;
	packet_list* packet_b = new_packet();
	packet_b->packet->seqno = 2;
	packet_list* packet_c = new_packet();
	packet_c->packet->seqno = 3;
	packet_c->packet->ackno = 99;

	append_packet(&packet_a, packet_b);
	append_packet(&packet_a, packet_c);

	packet_list* search = get_packet_by_seqno(packet_a, 3);
	assert(search->packet->ackno == 99);
}

int main() {
	packet_list* packet_a = new_packet();
	packet_a->packet->seqno = 1;
	packet_list* packet_b = new_packet();
	packet_b->packet->seqno = 2;
	packet_list* packet_c = new_packet();
	packet_c->packet->seqno = 3;

	packet_list* packet_d = new_packet();
	// you can delete a packet using remove_head_packet
	remove_head_packet(&packet_d);
	assert(packet_d == NULL);

	// you can initialize a reference to a packet using insert_packet_after and append_packet
	packet_list* new_reference = NULL;
	insert_packet_after(&new_reference, packet_a);
	assert(new_reference->packet->seqno == 1);
	new_reference = NULL;
	append_packet(&new_reference, packet_a);
	assert(new_reference->packet->seqno == 1);
	// confirm that this creates a single node (a list of size 1)
	assert(packet_list_size(new_reference) == 1);

	// insert packet b directly after packet a
	insert_packet_after(&packet_a, packet_b);
	// packet a should be the head of the list
	assert(packet_a->prev == NULL);
	// the next packet should be b, which has a sequence number of 2
	assert(packet_a->next->packet->seqno == 2);
	// the previous packet from b should be a, which has a sequence number of 1
	assert(packet_b->prev->packet->seqno == 1);
	// packet b should be the end of the list
	assert(packet_b->next == NULL);
	// the size is now 2
	assert(packet_list_size(packet_a) == 2);

	// append packet c to the end of the list
	append_packet(&packet_a, packet_c);
	// packet b should now point to packet c
	assert(packet_b->next->packet->seqno == 3);
	// packet c should now be at the end
	assert(packet_c->next == NULL);
	// the size is now 3
	assert(packet_list_size(packet_a) == 3);

	packet_list* packet_z = new_packet();
	packet_z->packet->seqno = 99;
	// insert packet 99 after packet 2
	insert_packet_after_seqno(&packet_a, packet_z, 2);
	// The order by sequence number is now:
	// 1 -> 2 -> 99 -> 3
	assert(packet_b->next->packet->seqno == 99);
	// the size is now 4
	assert(packet_list_size(packet_a) == 4);

	packet_list* packet_y = new_packet();
	packet_y->packet->seqno = 999;
	// insert packet 999 at the end, after 3
	insert_packet_after_seqno(&packet_a, packet_y, 3);
	// The order by sequence number is now:
	// 1 -> 2 -> 99 -> 3 -> 999
	assert(packet_c->next->packet->seqno == 999);
	// the size is now 5
	assert(packet_list_size(packet_a) == 5);

	test_serialize();
	test_get_by_seqno();
}
