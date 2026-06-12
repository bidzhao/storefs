CC      = gcc
CFLAGS  = -Wall -O2 -I/usr/include/infiniband -I/usr/include
LDFLAGS = -libverbs -lcurl -lcrypto -lz

COMMON_OBJS = rdmalib.o s3client.o

all: s3rdmaget s3rdmaput

s3rdmaget: s3rdmaget.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

s3rdmaput: s3rdmaput.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f s3rdmaget s3rdmaput *.o

.PHONY: all clean
