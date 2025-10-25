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
} StationQueue;

// Global variables - must be declared before functions
Train trains[1024];
int num_trains = 0;
struct timespec start_time;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER; // Single mutex for all queues
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
int threads_ready = 0;
int start_flag = 0;
FILE *output_file = NULL; // Initialize to NULL, open in main()

char *lastTrainDirection = NULL;
char *secondLastTrainDirection = NULL;

StationQueue *eastHighPriorityQueue;
StationQueue *eastLowPriorityQueue;
StationQueue *westHighPriorityQueue;
StationQueue *westLowPriorityQueue;

void init_queues()
{
	eastHighPriorityQueue = (StationQueue *)malloc(sizeof(StationQueue));
	eastHighPriorityQueue->head = NULL;
	eastHighPriorityQueue->tail = NULL;
	eastHighPriorityQueue->size = 0;

	eastLowPriorityQueue = (StationQueue *)malloc(sizeof(StationQueue));
	eastLowPriorityQueue->head = NULL;
	eastLowPriorityQueue->tail = NULL;
	eastLowPriorityQueue->size = 0;

	westHighPriorityQueue = (StationQueue *)malloc(sizeof(StationQueue));
	westHighPriorityQueue->head = NULL;
	westHighPriorityQueue->tail = NULL;
	westHighPriorityQueue->size = 0;

	westLowPriorityQueue = (StationQueue *)malloc(sizeof(StationQueue));
	westLowPriorityQueue->head = NULL;
	westLowPriorityQueue->tail = NULL;
	westLowPriorityQueue->size = 0;
}

void enqueue(StationQueue *queue, Train *train)
{
	TrainNode *new_node = (TrainNode *)malloc(sizeof(TrainNode));
	new_node->train = train;
	new_node->next = NULL;
	new_node->prev = NULL;

	pthread_mutex_lock(&queue_mutex); // Use global mutex

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

	pthread_mutex_unlock(&queue_mutex); // Use global mutex
}

// This will return the removed train for now, may not be needed.
Train *dequeue(StationQueue *queue)
{
	if (queue->size == 0)
	{
		return NULL;
	}

	pthread_mutex_lock(&queue_mutex); // Use global mutex

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

	pthread_mutex_unlock(&queue_mutex); // Use global mutex

	return temp->train;
}

Train *remove_train(StationQueue *queue, Train *train)
{
	if (queue->size == 0)
	{
		return NULL;
	}

	pthread_mutex_lock(&queue_mutex); // Use global mutex

	TrainNode *current = queue->head;
	while (current != NULL)
	{
		if (current->train == train)
		{
			// Found the train to remove
			if (current->prev != NULL)
			{
				current->prev->next = current->next;
			}
			else
			{
				queue->head = current->next; // Update head if needed
			}

			if (current->next != NULL)
			{
				current->next->prev = current->prev;
			}
			else
			{
				queue->tail = current->prev; // Update tail if needed
			}

			free(current);
			queue->size--;

			pthread_mutex_unlock(&queue_mutex); // Use global mutex

			return train;
		}
		current = current->next;
	}

	pthread_mutex_unlock(&queue_mutex); // Use global mutex

	return NULL; // Train not found
}

Train *peek(StationQueue *queue)
{
	if (queue->size == 0)
	{
		return NULL;
	}

	pthread_mutex_lock(&queue_mutex); // Use global mutex

	Train *result = queue->head->train;

	pthread_mutex_unlock(&queue_mutex); // Use global mutex

	return result;
}

Train *peek_two(StationQueue *queue)
{
	if (queue->size == 0 || queue->size == 1)
	{
		return NULL;
	}

	pthread_mutex_lock(&queue_mutex); // Use global mutex

	Train *result = queue->head->next->train;

	pthread_mutex_unlock(&queue_mutex); // Use global mutex

	return result;
}

void add_to_queue(Train *train)
{
	// Placeholder function to add train to the appropriate queue
	// based on its direction and priority.
	// Implementation of queues is not shown here.

	if (train->direction == EAST && train->priority == HIGH)
	{
		enqueue(eastHighPriorityQueue, train);
	}
	else if (train->direction == EAST && train->priority == LOW)
	{
		enqueue(eastLowPriorityQueue, train);
	}
	else if (train->direction == WEST && train->priority == HIGH)
	{
		enqueue(westHighPriorityQueue, train);
	}
	else
	{
		enqueue(westLowPriorityQueue, train);
	}
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
	// pthread_mutex_lock(&log_mutex);

	long elapsed = get_elapsed_tenths();
	char timestamp[13];
	format_timestamp(elapsed, timestamp);

	fprintf(output_file, "%s Train %2d is ready to go %s\n",
			timestamp,
			train->id,
			train->direction == EAST ? "East" : "West");
	fflush(output_file);

	// pthread_mutex_unlock(&log_mutex);
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

	add_to_queue(train);

	return NULL;
}

