CC = gcc
PROG = run
OBJS = List.o os-sim.o
 
all: $(OBJS)
	#$(CC) -o $(PROG) $(OBJS)

List.o: List.c
	$(CC) -c -o List.o List.c

os-sim.o: os-sim.c
	$(CC) -Wall -g -o os-sim.o List.o os-sim.c -lm

clean: 
	rm *.o
