#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef enum
{
	EAST,
	WEST
} Direction;

typedef enum
{
	LOW,
	HIGH
} Priority;

typedef struct
{
	int id;
	Direction direction; // 'E', 'e', 'W', 'w'
	Priority priority;
	int load_time;
	int cross_time;
	int isReady; // 1 for true, 0 for false
	pthread_t thread;
} Train;

typedef struct TrainNode
{
	Train *train;
	struct TrainNode *next;
	struct TrainNode *prev;
} TrainNode;

typedef struct
{
	TrainNode *head;
	TrainNode *tail;
	int size;
	pthread_mutex_t lock_queue;
} StationQueue;

Train trains[1024];

int num_trains = 0;

void enqueue(StationQueue *queue, Train *train)
{
	TrainNode *new_node = (TrainNode *)malloc(sizeof(TrainNode));
	new_node->train = train;
	new_node->next = NULL;
	new_node->prev = NULL;

	pthread_mutex_lock(&queue->lock_queue); // Maybe we lock this entire function later?

	if (queue->size == 0)
	{
		queue->head = new_node;
		queue->tail = new_node;
	}
	else
	{
		queue->tail->next = new_node;
		new_node->prev = queue->tail;
		queue->tail = new_node;
	}
	queue->size++;

	pthread_mutex_unlock(&queue->lock_queue); // Once again this may be be locked main later.
}

void dequeue(StationQueue *queue)
{
	if (queue->size == 0)
	{
		return;
	}

	pthread_mutex_lock(&queue->lock_queue); // Again maybe we lock this entire function later?

	TrainNode *temp = queue->head;
	queue->head = queue->head->next;
	if (queue->head != NULL)
	{
		queue->head->prev = NULL;
	}
	else
	{
		queue->tail = NULL; // Queue is now empty
	}
	free(temp);
	queue->size--;

	pthread_mutex_unlock(&queue->lock_queue); // Once again this may be be locked main later.
}

Train *peek(StationQueue *queue)
{
	Train **train;
	if (queue->size == 0)
	{
		return NULL;
	}

	pthread_mutex_lock(&queue->lock_queue); // Maybe we lock this entire function later?

	return queue->head->train;

	pthread_mutex_unlock(&queue->lock_queue); // Once again this may be be locked main later.
}

Direction get_direction(char dir_char)
{
	return (dir_char == 'E' || dir_char == 'e') ? EAST : WEST;
}

Priority get_priority(char dir_char)
{
	return (dir_char == 'E' || dir_char == 'W') ? HIGH : LOW;
}

int read_file(const char *filename)
{
	char direction;
	int load_time;
	int cross_time;

	FILE *file = fopen(filename, "r");
	if (!file)
	{
		fprintf(stderr, "Error: Cannot open the file '%s'\n", filename);
		return 0;
	}

	// Parse each line to trains

	while (fscanf(file, " %c %d %d", &direction, &load_time, &cross_time) != EOF)
	{
		Train new_train;
		new_train.direction = get_direction(direction);
		new_train.priority = get_priority(direction);
		new_train.load_time = load_time;
		new_train.cross_time = cross_time;
		new_train.isReady = 0;
		new_train.id = num_trains;
		trains[num_trains] = new_train;
		num_trains++;
	}

	if (num_trains == 0)
	{
		fprintf(stderr, "Error: No trains found in file\n");
		fclose(file);
		return 0;
	}

	fclose(file);
	return 1;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
		return 1;
	}

	if (!read_file(argv[1]))
	{
		return 1;
	}

	printf("Read %d trains:\n", num_trains);
	for (int i = 0; i < num_trains; i++)
	{
		printf("Train %d: direction=%s, priority=%s, load_time=%d, cross_time=%d, isReady=%d\n",
			   trains[i].id,
			   trains[i].direction == EAST ? "EAST" : "WEST",
			   trains[i].priority == HIGH ? "HIGH" : "LOW",
			   trains[i].load_time,
			   trains[i].cross_time,
			   trains[i].isReady);
	}

	return 0;
}
