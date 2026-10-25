#CFLAGS=-g -O3 -Wall -Wextra -DDEBUG
# to build with debug invoke make like this
# 	make OPTS=-DDEBUG
CFLAGS=-g -O3 -Wall -Wextra $(OPTS)

CPP = g++
CC = gcc

SRC = .
BIN_DIR = ./bin
OBJS_DIR = ./obj
LIB_DIR = ./lib

all: $(BIN_DIR)/smv_replicator $(BIN_DIR)/smv_reflector 


$(OBJS_DIR)/%.o: $(SRC)/%.cpp | $(OBJS_DIR)
	$(CPP) $(CFLAGS) -c -o $(@) $< $(INCLUDES)

$(OBJS_DIR)/%.o: %.c | $(OBJS_DIR)
	$(CC) $(CFLAGS) -c -o $(@) $< $(INCLUDES)


REPLICATOR_OBJS = $(OBJS_DIR)/smv_receiver.o \
				  $(OBJS_DIR)/smv_replicate.o

REFLECTOR_OBJS = $(OBJS_DIR)/smv_sender.o


$(BIN_DIR)/smv_reflector: $(OBJS_DIR)/smv_reflector.o $(REFLECTOR_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(@) $^ 

$(BIN_DIR)/smv_replicator: $(OBJS_DIR)/smv_replicator.o $(REPLICATOR_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(@) $^ 

.PHONY: clean

clean:
	rm -f $(OBJS_DIR)/*
	rm -f $(BIN_DIR)/*
	rm -f $(LIB_DIR)/*

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJS_DIR):
	mkdir -p $(OBJS_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)
