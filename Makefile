CC=gcc
flags=-c -Wall
executable_name=test
driver=driver


all: 		$(driver).o tasks.o allocator.o scheduler.o dp_slack.o
		 $(CC) $(driver).o tasks.o allocator.o scheduler.o dp_slack.o -o $(executable_name) -lm -g
		@echo "Executable generated -> test"

$(driver).o: 	$(driver).c
		$(CC) $(flags) $(driver).c

tasks.o: 	tasks.c
		$(CC) $(flags) tasks.c

allocator.o: 	allocator.c
		$(CC) $(flags) allocator.c

scheduler.o: 	scheduler.c
		$(CC) $(flags) scheduler.c

dp_slack.o: 	dp_slack.c
		$(CC) $(flags) dp_slack.c 

clean:		
		rm -f *.o $(executable_name)
