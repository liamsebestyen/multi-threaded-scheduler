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

Train trains[1024];

int num_trains = 0;

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
		printf("Train %d: direction=%c, load=%d, cross=%d\n",
			   i, trains[i].direction, trains[i].load_time, trains[i].cross_time);
	}

	return 0;
}
