CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

BUILD_DIR = build
TARGET = vm
TEST_TARGET = build/test_vm
SRCS = src/main.c src/vm.c
OBJS = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c include/vm.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) asm/arkanoid.asm

$(TEST_TARGET): tests/test_vm.c src/vm.c include/vm.h
	@mkdir -p $(BUILD_DIR)
	$(CC) -Wall -Wextra -Werror -std=c11 -O2 -Iinclude tests/test_vm.c src/vm.c -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all run test clean
