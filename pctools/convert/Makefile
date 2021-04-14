OBJECTS := $(patsubst %.c,%.o,$(wildcard src/*.c))
CFLAGS += -I./inc

all: $(OBJECTS)
	gcc -o all $(OBJECTS) -lm -lpthread

.PHONY: clean
clean:
	@rm -rf src/*.o
	@rm -rf ./all