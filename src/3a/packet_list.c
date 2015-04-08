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
		if (list->packet && ntohl(list->packet->seqno) == seqno) {
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
 * Insert a packet into the list, preserving sequence number order
 */
int insert_packet_in_order(packet_list** list, packet_list* packet) {
	if (!list || !packet || !(packet->packet)) {
		return -1;
	}
	if (!(*list)) {
		*list = packet;
		return 0;
	}
	if (get_packet_by_seqno(*list, ntohl(packet->packet->seqno))) {
		return 0;
	}
	packet_list* iter = *list;
	if (!iter->packet) {
		return -1;
	}
	packet_list* append_after = NULL;
	while (iter) {
		if (!iter->packet) {
			return -1;
		}
		if (ntohl(packet->packet->seqno) < ntohl(iter->packet->seqno)) {
			break;
		}
		if (!iter->next) {
			append_after = iter;
			break;
		}
		iter = iter->next;
	}
	if (append_after) {
		append_after->next = packet;
		packet->prev = append_after;
	}
	else {
		packet_list* old_prev = iter->prev;
		iter->prev = packet;
		packet->next = iter;
		packet->prev = old_prev;
		if (old_prev) {
			old_prev->next = packet;
		}
		else {
			*list = packet;
		}
	}
	return 0;
}

/**
 * Get the last consecutive sequence number in a list; if list is NULL, return 0,
 * i.e. the sequence number 1 before the first legal sequence number
 *
 * Return -1 on error
 */
int last_consecutive_sequence_number(packet_list* list) {
	if (!list) {
		return 0;
	}
	if (!(list->packet)) {
		return -1;
	}
	int last_seqno = ntohl(list->packet->seqno);
	list = list->next;
	while (list) {
		if (!(list->packet)) {
			return -1;
		}
		if (ntohl(list->packet->seqno) == last_seqno + 1) {
			last_seqno++;
		}
		else {
			break;
		}
		list = list->next;
	}
	return last_seqno;
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

void print_packet_list(packet_list* list, int indent_level) {
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
	//fprintf(stderr, "---------------------------\n");
	fprintf(stderr, "%sList size: %d\n", indents, packet_list_size(list));
	fprintf(stderr, "%sLast consecutive seqno: %d\n", indents, last_consecutive_sequence_number(list));
	/*
	while (list) {
		fprintf(stderr, "--------------\n");
		fprintf(stderr, "Seqno: %d\n", ntohl(list->packet->seqno));
		fprintf(stderr, "Ackno: %d\n", ntohl(list->packet->ackno));
		fprintf(stderr, "Length: %d\n", ntohs(list->packet->len));
		fprintf(stderr, "Data: |%s|\n", list->packet->data);
		fprintf(stderr, "--------------\n");
		list = list->next;
	}
	*/
	//fprintf(stderr, "---------------------------\n");
}
