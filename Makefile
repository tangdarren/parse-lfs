CC = gcc
CFLAGS = -Wall -Wextra -Og -g

BIN_DIR = bin
LIB_DIR = lib
SOL_DIR = solution

all: $(BIN_DIR) bin/parse_lfs

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/%: $(SOL_DIR)/%.o $(LIB_DIR)/lib.o
	$(CC) $(CFLAGS) -o $@ $^

$(SOL_DIR)/%.o: $(SOL_DIR)/%.c $(SOL_DIR)/%.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB_DIR)/%.o: $(LIB_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -rf $(BIN_DIR) $(SOL_DIR)/*.o $(LIB_DIR)/*.o
