#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
// Pattern matching program using MPI 
//
// Reads in control file indicating:
// 1. Whether to search for a single occurence or multiple
// 2. Which text file to use
// 3. Which pattern file to use
//
// Result of the pattern search is output to a file
// 
// If looking for a single occurence, a -2 will be output if a pattern is found
//
// If looking for multiple occurences, the indices of the pattern will be output
//
// In both cases, a -1 will be output to file if the pattern is not found
////////////////////////////////////////////////////////////////////////////////

const int found_tag = 50;

char *textData;
char *sub_textData;
int textLength;
int subTextLength;

int chunk;
const int master = 0;
int found;
int realIndex;
int found_flag;
int pattern_flag;
MPI_Request found_request;
MPI_Request bcast_request;

int *allPatterns;
int *recvcounts;
int totallen;

int world_rank;
int world_size;
char *patternData;
char **controlData = NULL;
int patternLength;
int controlLength;
long comparisonSum;
int indexFound;

int findMultiple;
int textNumber, patternNumber;

////////////////////////////////////////////////////////////////////////////////
// Function name: outOfMemory
//
// Description: Exits the program if the malloc function run out of memory
//
////////////////////////////////////////////////////////////////////////////////
void outOfMemory()
{
	fprintf (stderr, "Out of memory\n");
	exit (0);
}

