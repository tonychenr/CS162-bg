all: hw0
hw0: main.o wc.o map.o
	gcc -std=gnu99 main.o wc.o map.o -o hw0
main.o: main.c
	gcc -c -g -std=gnu99 main.c
wc.o: wc.c
	gcc -c -g -std=gnu99 wc.c
map.o: map.c
	gcc -c -g -std=gnu99 map.c
clean:
	rm -rf *.o main wc map