void select()
{
	/*
	Plan: Check the four queues with order of priority.
	1. If two highest priority trains ready
	*/

	// Next steps, return the selected, logic to check for the starvation.
	// Gonna need identical logic for the high priority trains as the low. Can you split to functions?

	Train *cross = NULL;

	int num_west_high = westHighPriorityQueue->size;
	int num_east_high = eastHighPriorityQueue->size;
	int num_west_low = westLowPriorityQueue->size;
	int num_east_low = eastLowPriorityQueue->size;

	int num_high = num_east_high + num_west_high;
	int num_low = num_east_low + num_west_low;
	int num_east = num_east_high + num_east_low;
	int num_west = num_west_high + num_west_low;

	if (!(num_east_high || num_west_high || num_east_low || num_west_low))
		return;

	// case the highest priority trains are the same direction

	if (num_high == 1)
	{
		if (num_east_high)
		{
			cross = dequeue(eastHighPriorityQueue);
		}
		else
		{
			cross = dequeue(westHighPriorityQueue);
		}
	}
	else if (num_high >= 2)
	{
		// We need to look if they are same direction or separate direction.
		if (num_east_high >= 2)
		{
			Train *temp1 = peek(eastHighPriorityQueue);
			Train *temp2 = peek_two(eastHighPriorityQueue);

			if (temp1->load_time > temp2->load_time)
			{
				cross = remove_train(eastHighPriorityQueue, temp2);
			}
			else if (temp2->load_time > temp1->load_time)
			{
				cross = remove_train(eastHighPriorityQueue, temp1);
			}
			else
			{
				// They are equal dequeue the one that reads first

				if (temp1->id < temp2->id)
				{
					cross = remove_train(eastHighPriorityQueue, temp1);
				}
				else
				{
					cross = remove_train(eastHighPriorityQueue, temp2);
				}
			}

			cross = dequeue(eastHighPriorityQueue);
		}
		else if (num_west_high >= 2)
		{
			Train *temp1 = peek(westHighPriorityQueue);
			Train *temp2 = peek_two(westHighPriorityQueue);

			if (temp1->load_time > temp2->load_time)
			{
				cross = remove_train(westHighPriorityQueue, temp2);
			}
			else if (temp2->load_time > temp1->load_time)
			{
				cross = remove_train(westHighPriorityQueue, temp1);
			}
			else
			{
				// They are equal dequeue the one that reads first

				if (temp1->id < temp2->id)
				{
					cross = remove_train(westHighPriorityQueue, temp1);
				}
				else
				{
					cross = remove_train(westHighPriorityQueue, temp2);
				}
			}

			cross = dequeue(westHighPriorityQueue);
		}
		else
		{
			// They are opposite direction
			// Check global variables
			if (lastTrainDirection == NULL)
			{

				dequeue(westHighPriorityQueue);
				lastTrainDirection = "WEST";
			}
			else if (lastTrainDirection == "EAST")
			{

				dequeue(westHighPriorityQueue);
				secondLastTrainDirection = lastTrainDirection;
				lastTrainDirection = "WEST";
			}
			else if (lastTrainDirection == "WEST")
			{

				dequeue(eastHighPriorityQueue);
				secondLastTrainDirection = lastTrainDirection;
				lastTrainDirection = "EAST";
			}
		}
	}
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
	output_file = fopen("output.txt", "w");
	init_queues(); // Initialize queues before using them!

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
	init_timer();
	pthread_cond_broadcast(&start_cond);
	pthread_mutex_unlock(&start_mutex);

	// Now main is the controller thread
	/*
	1. Monitor the queue's for a ready train (or maybe signal?)
	2. Select highest priority train
	3. Train cross (mutex)
	4. Repeat until ==
	5. Join all threads
	*/

	for (int i = 0; i < num_trains; i++)
	{
		pthread_join(trains[i].thread, NULL);
	}

	// Cleanup
	fclose(output_file);

	return 0;
}
