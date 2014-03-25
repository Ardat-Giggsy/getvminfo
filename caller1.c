/*
 *COMP 790-042
 *Assignment 4
 *Rui Liu
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "getvminfo.h" /* used by both kernel module and user program */

int fp;
char the_file[256] = "/sys/kernel/debug/";
char call_buf[MAX_CALL];  /* assumes no bufferline is longer */
char resp_buf[MAX_RESP];

void do_syscall(char *call_string);

void main (int argc, char* argv[])
{
  unsigned long long i = 0;
  int rc = 0;
  char *addr;
  int fd, length;
  struct stat sb;
  char *fullname; 
  char c;
  int gen;
  
  /*Too many or too few command line arguments*/
  if (argc != 3) {
    exit(-1);
  }

  fullname = (char *)malloc(sizeof(char));
  strcpy(fullname, "getvminfo ");
  strcat(fullname, argv[2]);

  /* Open the file */
  
  strcat(the_file, dir_name);
  strcat(the_file, "/");
  strcat(the_file, file_name);

  if ((fp = open (the_file, O_RDWR)) == -1) {
      fprintf (stderr, "error opening %s\n", the_file);
      exit (-1);
  }
  
  /*Try Read Only Open for the file specified*/
  fd = open(argv[1], O_RDONLY);

  //File Opening Failed
  if (fd == -1)
  {
    exit(-1);
  } 

  if (fstat(fd, &sb) == -1)
  {
    exit(-1);
  }
  
  addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  
  if (addr == MAP_FAILED)
  {
    exit(-1);
  }

  do_syscall(fullname);

  fprintf(stdout, "Module getvminfo returns %s", resp_buf);

  /*Now addr is the start address of the mapping*/
  /*Now write to stdio*/
  
  fprintf(stdout, "The start address of the mapping is: %lu\n", (unsigned long) addr);
  
  //Random Read...
   
  srand(time(NULL));
  for(i = 0; i < sb.st_size; i++)
  {
    gen = rand() % sb.st_size;
    //fprintf(stdout, "%d\n", gen);
    c = addr[gen];
  }

  close(fp);
} /* end main() */

void do_syscall(char *call_string)
{
  int rc;

  strcpy(call_buf, call_string);

  rc = write(fp, call_buf, strlen(call_buf) + 1);
  if (rc == -1) {
     fprintf (stderr, "error writing %s\n", the_file);
     fflush(stderr);
     exit (-1);
  }

  rc = read(fp, resp_buf, sizeof(resp_buf));
  if (rc == -1) {
     fprintf (stderr, "error reading %s\n", the_file);
     fflush(stderr);
     exit (-1);
  }
}

