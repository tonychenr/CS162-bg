all: main wc map
main: main.c
	gcc -g -o main -std=gnu99 main.c
wc: wc.c
	gcc -g -o wc -std=gnu99 wc.c
map: map.c
	gcc -g -o map -std=gnu99 map.c
clean:
	rm -rf *.o main wc map