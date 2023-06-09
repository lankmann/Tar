/* create.c adds files to a tar archive */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "functions.h"
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>


void int_to_octal(int input, char *result, size_t size) {
  /* Converts an integer into and octal string and pads the start with 0's */
  int i, len;
  char *octal;
  if (!(octal = malloc(size))) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  /* conversion */
  sprintf(octal, "%o", input);
  if ((len = strlen(octal)) > size-1) {
    fprintf(stderr, "Can't create header: Octal %s too large", octal);
    exit(EXIT_FAILURE);
  }
  /* padding */
  for (i = 0; i < size - len - 1; i ++)
    result[i] = '0';
  strcpy(result + i, octal);
  result[size - 1] = '\0';
  free(octal);
}

char *create_archive_header(char *file_path) {
  /* Writes the archive header for one file, returns pointer to header */
  char *header;
  char prefix[PREF_LEN];
  int header_index = 0;
  int i;
  /* allocating header */
  if (!(header = calloc(BLOCK_SIZE, sizeof(char *)))) {
    perror("calloc");
    exit(EXIT_FAILURE);
  }
  /** Write file name **/
  for (i = 0; i < PREF_LEN; i ++)
    prefix[i] = '\0';
  int path_len;
  if ((path_len = strlen(file_path)) >= PATH_LEN) {
      fprintf(stderr, "File path %s too long", file_path);
      exit(EXIT_FAILURE);
  }
  /* If `file_path` can't fit in name field (100 bytes), include the end of 
  `file_path` in name and write the rest in `prefix` */
  if (path_len > NAME_LEN) {
    i = path_len - NAME_LEN;
    /* Find the first '/' within 100 characters from the end */
    while (i < path_len && file_path[i++] != '/') {
      /* do nothing */
    }
    if (i < path_len-1) {
      /* `prefix = file_path[0:i]` */
      strncpy(prefix, file_path, i-1);
      if (i-1 < PREF_LEN)
        prefix[i-1] = '\0';
      /* `header_name_field = file_path[i:]` */
      strncpy(header, file_path + i, NAME_LEN);
    } else {
      fprintf(stderr, "Can't create header: File name %s too long", file_path);
      exit(EXIT_FAILURE);
    }
  } else {
    strcpy(header, file_path);
  }
  header_index += NAME_LEN;

  /** Write file mode **/
  struct stat st;
  char mode_octal[8];
  /* Get `file_path` stats */
  if (lstat(file_path, &st)) {
    perror("lstat");
    exit(EXIT_FAILURE);
  }
  /* Convert to 4 digit octal (mask to ensure it is 4 digits) */
  const int mode_mask = 07777;
  int_to_octal(st.st_mode & mode_mask, mode_octal, 8);
  strcpy(header + header_index, mode_octal);
  header_index += 8;

  /** Write UID and GID **/
  char uid[8], gid[8];
  int uid_len, gid_len;
  /* Convert integers st_uid/st_gid to octal strings */
  sprintf(uid, "%o", st.st_uid);
  sprintf(gid, "%o", st.st_gid);
  if ((uid_len = strlen(uid)) < 8) {
    /* Pad the start of uid with zeroes */
    for (i = 0; i < 7-uid_len; i ++)
      header[header_index + i] = '0';
    /* Add uid to header */
    strcpy(header + header_index + i, uid);
    header[header_index + i + uid_len] = '\0';
  } else {
    /* If uid_len > 7, compress it and add to header */
    insert_special_int(header + header_index, 8, st.st_uid);
  }
  header_index += 8;
  if ((gid_len = strlen(gid)) < 8) {
    /* Pad the start of gid with zeroes */
    for (i = 0; i < 7-gid_len; i ++)
      header[header_index + i] = '0';
    /* Add gid to header */
    strcpy(header + header_index + i, gid);
    header[header_index + i + gid_len] = '\0';
  } else {
      fprintf(stderr, "Can't create header: GID %s too long", gid);
      exit(EXIT_FAILURE);
  }
  header_index += 8;

  /** Write size **/
  char size[12];
  int_to_octal((S_ISREG(st.st_mode) ? st.st_size : 0), size, 12);
  strcpy(header + header_index, size);
  header_index += 12;

  /** Write mtime **/
  char mtime[12];
  int_to_octal(st.st_mtime, mtime, 12);
  strcpy(header + header_index, mtime);
  header_index += 12;

  /** Write typeflag **/
  /* skip chksum */
  header_index += 8;
  char type;
  if (S_ISDIR(st.st_mode)) 
    type = '5';
  else if (S_ISLNK(st.st_mode))
    type = '2';
  else
    type = '0';
  header[header_index] = type;
  header_index += 2;

  /** Write linkname **/
  header_index = 157;
  char readlink_buffer[NAME_LEN];
  ssize_t num_read = readlink(file_path, readlink_buffer, NAME_LEN);
  if (num_read > 0) {
    strcpy(header + header_index, readlink_buffer);
    header_index += num_read;
    if (num_read != NAME_LEN) {
      header[header_index] = '\0';
    }
  }

  /** Write magic **/
  header_index = 257;
  strcpy(header + header_index, "ustar");

  /** Write version **/
  header_index = 263;
  strcpy(header + header_index, "00");

  /** Write uname and gname **/
  header_index = 265;
  struct stat stat_buf;
  if (stat(file_path, &stat_buf) == -1) {
    perror("stat");
    exit(EXIT_FAILURE);
  }
  struct passwd *pw = getpwuid(stat_buf.st_uid);
  struct group *gr = getgrgid(stat_buf.st_gid);

  char *uname = pw->pw_name;
  char *gname = gr->gr_name;

  strncpy(header + header_index, uname, 32); 
  header_index = 297;
  strncpy(header + header_index, gname, 32);
  header_index = 329;

  /** Write prefix **/
  /* Location to write prefix: */
  header_index = 345;
  strcpy(header + header_index, prefix);

  /** Write chksum (should occur last) **/
  unsigned int sum = 0;
  char chksum[8];
  /* Location to write chksum: */
  header_index = 148;
  for (i = 0; i < BLOCK_SIZE; i ++) {
    sum += (unsigned char)header[i];
  }
  /* Add spaces (32) occupying the 8 chksum bytes (32 * 8) */
  sum += 256;
  int_to_octal(sum, chksum, 8);
  for (i = 0; i < 8; i ++) {
    header[header_index + i] = chksum[i];
  }
  return header;
}

