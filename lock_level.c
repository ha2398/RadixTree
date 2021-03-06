/*
 * lock_level.c
 */

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

static void radix_tree_init(struct radix_tree *tree, int bits, int radix)
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

	locks = malloc(sizeof(*locks) * tree->max_height);

	if (!locks) {
		die_with_error("failed to allocate mutexes\n");
	} else {
		int i;

		for (i = 0; i < tree->max_height; i++)
			pthread_mutex_init(&locks[i], NULL);
	}
}

/* Finds the appropriate slot to follow in the tree */
static int find_slot_index(unsigned long key, int levels_left, int radix)
{
	return key >> ((levels_left - 1) * radix) & ((1 << radix) - 1);
}

static void *radix_tree_find_alloc(struct radix_tree *tree, unsigned long key,
			    void *(*create)(unsigned long))
{
	int levels_left = tree->max_height;
	int radix = tree->radix;
	int n_slots = 1 << radix;
	int index;
	struct radix_node *current_node = tree->node;
	void **next_slot = NULL;
	void *slot;

	int current_level = 0;

	while (levels_left) {
		index = find_slot_index(key, levels_left, radix);

		pthread_mutex_lock(&locks[current_level]);

		next_slot = &current_node->slots[index];
		slot = *next_slot;

		if (slot) {
			current_node = slot;
		} else if (create) {
			void *new;

			if (levels_left != 1)
				new = calloc(sizeof(struct radix_node) +
					(n_slots * sizeof(void *)), 1);
			else
				new = create(key);

			if (!new)
				die_with_error("failed to create new node.\n");

			*next_slot = new;
			current_node = new;
		} else {
			pthread_mutex_unlock(&locks[current_level]);
			return NULL;
		}

		pthread_mutex_unlock(&locks[current_level]);

		current_level++;
		levels_left--;
	}

	return *next_slot;
}

static void *radix_tree_find(struct radix_tree *tree, unsigned long key)
{
	return radix_tree_find_alloc(tree, key, NULL);
}

struct radix_tree_desc lock_level_desc = {
	.name = "lock_level",
	.init = radix_tree_init,
	.find_alloc = radix_tree_find_alloc,
	.find = radix_tree_find,
};
