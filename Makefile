CFLAGS=-g -O3 -Wall -Wextra -DDEBUG
all: smv_reflector smv_replicator

smv_replicator.c: smv.h

smv_replicate.c: smv.h

smv_reflector.c: smv.h

smv_sender.c: smv.h

smv_receiver.c: smv.h

sizes.c: smv.h

smv_sender.o: smv_sender.c
	gcc $(CFLAGS) -Wall -Wextra -c smv_sender.c

smv_receiver.o: smv_receiver.c
	gcc $(CFLAGS) -Wall -Wextra -c smv_receiver.c

smv_reflector.o: smv_reflector.c
	gcc $(CFLAGS) -Wall -Wextra -c smv_reflector.c

smv_replicate.o: smv_replicate.c
	gcc $(CFLAGS) -Wall -Wextra -c smv_replicate.c

sizes.o: sizes.c
	gcc $(CFLAGS) -Wall -Wextra -c sizes.c



smv_reflector: smv_reflector.o smv_sender.o
	gcc $(CFLAGS) -Wall -Wextra -o smv_reflector smv_reflector.o smv_sender.o

smv_replicator: smv_replicator.o smv_receiver.o smv_replicate.o
	gcc $(CFLAGS) -Wall -Wextra -o smv_replicator smv_replicator.o smv_receiver.o smv_replicate.o

sizes: sizes.c
	gcc $(CFLAGS) -Wall -Wextra -o sizes sizes.o

.PHONY: clean

clean:
	rm -f a.out *.o smv_reflector smv_replicator sizes

