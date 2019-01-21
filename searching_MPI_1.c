#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

////////////////////////////////////////////////////////////////////////////////
// Program main
////////////////////////////////////////////////////////////////////////////////

//Message tag so that the master can accept a message from any process
const int found_tag = 50;

char *textData;
char *sub_textData;
int textLength;

int chunk;
const int master = 0;
int found;

int found_flag;
int pattern_flag;
MPI_Request found_request;
MPI_Request bcast_request;

int world_rank;
int world_size;
char *patternData;
int patternLength;
long comparisonSum;
int indexFound;
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

//Checks whether the pattern has been found in the text
int patternFound()
{
	//Master process
	if (world_rank == master)
	{
		found = 0;
		//Test whether a message has been received from any process 
		//indicating that the pattern has been found.
		MPI_Test(&found_request, &found, MPI_STATUS_IGNORE);
		
		//If the pattern is found, join the asynchronous broadcast and notify the slaves.
		if (found != 0) 
		{
			MPI_Ibcast(&found, 1, MPI_INT, master, MPI_COMM_WORLD, &bcast_request);
			
			//Wait for the slaves to receive the data before continuing execution.
			MPI_Wait(&bcast_request, MPI_STATUS_IGNORE);
			return 1;
		}
		else
		{
			//Pattern not found yet.
			return 0;
		}
	}
	//Slave processes
	else
	{
		int found = 0;	
		//Test whether the asynchronous broadcast from the master process has happened.
		MPI_Test(&bcast_request, &found, MPI_STATUS_IGNORE);	
	
		//If the broadcast has happened, stop searching.
		if (found != 0) 
		{			
			return 1;
		}
		else
		{
			//Pattern not found yet.
			return 0;
		}
	}
}

int hostMatch(long *comparisons)
{
	int i,j,k, lastI;
	
	i=0;
	j=0;
	k=0;
	lastI = chunk-patternLength;
    *comparisons=0;
		
	while (i<=lastI && j<patternLength)
	{	
		//Only check every 2000 so that execution time is not slowed down
		if(i % 2000 == 0)
		{
			//Check whether the pattern has been found. If so, stop searching.
			if (patternFound() == 1)
			{
				break;			
			}
				
		}
		
	    (*comparisons)++;
		if (sub_textData[k] == patternData[j])
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
	
	//If the pattern is found, the process sends a message to the master with the relevant tag.
	if (j == patternLength)
	{
		int pattern = 1;
		MPI_Send(&pattern, 1, MPI_INT, master, found_tag, MPI_COMM_WORLD);
		return i;
	}
	else
		return -1;
}

//Instructs each process to start searching, and afterwards collects the results.
void processData()
{
	int result;
    long comparisons;
	int index = 0;
	
	//Search for the pattern.
	result = hostMatch(&comparisons);
	
	//The result must be adjusted as the index where the pattern is found
	//depends on which section of text was being searched.
	if (result != -1)
		index = result + (world_rank*chunk);	
	
	//Reduce the outcome for the pattern search from all the processes into variables 
	//in the master process. 
	MPI_Reduce(&comparisons, &comparisonSum, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
	MPI_Reduce(&index, &indexFound, 1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);
	
	//Master process prints out the results.
	if(world_rank == 0)
	{	
		if (indexFound == -1)
		{
			printf ("Pattern not found\n");
		}
		else
		{
			printf ("Pattern found at position %d\n", indexFound);
			printf ("# comparisons = %ld\n", comparisonSum);
			printf ("-------------------------\n");
		}
	}
}

//Sets up the communication to allow the master to notify slaves to stop searching
void setupCommunication()
{
	found_request = MPI_REQUEST_NULL;
	bcast_request = MPI_REQUEST_NULL;
	found_flag = 0;
	//If process is master, setup an asynchronous receive using a global flag. 
	//This message could originate from any process, but will have to use this tag.
	if (world_rank == 0)
	{
		MPI_Irecv(&pattern_flag, 1, MPI_INT, MPI_ANY_SOURCE, found_tag, MPI_COMM_WORLD, &found_request);
	}
	//If the process is a slave, setup an asynchronous broadcast to wait for communication
	//from the master.
	else
	{
		MPI_Ibcast(&found_flag, 1, MPI_INT, master, MPI_COMM_WORLD, &bcast_request);
	}
}

int main(int argc, char **argv)
{
	int testNumber;
	
	//Initialise the MPI environment.
	MPI_Init(NULL, NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
 	MPI_Comm_size(MPI_COMM_WORLD, &world_size);
	
	//If the world size is less than 2 the master slave model will not function, so kill the exectution
	if (world_size < 2)
    {
    		fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
    		MPI_Abort(MPI_COMM_WORLD, 1);
  	}
	
	//Master process loads in the text file
	if(world_rank == 0)
	{
		printf("Pattern search using %d processes\n", world_size);
		if (!readText())
		{
			printf("Unable to open text file");
			return 0;
		}
		printf ("Text length = %d\n", textLength);
		//Master process notifies the slaves of the size of the text file
		MPI_Bcast(&textLength, 1, MPI_INT, 0, MPI_COMM_WORLD);		
	}
	else
	{
		MPI_Bcast(&textLength, 1, MPI_INT, 0, MPI_COMM_WORLD);
		textData = (char *)malloc(sizeof(char)*textLength);
	}
	
	//Calculate the chunk size depending on how many processes are in use.
	chunk = textLength/world_size;
	sub_textData = (char *)malloc(sizeof(char)*chunk);	
	
	//Scatter the text data between the processes.
	MPI_Scatter(textData, chunk, MPI_CHAR, sub_textData, chunk, MPI_CHAR, 0, MPI_COMM_WORLD);
	int patternNumber;
	
	//Infinite loop.
	//All executions will eventually break so there is no chance of deadlock.
	while(1)
	{		
		patternLength = 0;		
		patternNumber++;
		
		//Master process reads in the next pattern file to be searched for.
		if(world_rank == master)
		{
			//If the pattern file is not found or cannot be opened, zero
			//is broadcast instead of the pattern length. This instructs the slaves
			//to break from the while loop.
			if (!readPattern(patternNumber))
			{
				MPI_Bcast(&patternLength, 1, MPI_INT, master, MPI_COMM_WORLD);
				break;
			} 
			printf("Pattern number : %d\n", patternNumber);
			printf ("Pattern length = %d\n", patternLength);
			
			//Broacast the pattern length to the slave processes.
			MPI_Bcast(&patternLength, 1, MPI_INT, master, MPI_COMM_WORLD);
		}
		else
		{
			MPI_Bcast(&patternLength, 1, MPI_INT, master, MPI_COMM_WORLD);
			patternData = (char *)malloc(sizeof(char)*patternLength);
			
			//If pattern length is 0, break out of the while loop as there
			//are no more patterns to be searched for.
			if (patternLength == 0)
			{
				break;
			}
		}		
		
		//Master broadcasts the pattern data to the slave processes
		MPI_Bcast(patternData, patternLength, MPI_CHAR, master, MPI_COMM_WORLD);
		setupCommunication();
		processData();
		free(patternData);
	}
	//Free the buffers that were allocated memory from the heap.
	free(textData);
	free(sub_textData);

	//End of MPI section
	MPI_Finalize();
}
