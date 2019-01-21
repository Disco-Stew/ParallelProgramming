#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>


////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////

char *textData;
int textLength;

char *patternData;
int patternLength;

clock_t c0, c1;
time_t t0, t1;

void outOfMemory()
{
	fprintf (stderr, "Out of memory\n");
	exit (0);
}

void readFromFile (FILE *f, char **data, int *length)
{
	int ch;
	int allocatedLength;
	char *result;
	int resultLength = 0;

	allocatedLength = 0;
	result = NULL;

	ch = fgetc (f);
	while (ch >= 0)
	{
		resultLength++;
		if (resultLength > allocatedLength)
		{
			allocatedLength += 10000;
			result = (char *) realloc (result, sizeof(char)*allocatedLength);
			if (result == NULL)
				outOfMemory();
		}
		result[resultLength-1] = ch;
		ch = fgetc(f);
	}
	*data = result;
	*length = resultLength;
}

int readText ()
{
	FILE *f;
	char fileName[1000];
#ifdef DOS
        sprintf (fileName, "inputs\\text.txt");
#else
	sprintf (fileName, "inputs/text.txt");
#endif
	f = fopen (fileName, "r");
	if (f == NULL)
		return 0;
	readFromFile (f, &textData, &textLength);
	fclose (f);

	return 1;

}

int readPattern(int testNumber)
{
	FILE *f;
	char fileName[1000];
#ifdef DOS
        sprintf (fileName, "inputs\\pattern%d.txt", testNumber);
#else
	sprintf (fileName, "inputs/pattern%d.txt", testNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL)
		return 0;
	readFromFile (f, &patternData, &patternLength);
	fclose (f);

	return 1;
}



int hostMatch(long *comparisons)
{
	int i,j,k, lastI;
	
	i=0;
	j=0;
	k=0;
	lastI = textLength-patternLength;
        *comparisons=0;

	while (i<=lastI && j<patternLength)
	{
        (*comparisons)++;
		if (textData[k] == patternData[j])
		{
			k++;
			j++;
		}
		else
		{
			i++;
			k=i;
			j=0;
		}
	}
	if (j == patternLength)
		return i;
	else
		return -1;
}
void processData()
{
	unsigned int result;
        long comparisons;

	result = hostMatch(&comparisons);
	if (result == -1)
		printf ("Pattern not found\n");
	else
		printf ("Pattern found at position %d\n", result);
        printf ("# comparisons = %ld\n", comparisons);

}

int main(int argc, char **argv)
{
	int testNumber;

	if (!readText())
	{
        printf("Unable to open text file");
		return 0;
	}
	
	//Initialise MPI environment
	MPI_Init(NULL, NULL);
	int world_rank;
 	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
 	int world_size;
 	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	
	//If there are fewer than 2 processes, kill the execution.
	if (world_size < 2)
       	{
    		fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    		MPI_Abort(MPI_COMM_WORLD, 1);
  	}
	
	//Process 0 prints the number of patterns, so that it is not printed multiple times.
	if(world_rank == 0)
	{
		printf("Pattern search using %d processes\n", world_size);
	}
	
	//Set the testNumber so that each rank processes different pattern files.
	testNumber = world_rank+1;
	
	//While there is still another pattern to process, search the text.
	while (readPattern(testNumber))
	{
		c0 = clock(); t0 = time(NULL);
   	 	processData();
		c1 = clock(); t1 = time(NULL);

		printf("Test %d run by process %d\n", testNumber, world_rank);
        printf("Test %d elapsed wall clock time = %ld\n", testNumber, (long) (t1 - t0));
        printf("Test %d elapsed CPU time = %f\n\n", testNumber, (float) (c1 - c0)/CLOCKS_PER_SEC); 
		testNumber+=world_size;
	}
	
	//End of MPI section
	MPI_Finalize();
}