void traverse_directory(char *path, int output_fd, bool verbose) {
  /* go through the provided path to add each 
   * directory/file to the tarfile */
  DIR *dir;
  struct dirent *dir_read;
  struct stat stat_buffer;

  /* opening the directory passed in */
  dir = opendir(path);
  if (dir == NULL) {
    perror("opendir");
    exit(EXIT_FAILURE);
  }
  while((dir_read = readdir(dir)) != NULL) {
    /* skipping over self and parent */
    if (strcmp(dir_read->d_name, ".") == 0 
    || strcmp(dir_read->d_name, "..") == 0) {
      continue;
    }

    /* getting the path of the current child */
    char new_path[PATH_LEN];
    strcpy(new_path, path);
    strcat(new_path, dir_read->d_name);

    if (lstat(new_path, &stat_buffer) < 0) {
      perror("lstat");
      exit(EXIT_FAILURE);
    }
    if (S_ISDIR(stat_buffer.st_mode))
      strcat(new_path, "/");

    /* dealing with the curr child */
    if (verbose)
      printf("%s\n", new_path);
    add_to_tarfile(new_path, output_fd);

    /* recurses with the new path */
    if (S_ISDIR(stat_buffer.st_mode)) {
      traverse_directory(new_path, output_fd, verbose);
    }
  }
  /* closing the directory */
  closedir(dir);
}

void add_to_tarfile(char *to_add, int output_fd) {
  /* writing the header, file contents, and padding */
  char *curr_header = create_archive_header(to_add);
  int read_result;
  int input_fd;
  char *buffer;
  char *ending_nulls;
  struct stat sb;
  int size;
  int is_empty_file = 0;
  int iterations = 0;
  int num_null_to_add;

  /* allocating memory for the buffer */
  if (!(buffer = malloc(BLOCK_SIZE))) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  /* write the header */
  write(output_fd, curr_header, BLOCK_SIZE);
  free(curr_header);

  if (lstat(to_add, &sb) < 0) {
    perror("lstat");
    exit(EXIT_FAILURE);
  }
  if (!(S_ISDIR(sb.st_mode))) {
    if (S_ISREG(sb.st_mode)) {
      /* write the file contents */
      input_fd = open(to_add, O_RDONLY);
      do {
        read_result = read(input_fd, buffer, BLOCK_SIZE);
        if (read_result < 0) {
          free(buffer);
          perror("read");
          exit(EXIT_FAILURE);
        }
        if ((read_result == 0) && (iterations == 0)) {
          is_empty_file = 1;
        }
        write(output_fd, buffer, read_result);
        iterations++;
      } while((read_result != 0) || (read_result == BLOCK_SIZE -1));
      close(input_fd);
    }
  
    size = sb.st_size;
    /* writing the ending null bytes */
    if (is_empty_file == 0) {
      num_null_to_add = BLOCK_SIZE - (size % BLOCK_SIZE);
      if (num_null_to_add != 0) {
        if (!(ending_nulls = calloc(num_null_to_add, 1))) {
          perror("calloc");
          exit(EXIT_FAILURE);
        }
        write(output_fd, ending_nulls, num_null_to_add);
        /* freeing the null byte buffer */
        free(ending_nulls);
      }
    }
  }
  /* freeing the buffer */
  free(buffer);
}
