#include <omp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
// Pattern matching program using OMP
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

FILE *fp;

int chunk;
const int master = 0;
int found;

int found_flag;
int pattern_flag;

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
    fp = fopen ("result_OMP.txt","a");
    if (fp == NULL) 
        return 0;
    fprintf (fp, "%d %d %d\n", textNumber, patternNumber, index); 
    fclose (fp);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: findPatternsInText
//
// Description: OMP for loop to search for the pattern
//				If the pattern is found, it gets output to file
//
// Return: 1 if pattern was found; else, returns -1
////////////////////////////////////////////////////////////////////////////////
int findPatternsInText()
{
	int i,j,k,lastI, indexFound;
	long no_comparisons, no_patterns;
	
	lastI = textLength-patternLength;
	indexFound = -1;
	
    #pragma omp parallel default (none) shared (indexFound, fp) firstprivate (patternNumber, textNumber, textData, patternData, textLength, patternLength, lastI, findMultiple) private (i, j, k)
    {
		int minimumChunk;
		if (textLength < 10)
			minimumChunk = 1;
		else
			minimumChunk = textLength /10; 
		#pragma omp for schedule(guided, minimumChunk)
		for (i=0; i<=lastI;i++)
		{	
			if(indexFound == 1 && findMultiple!=1) continue;
			
			k=i;
			j=0;
			while (j<patternLength && (indexFound == -1 || findMultiple))
			{
				if (textData[k] == patternData[j])
				{					
					j++;
					k++;	
				}
				else 
				{
					j = patternLength+1;
				}						
			}
			
			if (j == patternLength)
			{			
				if (!findMultiple)
				{
					#pragma omp critical
					{
						if(indexFound == -1)
						{
							fprintf (fp, "%d %d %d\n", textNumber, patternNumber, -2); 
							indexFound = 1;
						}					
					}					
				}
				else
				{
					#pragma omp critical
					{
						fprintf (fp, "%d %d %d\n", textNumber, patternNumber, i); 
						indexFound = 1;
					}
					
				}
			}							
		}
			
	}
	return indexFound;
}

////////////////////////////////////////////////////////////////////////////////
// Function name: main
//
// Description: Main execution of program. 
//
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char * argv[]) {
    
    int i; /* Loop index */
    int line_count; /* Total number of read lines */
	int result;
    
	remove("result_OMP.txt");
	controlData = readControlFile(&controlLength);
    /* Read lines from file. */
	
	fp = fopen ("result_OMP.txt","a");
	for (i = 0; i < controlLength; i++) {
        sscanf (controlData[i],"%d %d %d",&findMultiple,&textNumber,&patternNumber);
		readText(textNumber);
		readPattern(patternNumber);
		if (textLength >= patternLength)
		{
			result = findPatternsInText();
			if (result == -1) 
				fprintf (fp, "%d %d %d\n", textNumber, patternNumber, -1); 
		}			
		else 
			fprintf (fp, "%d %d %d\n", textNumber, patternNumber, -1); 
		
		free(textData);
		free(patternData);
    }
	fclose(fp);
	
    /* Cleanup. */
    for (i = 0; i < controlLength; i++) {
        free(controlData[i]);
    }
	
	
    free(controlData);
    controlData = NULL;

    /* All right */
    return 0;
}