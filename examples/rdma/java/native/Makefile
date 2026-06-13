CC      = gcc
CFLAGS  = -O2 -fPIC -Wall -I/usr/include/infiniband -I/usr/include
LDFLAGS = -shared -libverbs

TARGET  = librdmaverbs.so

all: $(TARGET)

$(TARGET): verbs_shim.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
