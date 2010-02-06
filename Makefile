CFLAGS = -g -O2
LDFLAGS =
#CPPFLAGS = -DPRINTDEBUG

# targets to build with 'make all'
TARGETS = credis-test libcredis.a libcredis.so

all: $(TARGETS)

credis-test: credis-test.o libcredis.a
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -o $@ $^

libcredis.a: credis.o
	$(AR) -cvq $@ $^

libcredis.so: credis.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^

credis.o: credis.c credis.h Makefile
	$(CC) -c -fPIC $(CFLAGS) $(CPPFLAGS) -o $@ credis.c

install:
	@echo "Installing library (to be done)"
clean:
	rm -f *.o *~ $(TARGETS)
