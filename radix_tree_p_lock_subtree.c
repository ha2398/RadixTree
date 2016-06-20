/*
 * radix_tree_p_lock_subtree.c
 */

#include <pthread.h>
#include "radix_tree.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

/*
 * Global variables 
 */
static pthread_mutex_t *locks;

/* Prints an error @message and stops execution */
static void die_with_error(char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

void radix_tree_init(struct radix_tree *tree, int bits, int radix)
{
	if (radix < 1) {
		perror("Invalid radix\n");
		return;
	}

	if (bits < 1) {
		perror("Invalid number of bits\n");
		return;
	}

	unsigned long n_slots = 1L << radix;

	tree->radix = radix;
	tree->max_height = DIV_ROUND_UP(bits, radix);

	tree->node = calloc(sizeof(struct radix_node) +
		     		  (n_slots * sizeof(void *)), 1);

	if (!tree->node)
		die_with_error("failed to create new node.\n");

	locks = malloc(sizeof(*locks) * n_slots);

	if (!locks) {
		die_with_error("failed to allocate mutexes\n");
	} else {
		int i;

		for (i = 0; i < n_slots; i++)
			pthread_mutex_init(&locks[i], NULL);
	}
}

/* Finds the appropriate slot to follow in the tree */
static int find_slot_index(unsigned long key, int levels_left, int radix)
{
	return (int) (key >> (levels_left * radix) & ((1 << radix) - 1));
}

void *radix_tree_find_alloc(struct radix_tree *tree, unsigned long key,
			    void *(*create)(unsigned long))
{
	int levels_left = tree->max_height - 1;
	int radix = tree->radix;
	int n_slots = 1 << radix;
	int index;
	int subtree;

	struct radix_node *current_node = tree->node;
	void **next_slot = NULL;

	subtree = find_slot_index(key, levels_left, radix);

	if (create)
		pthread_mutex_lock(&locks[subtree]);

	while (levels_left) {
		index = find_slot_index(key, levels_left, radix);

		next_slot = &current_node->slots[index];

		if (*next_slot) {
			current_node = *next_slot;
		} else if (create) {
			*next_slot = calloc(sizeof(struct radix_node) +
		     		  	   (n_slots * sizeof(void *)), 1);

			if (!*next_slot)
				die_with_error("failed to create new node.\n");
			else
				current_node = *next_slot;
		} else {
			return NULL;
		}

		levels_left--;
	}

	index = find_slot_index(key, levels_left, radix);
	next_slot = &current_node->slots[index];

	if (!(*next_slot) && create)
		*next_slot = create(key);

	if (create)
		pthread_mutex_unlock(&locks[subtree]);

	return *next_slot;
}

void *radix_tree_find(struct radix_tree *tree, unsigned long key)
{
	return radix_tree_find_alloc(tree, key, NULL);
}

static void radix_tree_delete_node(struct radix_node *node, int n_slots,
				   int levels_left)
{
	int i;
	struct radix_node *next_node = NULL;

	if (levels_left) {
		for (i = 0; i < n_slots; i++) {
			next_node = node->slots[i];

			if (next_node) {
				radix_tree_delete_node(next_node, n_slots,
						       levels_left - 1);
				free(next_node);
			}
		}
	} else {
		for (i = 0; i < n_slots; i++) {
			if (node->slots[i])
				free(node->slots[i]);
		}
	}
}

void radix_tree_delete(struct radix_tree *tree)
{
	int i;
	int n_slots = 1 << tree->radix;

	for (i = 0; i < n_slots; i++)
		pthread_mutex_destroy(&locks[i]);

	free(locks);

	radix_tree_delete_node(tree->node, n_slots, tree->max_height - 1);
	free(tree->node);
}