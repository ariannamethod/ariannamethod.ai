CC = cc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

.PHONY: all test clean

all: libaml.a

libaml.a: core/ariannamethod.o
	ar rcs $@ $^

core/ariannamethod.o: core/ariannamethod.c core/ariannamethod.h
	$(CC) $(CFLAGS) -c $< -o $@

test: core/test_aml
	./core/test_aml

core/test_aml: core/test_aml.c core/ariannamethod.c core/ariannamethod.h
	$(CC) $(CFLAGS) core/test_aml.c core/ariannamethod.c -o $@ $(LDFLAGS)

clean:
	rm -f core/*.o core/test_aml libaml.a
