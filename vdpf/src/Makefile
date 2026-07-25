TARGET = test
BENCH_TARGET = bench_vereval
CFLAGS = -O3
LDFLAGS = -lcrypto -lssl -lm

$(TARGET): test.o libdpf.a
	gcc $^ -o $@ $(LDFLAGS)

test.o: test.c ../include/dpf.h
	gcc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

$(BENCH_TARGET): bench_vereval.o libdpf.a
	gcc $^ -o $@ $(LDFLAGS)

bench_vereval.o: bench_vereval.c ../include/dpf.h ../include/mmo.h
	gcc $(CFLAGS) -std=gnu11 -c $< -o $@

libdpf.a: dpf.o vdpf.o mmo.o common.o sha256.o
	ar rcs $@ $^

dpf.o: dpf.c ../include/dpf.h
	gcc $(CFLAGS) -c -o $@ $< $(LDFLAGS)

mmo.o: mmo.c ../include/mmo.h 
	gcc $(CFLAGS) -c -o $@ $< $(LDFLAGS)

clean:
	rm -f *.o *.a $(TARGET) $(BENCH_TARGET)
