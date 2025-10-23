#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

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

// Global variables - must be declared before functions
Train trains[1024];
int num_trains = 0;
struct timespec start_time;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
int threads_ready = 0;
int start_flag = 0;
FILE *output_file = NULL; // Initialize to NULL, open in main()

void enqueue(StationQueue *queue, Train *train)
{
	TrainNode *new_node = (TrainNode *)malloc(sizeof(TrainNode));
	new_node->train = train;
	new_node->next = NULL;
	new_node->prev = NULL; // Damn first time opening this file after the breakup...

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

// This will return the removed train for now, may not be needed.
Train *dequeue(StationQueue *queue)
{
	if (queue->size == 0)
	{
		return NULL;
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

	return temp->train;
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

// Don't love the timer implementation as of now.

void init_timer()
{
	clock_gettime(CLOCK_MONOTONIC, &start_time);
}

long get_elapsed_tenths()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	long elapsed_sec = now.tv_sec - start_time.tv_sec;
	long elapsed_nsec = now.tv_nsec - start_time.tv_nsec;

	return elapsed_sec * 10 + (elapsed_nsec + 50000000) / 100000000;
}

void format_timestamp(long tenths, char *buffer)
{
	int minutes = tenths / 600; // 60 seconds * 10 tenths/sec
	int remaining = tenths % 600;
	int seconds = remaining / 10;
	int tenth = remaining % 10;

	sprintf(buffer, "%02d:%02d.%d", minutes, seconds, tenth);
}

void log_train_ready(Train *train)
{
	pthread_mutex_lock(&log_mutex);

	long elapsed = get_elapsed_tenths();
	char timestamp[13];
	format_timestamp(elapsed, timestamp);

	fprintf(output_file, "%s Train %2d is ready to go %s\n",
			timestamp,
			train->id,
			train->direction == EAST ? "East" : "West");
	fflush(output_file);

	pthread_mutex_unlock(&log_mutex);
}

void *train_thread(void *arg)
{
	Train *train = (Train *)arg;

	// Signal that this thread is ready
	pthread_mutex_lock(&start_mutex);
	threads_ready++;
	pthread_cond_broadcast(&start_cond); // Broadcast instead of signal
	// Wait for start signal
	while (!start_flag)
	{
		pthread_cond_wait(&start_cond, &start_mutex);
	}
	pthread_mutex_unlock(&start_mutex);

	// Simulate loading
	usleep(train->load_time * 100000);

	// Mark ready and log
	train->isReady = 1;
	log_train_ready(train);

	return NULL;
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

	// Initialize timer and output file
	init_timer();
	output_file = fopen("output.txt", "w"); // Open here, not at global scope

	if (!read_file(argv[1]))
	{
		return 1;
	}

	// Create all train threads
	for (int i = 0; i < num_trains; i++)
	{
		pthread_create(&trains[i].thread, NULL, train_thread, &trains[i]);
	}

	// Wait until all threads are ready
	pthread_mutex_lock(&start_mutex);
	while (threads_ready < num_trains)
	{
		pthread_cond_wait(&start_cond, &start_mutex);
	}
	// Release all trains at once
	start_flag = 1;
	pthread_cond_broadcast(&start_cond);
	pthread_mutex_unlock(&start_mutex);

	// Wait for all trains to finish
	for (int i = 0; i < num_trains; i++)
	{
		pthread_join(trains[i].thread, NULL);
	}

	// Cleanup
	fclose(output_file);

	return 0;
}
