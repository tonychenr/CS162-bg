#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

int wc(FILE *readfile, int *counts) {
  int charcount = 0;
  int wordcount = 0;
  int linecount = 0;
  int curr = fgetc(readfile);
  int prev = '\n';
  while (1) {
    if (curr == EOF) {
      if (!isspace(prev) && prev != '\0') {
        wordcount++;
      }
      break;
    }
    if (isspace(curr) && !isspace(prev)) {
      wordcount++;
    }
    if (curr == '\n') {
      linecount++;
    }
    charcount++;
    prev = curr;
    curr = fgetc(readfile);
  }
  counts[0] = linecount;
  counts[1] = wordcount;
  counts[2] = charcount;
}

int main(int argc, char *argv[]) {
  int *counts = (int *)malloc(3*sizeof(int));
  if (argc == 1) {
    wc(stdin, counts);
    printf("%d %d %d\n", counts[0], counts[1], counts[2]);
  } else {
    for (int i = 1; i < argc; i++){
      FILE *input = fopen(argv[i], "r");
      wc(input, counts);
      printf("%d %d %d %s\n", counts[0], counts[1], counts[2], argv[i]);
      fclose(input);
    }
  }

  return 0;
}