////////////////////////////////////////////////////////////////////////////////
// Function name: readControlFile
//
// Description: Reads in the lines of the control file.
//				Stores them in an array to be used by master.
//
//				Function calculates array allocation, so large control files
//				will be accepted.
//
//
// Return: The array of character lines in the control file; else, returns null
////////////////////////////////////////////////////////////////////////////////
char** readControlFile(int* count) {
    FILE *file;
	char* filename = "inputs/control.txt";
	char** array = NULL;        /* Array of lines */
    int i;                   /* Loop counter */
    char line[100];           /* Buffer to read each line */
    int line_count;          /* Total number of lines */
    int line_length;         /* Length of a single line */
	int blank_line_count = 0;
    /* Clear output parameter. */
    *count = 0;

    /* Get the count of lines in the file */
	file = fopen(filename, "rt");
    if (file == NULL) {
        printf("Can't open file %s.\n", filename);
        return NULL;
	}
	
    line_count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {                                             
		line_count++;	   
    }

    /* Move to the beginning of file. */
    rewind(file);

    /* Allocate an array of pointers to strings 
     * (one item per line). */
    array = malloc(line_count * sizeof(char *));
    if (array == NULL) {
        outOfMemory(); /* Error */
    }
	int success;
    /* Read each line from file and deep-copy in the array. */
	int line_number = 0;
    for (i = 0; i < line_count; i++) {    
        /* Read the current line. */
        if(fgets(line, sizeof(line), file))
		{
			if(!isspace(line[0]))
			{
				/* Remove the ending '\n' from the read line. */
				line_length = strlen(line);        

				/* Allocate space to store a copy of the line. +1 for NUL terminator */
				array[line_number] = malloc(line_length + 1);

				/* Copy the line into the newly allocated space. */
				strcpy(array[line_number], line);
				line_number++;
			}
		}	
    }
	fclose (file);
    /* Write output param */
    *count = line_number;
    /* Return the array of lines */
    return array;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: readFromFile
//
// Description: Reads the characters from the text file passed in.
//				Assigns the values to the passed array, along with the length
//
////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
// Function name: readText
//
// Description: Allocates an array for the text file and then calls the readFromFile function
//				Allocated the filename for the file read
//
// Returns: 1 if successful; else, 0
////////////////////////////////////////////////////////////////////////////////
int readText (int textNumber)
{
	FILE *f;
	char fileName[1000];
#ifdef DOS
    sprintf (fileName, "inputs\\text%d.txt", textNumber);
#else
	sprintf (fileName, "inputs/text%d.txt", textNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL)
		return 0;
	readFromFile (f, &textData, &textLength);
	fclose (f);

	return 1;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: readPattern
//
// Description: Allocates an array for the pattern file and then calls the readFromFile function
//				Allocated the filename for the file read
//
// Returns: 1 if successful; else, 0
////////////////////////////////////////////////////////////////////////////////
int readPattern(int patternNumber)
{
	FILE *f;
	char fileName[1000];
#ifdef DOS
    sprintf (fileName, "inputs\\pattern%d.txt", patternNumber);
#else
	sprintf (fileName, "inputs/pattern%d.txt", patternNumber);
#endif
	f = fopen (fileName, "r");
	if (f == NULL)
		return 0;
	readFromFile (f, &patternData, &patternLength);
	fclose (f);

	return 1;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: writePatternToFile
//
// Description: Writes a single index to the output file
//
// Return: 0
////////////////////////////////////////////////////////////////////////////////
int writePatternToFile(int index)
{
	FILE *fp;
    fp = fopen ("result_MPI.txt","a");
    if (fp == NULL) 
        return 0;
    fprintf (fp, "%d %d %d\n", textNumber, patternNumber, index); 
    fclose (fp);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: patternFound
//
// Description: Check whether the pattern has been found by another process
//				Master process checks whether it has been notified of a found pattern
// 				Other processes check whether the master has been notified
//
//
// Return: 1 if pattern has been found; else, return 0;
////////////////////////////////////////////////////////////////////////////////
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
		found = 0;	
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

////////////////////////////////////////////////////////////////////////////////
// Function name: findPatternsSequentially
//
// Description: Searches for the pattern sequentially in the text file.
//				If searching for multiple occurences, writes found indices to file
// 				If searching for a single occurence, writes -2 to file
//
// Return: 1 if pattern was found; else, returns -1
////////////////////////////////////////////////////////////////////////////////
int findPatternsSequentially()
{
	int i,j,k, lastI;
	
	i=0;
	j=0;
	k=0;
	indexFound = -1;
	lastI = textLength-patternLength;

	while (i<=lastI && j<patternLength)
	{
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
		if (j == patternLength)
		{
			if (findMultiple)
			{
				writePatternToFile(i);
				indexFound = 1;
			}
			else
			{
				writePatternToFile(-2);
				return 1;
			}
		}
	}
	return indexFound;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: findPatternsOccurences
//
// Description: Called by MPI processes to search for pattern 
//				Each process collect the found pattern indices
//
// Return: 1 if pattern was found; else, returns -1
////////////////////////////////////////////////////////////////////////////////
int findPatternOccurences()
{
	int i,j,k, lastI, patternsFound;
	
	i=0;
	j=0;
	k=0;
	if (world_rank == master)
		lastI = subTextLength - patternLength;
	else
		lastI = subTextLength-patternLength-1;
	int length = 0;
	indexFound = -1;
	patternsFound = 0;
	int *patternIndices = malloc((subTextLength/patternLength)*sizeof(int));
	
	for(i = 0 ; i <=lastI; i++)
	{
		//Check every 1000 indices so that the test is not carried out too often.
		if(i % 1000 == 0 && findMultiple == 0)
		{
			//Check whether the pattern has been found. If so, stop searching.
			if (patternFound() == 1)
			{
				break;			
			}			
		}
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
		if (j == patternLength)
		{
			if (findMultiple == 1)
			{
				
				if (world_rank == master)
					realIndex = i + (chunk*(world_size-1));
				else
					realIndex = i + (world_rank - 1)*chunk;
				
				indexFound = 1;
				patternIndices[patternsFound] = realIndex;
				patternsFound += 1;	
			}
			else
			{		
				int pattern = 1;
				MPI_Send(&pattern, 1, MPI_INT, master, found_tag, MPI_COMM_WORLD);	
				
				indexFound = 1;				
			}
			
		}
	}
	printf("Search finished");
	MPI_Barrier(MPI_COMM_WORLD);
	printf("Process %d finished search", world_rank);
	if (findMultiple == 1)
	{
		/* Only root has the received data */
		if (world_rank == master)
		{
			recvcounts = malloc( world_size * sizeof(int)) ;
			if (recvcounts == NULL)
				outOfMemory();
		}
		
		MPI_Gather(&patternsFound, 1, MPI_INT,
				   recvcounts, 1, MPI_INT,
				   master, MPI_COMM_WORLD);
		
		totallen = 0;
		int *displs = NULL;
		

		if (world_rank == master) {
			displs = malloc( world_size * sizeof(int) );
			
			displs[0] = 0;
			totallen += recvcounts[0];
			
			int f;
			for (f=1; f<world_size; f++) {
			   totallen += recvcounts[f];   /* plus one for space or \0 after words */
			   displs[f] = displs[f-1] + recvcounts[f-1];
			}
			
			/* allocate string, pre-fill with spaces and null terminator */
			allPatterns = malloc(totallen * sizeof(int)); 	
			
		}
		
		MPI_Gatherv(patternIndices, patternsFound, MPI_INT,
					allPatterns, recvcounts, displs, MPI_INT,
					master, MPI_COMM_WORLD);
		
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return indexFound;
		
}
	
////////////////////////////////////////////////////////////////////////////////
// Function name: setupCommunication
//
// Description: Sets up the communication so that a successful process can nofify
//				the master process when a pattern is found. 
//				The master process expects a message from the successful process
//				The slave processes setup a broadcast to wait on a nofication 
//				from the master process.
//
////////////////////////////////////////////////////////////////////////////////
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

////////////////////////////////////////////////////////////////////////////////
// Function name: main
//
// Description: Main execution of program. 
//				Initiates the pattern searches.
//				Master reads in the pattern data and broadcasts it
//				Master reads in the text data, calculates the chunks, and sends out
//				the relevant data to each of the slave processes.
//				Processes search for the pattern in the file
//				Master prints results to file
//
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char * argv[]) {
    int i; /* Loop index */
    int line_count; /* Total number of read lines */
	MPI_File text, out;
	int ierr;
    int overlap = 100;

	//Remove the results file so that old results are removed
	remove("result_MPI.txt");
	
    //Initialises MPI environment
	MPI_Init(NULL, NULL);
	//Reads in process rank
	MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
	//Reads in world size
 	MPI_Comm_size(MPI_COMM_WORLD, &world_size);

	ierr = MPI_File_open(MPI_COMM_WORLD, "result_MPI.txt", MPI_MODE_CREATE|MPI_MODE_WRONLY, MPI_INFO_NULL, &out);
    if (ierr) {
        MPI_Finalize();
        exit(3);
    }
	
	int cont;
	int iteration = 0;
	
	
	if (world_rank == master)
	{
		/*Master reads in the control file*/
		controlData = readControlFile(&controlLength);
		
		/*If there are control lines, send a continue flag*/
		if (iteration == controlLength)
			cont = 0;
		else	
			cont = 1;
		MPI_Bcast(&cont, 1, MPI_INT, master, MPI_COMM_WORLD);
	}
	else
	{
		MPI_Bcast(&cont, 1, MPI_INT, master, MPI_COMM_WORLD);
	}
	
	
	/* Main loop of the program. Runs until the master broadcasts a new flag */
	while(cont == 1)
	{
		/*---------------------------------------------------------------------
		-- Section: Control file read
		--
		-- Description: Master reads in the values from the next control line
		--				Master broadcasts the findMultiple flag
		----------------------------------------------------------------------*/
        if (world_rank == master)
		{
			sscanf (controlData[iteration],"%d %d %d",&findMultiple,&textNumber,&patternNumber);
			MPI_Bcast(&findMultiple, 1, MPI_INT, master, MPI_COMM_WORLD);
		}		
		else
		{
			MPI_Bcast(&findMultiple, 1, MPI_INT, master, MPI_COMM_WORLD);
		}
		
		/*---------------------------------------------------------------------
		-- Section: Pattern file read
		--
		-- Description: Master reads in pattern data
		--				Master broadcasts the pattern data to the slave processes
		----------------------------------------------------------------------*/
		if(world_rank == master)
		{
			readPattern(patternNumber);		
			
			//Broacast the pattern length to the slave processes.
			MPI_Bcast(&patternLength, 1, MPI_INT, master, MPI_COMM_WORLD);
			printf("Pattern: %d\n", patternLength);
		}
		else
		{
			MPI_Bcast(&patternLength, 1, MPI_INT, master, MPI_COMM_WORLD);
			patternData = (char *)malloc(sizeof(char)*patternLength);
		}		
		MPI_Bcast(patternData, patternLength, MPI_CHAR, master, MPI_COMM_WORLD);
		
		/*---------------------------------------------------------------------
		-- Section: Text read and chunk sizes
		--
		-- Description: Master reads in text data
		--				Master calculates the chunk for each process
		--				Master broadcasts the size to each process
		----------------------------------------------------------------------*/
		int mastersize;
		int extendedsize;
		if (world_rank == 0)
		{
			readText(textNumber);
			printf("Text: %d\n", textLength);
			div_t sizes;
			sizes = div(textLength, world_size);
			subTextLength = sizes.quot;
			mastersize = subTextLength + sizes.rem;
			extendedsize = subTextLength+patternLength;
			
			MPI_Bcast(&extendedsize, 1, MPI_INT, master, MPI_COMM_WORLD);
			chunk = subTextLength;
		}
		else
		{
			MPI_Bcast(&subTextLength, 1, MPI_INT, master, MPI_COMM_WORLD);
			chunk = subTextLength - patternLength;
		}
		
		/*---------------------------------------------------------------------
		-- Section: Text check and sequential
		--
		-- Description: Master checks whether the pattern should be searched
		--				sequentially.
		--				Master searches sequentially if appropriate
		--
		----------------------------------------------------------------------*/
		if (patternLength > chunk)
		{
			if (world_rank == master)
			{ 
				if(patternLength > textLength)
				{
					writePatternToFile(-1);
				}
				else
				{
					int result = findPatternsSequentially();
					if (result == -1)
					writePatternToFile(-1);
				}
				
			}
		}
		
		/*---------------------------------------------------------------------
		-- Section: Sequential search for pattern
		--
		-- Description: Master sends the chunk of text to each process, 
		--				including an overlap.
		--				MPI communication set up to allow the processes to be 
		--				notified when the pattern has been found
		--
		----------------------------------------------------------------------*/
		else
		{
			if (world_rank == master)
			{
				int x;
				for (x = 1; x < world_size; x++)
				{
					int altindex = (x-1)*subTextLength;
					MPI_Send(&textData[altindex], extendedsize, MPI_CHAR, x, 1, MPI_COMM_WORLD);
				}
				
				sub_textData = (char *)malloc(sizeof(char)*(mastersize+1));
				int startPos = subTextLength*(world_size-1);
				memcpy(sub_textData, textData + startPos, mastersize*sizeof(char));
				sub_textData[mastersize] = '\0'; 
				chunk = subTextLength;
				subTextLength = mastersize;
			}
			else
			{
				sub_textData = (char *)malloc(sizeof(char)*(subTextLength+1));
				MPI_Recv(sub_textData, subTextLength, MPI_CHAR, master, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				sub_textData[subTextLength] = '\0';
			}
			setupCommunication();
			int masterResult;
			int result = findPatternOccurences();
			MPI_Reduce(&result, &masterResult, 1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);
			
			/*---------------------------------------------------------------------
			-- Section: Print results
			--
			-- Description: Master prints the returned results to file.
			--
			----------------------------------------------------------------------*/
			if (world_rank == master)
			{
				if (masterResult == 1 && findMultiple == 1)
				{
					if (totallen > 0)
					{
						int y;
						FILE *fp;
						fp = fopen ("result_MPI.txt","a");
						if (fp == NULL) 
							return 0;
						
						for(y=0; y<totallen;y++)					
							fprintf (fp, "%d %d %d\n", textNumber, patternNumber, allPatterns[y]);
						fclose (fp);
					}
				}
				else if (masterResult == 1)
				{
					writePatternToFile(-2);
				}
				else 
				{
					writePatternToFile(-1);
				}
			}		
		}
		
		
		//Check whether to continue the pattern search
		//If not, notify all processes.
		if (world_rank == master)
		{
			iteration++;
			if (iteration == controlLength)
				cont = 0;
			else	
				cont = 1;
			MPI_Bcast(&cont, 1, MPI_INT, master, MPI_COMM_WORLD);
		}
		else
		{
			MPI_Bcast(&cont, 1, MPI_INT, master, MPI_COMM_WORLD);
		}		
    }
	
    /* Cleanup. */
    if (world_rank == master)
	{
		for (i = 0; i < controlLength; i++) 
		{
			free(controlData[i]);
		}
	}
		
	//MPI_File_close(&out);
	
    free(controlData);
    controlData = NULL;

	MPI_Finalize();
    /* All right */
    return 0;
}