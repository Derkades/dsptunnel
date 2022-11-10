BINARY = dsptunnel
CFLAGS = -Wall -Wextra -O3
LDFLAGS = -lpthread

.PHONY: all
all: $(BINARY)


dsptunnel: dsptunnel.o tun.o dsp.o input.o output.o fletcher.o parity.o


dsp.o: dsp.c
dsptunnel.o: dsptunnel.c dsptunnel.h tun.h dsp.h input.h output.h parity.h
fletcher.o: fletcher.c
input.o: input.c dsptunnel.h fletcher.h input.h parity.h
output.o: output.c dsptunnel.h fletcher.h output.h parity.h
tun.o: tun.c
parity.o: parity.c


.PHONY: clean
clean:
	rm -f $(BINARY) *.o

