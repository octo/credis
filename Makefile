CFLAGS = -g -O2

# generic build macro
BUILD_C = $(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -o $@ $^

# targets to build with 'make all'
TARGETS = credis-test

all: $(TARGETS)

credis-test: credis-test.o credis.o
	$(BUILD_C)

clean:
	rm -f *.o *~ $(TARGETS)
