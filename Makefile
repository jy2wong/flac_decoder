CC=clang++
CFLAGS=-std=c++11 -g
.PHONY=all clean

all: flac_decoder

flac_decoder: FlacDecoder.o
	$(CC) $(CFLAGS) $< -o $@

FlacDecoder.o : FlacDecoder.cc FlacDecoder.h Flac.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -f flac_decoder FlacDecoder.o
