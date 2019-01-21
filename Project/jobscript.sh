#!/bin/bash

# Execute job from current working directory
#$ -cwd

# Gives the name for output of execution
#$ -o searching_MPI_1.out

# Ask the scheduler for allocating 2 MPI slots
#$ -pe mpislots-verbose 4

# Load mpi module
module add mpi/openmpi

# Prints date
date

# Compiling the Program
mpicc $1.c -o $1

# Prints starting new job
echo "Starting new job"

# Executes the compiled program on 8 MPI processes
time mpirun -np 2 $1

# Prints divider
echo "------------------------"
echo ""

# Executes the compiled program on 8 MPI processes
time mpirun -np 4 $1

# Prints divider
echo "------------------------"
echo ""

# Executes the compiled program on 8 MPI processes
time mpirun -np 8 --oversubscribe $1

# Prints finished job
echo "Finished job"

# Prints date
date
