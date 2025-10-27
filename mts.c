#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

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
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t train_can_cross = PTHREAD_COND_INITIALIZER;
pthread_mutex_t track_mutex = PTHREAD_MUTEX_INITIALIZER;
Train *selected_train = NULL;
int threads_ready = 0;
int start_flag = 0;
FILE *output_file = NULL;

char *lastTrainDirection = NULL;
char *secondToLastTrainDirection = NULL;

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

	pthread_mutex_lock(&queue_mutex);

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

// This will return the removed train

Train *dequeue(StationQueue *queue)
{
	pthread_mutex_lock(&queue_mutex); // Lock first

	if (queue->size == 0)
	{
		pthread_mutex_unlock(&queue_mutex);
		return NULL;
	}

	TrainNode *temp = queue->head;
	Train *result = temp->train; // Save the train pointer

	queue->head = queue->head->next;
	if (queue->head != NULL)
	{
		queue->head->prev = NULL;
	}
	else
	{
		queue->tail = NULL;
	}

	free(temp);
	queue->size--;

	pthread_mutex_unlock(&queue_mutex);

	return result;
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
				queue->head = current->next;
			}

			if (current->next != NULL)
			{
				current->next->prev = current->prev;
			}
			else
			{
				queue->tail = current->prev;
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

	if (elapsed_nsec < 0)
	{
		elapsed_sec--;
		elapsed_nsec += 1000000000;
	}

	return elapsed_sec * 10 + elapsed_nsec / 100000000;
}

void format_timestamp(long tenths, char *buffer)
{
	int hours = tenths / 36000;
	int remaining = tenths % 36000;
	int minutes = remaining / 600;
	remaining = remaining % 600;
	int seconds = remaining / 10;
	int tenth = remaining % 10;

	sprintf(buffer, "%02d:%02d:%02d.%d", hours, minutes, seconds, tenth);
}

void log_train_ready(Train *train)
{
	long elapsed = get_elapsed_tenths();
	char timestamp[13];
	format_timestamp(elapsed, timestamp);

	fprintf(output_file, "%s Train %2d is ready to go %s\n",
			timestamp,
			train->id,
			train->direction == EAST ? "East" : "West");
	fflush(output_file);
}

void log_train_crossing(Train *train)
{
	long elapsed = get_elapsed_tenths();
	char timestamp[13];
	format_timestamp(elapsed, timestamp);

	fprintf(output_file, "%s Train %2d is ON the main track going %s\n",
			timestamp,
			train->id,
			train->direction == EAST ? "East" : "West");
	fflush(output_file);
}

void log_train_complete(Train *train)
{
	long elapsed = get_elapsed_tenths();
	char timestamp[13];
	format_timestamp(elapsed, timestamp);

	fprintf(output_file, "%s Train %2d is OFF the main track after going %s\n",
			timestamp,
			train->id,
			train->direction == EAST ? "East" : "West");
	fflush(output_file);
}

void *train_thread(void *arg)
{
	Train *train = (Train *)arg;

	// Signal that this thread is ready
	pthread_mutex_lock(&start_mutex);
	threads_ready++;
	pthread_cond_broadcast(&start_cond);
	while (!start_flag)
	{
		pthread_cond_wait(&start_cond, &start_mutex);
	}
	pthread_mutex_unlock(&start_mutex);

	usleep(train->load_time * 100000);

	train->isReady = 1;
	log_train_ready(train);

	add_to_queue(train);

	// Wait for THIS specific train to be selected
	pthread_mutex_lock(&track_mutex);
	while (selected_train != train)
	{
		pthread_cond_wait(&train_can_cross, &track_mutex);
	}

	// This train was selected - cross the track
	log_train_crossing(train);
	usleep(train->cross_time * 100000);
	log_train_complete(train);

	// Done crossing
	selected_train = NULL;
	pthread_mutex_unlock(&track_mutex);

	return NULL;
}

void update_direction_tracking(Direction dir)
{
	secondToLastTrainDirection = lastTrainDirection;
	lastTrainDirection = (dir == EAST) ? "EAST" : "WEST";
}

Train *select_from_priority_level(StationQueue *east_queue, StationQueue *west_queue)
{
	int num_east = east_queue->size;
	int num_west = west_queue->size;

	// No trains at this priority level
	if (num_east == 0 && num_west == 0)
	{
		return NULL;
	}

	// Only one direction has trains
	if (num_east > 0 && num_west == 0)
	{
		Train *selected = dequeue(east_queue);
		update_direction_tracking(EAST);
		return selected;
	}

	if (num_west > 0 && num_east == 0)
	{
		Train *selected = dequeue(west_queue);
		update_direction_tracking(WEST);
		return selected;
	}

	// Both directions have trains - alternate based on last direction
	if (!(lastTrainDirection == NULL || strcmp(lastTrainDirection, "EAST")))
	{
		// First train or last was east - go west
		Train *selected = dequeue(west_queue);
		update_direction_tracking(WEST);
		return selected;
	}
	else
	{
		// Last was west - go east
		Train *selected = dequeue(east_queue);
		update_direction_tracking(EAST);
		return selected;
	}
}

Train *check_starvation()
{
	// Can't check starvation if we haven't had 2 trains yet
	if (lastTrainDirection == NULL || secondToLastTrainDirection == NULL)
	{
		return NULL;
	}

	// Check if last two trains went same direction
	if (strcmp(lastTrainDirection, secondToLastTrainDirection) == 0)
	{
		// Force opposite direction
		if (strcmp(lastTrainDirection, "EAST") == 0)
		{
			// Last two were east, must go west
			Train *selected = dequeue(westHighPriorityQueue);
			if (selected == NULL)
			{
				selected = dequeue(westLowPriorityQueue);
			}
			if (selected != NULL)
			{
				update_direction_tracking(WEST);
			}
			return selected;
		}
		else
		{
			// Last two were west, must go east
			Train *selected = dequeue(eastHighPriorityQueue);
			if (selected == NULL)
			{
				selected = dequeue(eastLowPriorityQueue);
			}
			if (selected != NULL)
			{
				update_direction_tracking(EAST);
			}
			return selected;
		}
	}

	return NULL;
}

Train *select_next_train()
{
	// Check high priority first, then low priority
	Train *selected = check_starvation();
	if (selected != NULL)
	{
		return selected;
	}

	selected = select_from_priority_level(eastHighPriorityQueue, westHighPriorityQueue);

	if (selected == NULL)
	{
		selected = select_from_priority_level(eastLowPriorityQueue, westLowPriorityQueue);
	}

	return selected;
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

	// Main dispatcher loop
	int trains_crossed = 0;
	while (trains_crossed < num_trains)
	{
		Train *next_train = select_next_train();

		if (next_train == NULL)
		{
			// No trains ready yet, wait a bit
			usleep(10000); // 0.01 seconds
			continue;
		}

		// Signal this train to cross
		pthread_mutex_lock(&track_mutex);
		selected_train = next_train;
		pthread_cond_broadcast(&train_can_cross);
		pthread_mutex_unlock(&track_mutex);

		// Wait for train to finish crossing
		while (selected_train != NULL)
		{
			usleep(10000);
		}

		trains_crossed++;
	}

	// Wait for all trains to finish
	for (int i = 0; i < num_trains; i++)
	{
		pthread_join(trains[i].thread, NULL);
	}

	// Cleanup
	fclose(output_file);

	return 0;
}
