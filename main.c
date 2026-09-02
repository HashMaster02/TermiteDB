#include <stdio.h>

// PART 1
// DONE: Append key-value commandline appends to a file
// TODO: Maintain hashmap to byte-offset of the latest entry of a given
// key-value pair
// TODO: Read latest entry out of file

int main(int argc, char *argv[]) {

  if (argc <= 1) {
    return 0;
  }

  FILE *fileptr;
  fileptr = fopen("segment.txt", "a");
  for (int i = 1; i < argc; i++) {
    // Cmdline Formatting => termite key:value OR termite "key: value"
    fputs(argv[i], fileptr);
    fputc('\n', fileptr);
  }

  return 0;
}
