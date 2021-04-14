OBJECTS := $(patsubst %.c,%.o,$(wildcard src/*.c))
CFLAGS += -I./inc
CFLAGS += -std=gnu99

all: $(OBJECTS)
	gcc -o all $(OBJECTS) $(LDFLAGS) -lm -lpthread -ldl -lrt -lcurl -lcrypto

.PHONY: clean
clean:
	@rm -rf src/*.o
	@rm -rf ./all
