#include <sys/stat.h>

struct header {
  struct stat stat;
  char name[100];
  int chksum;
  char typeflag;
  char linkname[100];
  char uname[32];
  char gname[32];
  char prefix[155];
};

#define BLOCK_LEN 512

long int octal_to_int(char *input, size_t size);
void read_archive_header(char *header, struct header *info);