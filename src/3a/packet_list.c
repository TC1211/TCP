#include <stdlib.h>
#include <string.h>

#include "constants.h"

/**
 * A doubly-linked list with a malloc'd packet as the payload
 */
typedef struct packet_list {
	/**
	 * The next element in the list. NULL if the last element.
	 */
	struct packet_list *next;
	/**
	 * The previous element in the list. NULL if the first element.
	 */
	struct packet_list *prev;
	/**
	 * The malloc'd location of the packet
	 */
	packet_t *packet;
} packet_list;

/**
 * Create a new, unlinked packet node
 */
packet_list* new_packet() {
	packet_list *new = (packet_list*) malloc(sizeof(packet_list));
	new->next = NULL;
	new->prev = NULL;
	new->packet = (packet_t*) malloc(MAX_PACKET_SIZE);
	return new;
}

/**
 * Delete the node at this location in the list and modify the list to point
 * to the next entry, which may be NULL
 */
int remove_head_packet(packet_list** list) {
	if (!list) {
		return -1;
	}
	if (!(*list)) {
		return 0;
	}
	if ((*list)->packet) {
		free((*list)->packet);
	}
	packet_list* new_head = (*list)->next;
	if (new_head) {
		new_head->prev = NULL;
	}
	free(*list);
	*list = new_head;
	return 0;
}

/**
 * Insert a packet after the node pointed to in the list; if the node
 * pointed to is NULL, then the list will be initialized with that packet
 */
int insert_packet_after(packet_list** list, packet_list* packet) {
	if (!packet || !list) {
		return -1;
	}
	if (!(*list)) {
		*list = packet;
		return 0;
	}
	packet_list* old_next = (*list)->next;
	(*list)->next = packet;
	packet->prev = *list;
	packet->next = old_next;
	if (old_next) {
		old_next->prev = packet;
	}
	return 0;
}

/**
 * Return the first packet with a certain sequence number in the list, or
 * else NULL
 */
packet_list* get_packet_by_seqno(packet_list* list, unsigned int seqno) {
	while (list) {
		if (list->packet && list->packet->seqno == seqno) {
			return list;
		}
		list = list->next;
	}
	return NULL;
}

/**
 * Insert a packet after the first node with a certain sequence number;
 * do nothing if the sequence number is not found, or if the node pointed
 * to in the list is NULL
 */
int insert_packet_after_seqno(
		packet_list** list, packet_list* packet, unsigned int seqno) {
	if (!packet || !list || !(*list)) {
		return -1;
	}
	packet_list* search = get_packet_by_seqno(*list, seqno);
	if (search) {
		return insert_packet_after(&search, packet);
	}
	return -1;
}

/**
 * Insert a packet at the end of the list; if the node pointed to in the list
 * is NULL, then initialize the list with that packet
 */
int append_packet(packet_list** list, packet_list* packet) {
	if (!packet || !list) {
		return -1;
	}
	if (!(*list)) {
		*list = packet;
		return 0;
	}
	packet_list* list_iter = *list;
	while (list_iter->next) {
		list_iter = list_iter->next;
	}
	return insert_packet_after(&list_iter, packet);
}

// tail recursion for the lols
int packet_list_size_acc(packet_list* list, int current_size) {
	if (!list) {
		return current_size;
	}
	return packet_list_size_acc(list->next, current_size + 1);
}

/**
 * Return the size of the list, starting from this point onwards
 */
int packet_list_size(packet_list* list) {
	return packet_list_size_acc(list, 0);
}

/**
 * Get the size of the data in the packets
 */
int packet_data_size(packet_list* list) {
	int data_size = 0;
	while (list) {
		if (list->packet) {
			data_size += list->packet->len - PACKET_METADATA_LENGTH;
		}
		list = list->next;
	}
	return data_size;
}

/**
 * Write out size bytes of data to the buffer from the packet list
 * and return the number of packets written and the byte offset
 * of the last packet written
 */
void serialize_packet_data(char* buffer, size_t size, packet_list* list,
		int* packets_written, int* last_packet_offset) {
	char* buffer_iter = buffer;
	int packet_count = 0;
	int offset = 0;
	while (list) {
		if (list->packet) {
			uint16_t amountToCopy = list->packet->len - PACKET_METADATA_LENGTH;
			size_t spaceLeft = size - (buffer_iter - buffer);
			if (spaceLeft > 0 && spaceLeft < amountToCopy) {
				amountToCopy = spaceLeft;
				offset = amountToCopy;
			}
			else if (spaceLeft <= 0) {
				break;
			}
			memcpy(buffer_iter, list->packet->data, amountToCopy);
			buffer_iter += amountToCopy;
		}
		list = list->next;
		packet_count++;
	}
	if (packets_written) {
		*packets_written = packet_count;
	}
	if (last_packet_offset) {
		*last_packet_offset = offset;
	}
}
