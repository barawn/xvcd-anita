#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>

#include "ftdi_xvc_core.h"

#define VENDOR 0x0403
#define PRODUCT 0x6010

unsigned int verbosity;

#define DEBUGCOND(lvl) (lvl<=verbosity)
#define DEBUG(lvl,...) if (lvl<=verbosity) printf(__VA_ARGS__)
#define DEBUGPRINTF(...) printf(__VA_ARGS__)

int handle_data(int fd);
int sread(int fd, void *target, int len);
int tisc_command_byte(unsigned char cb);

void sigterm(int signo) {
  (void) signo;
}

int main(int argc, char **argv) {

  int i;
  // Socket.
  int s;
  int c;
  int nconns = 0;
  struct sockaddr_in address;
  // Command byte.
  unsigned char cb;
  int do_cb = 0;
  // Signal handling.
  struct sigaction sa;
  sigset_t sigset, oldset;

  sa.sa_handler = sigterm;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  while ((c = getopt(argc, argv, "v:x:s:t:")) != -1) 
     switch(c) 
       {
	case 'v': verbosity = strtoul(optarg, NULL, 0); break;
	case 'x': cb = strtoul(optarg, NULL, 0); do_cb = 1; break;
	case 's': printf("Not implemented yet (-s)\n"); return 1;
	case 't': printf("Not implemented yet (-t)\n"); return 1;
	case '?': if (optopt == 'x') 
	    {
	       printf("Option -%c requires an argument.\n", optopt);
	    } else if (optopt == 'v') 
		 {
		    verbosity++;
		 }
	  else
	    {
	       printf("usage: %s [-v] [-x command_byte] [-s serial-no] [-t type]\n", *argv);
	    }
	  return 1;
       }	  
  ftdi_xvc_init(verbosity);
  
  if (ftdi_xvc_open_device(VENDOR, PRODUCT) < 0) {
    return 1;
  }
  if (do_cb) tisc_command_byte(cb);
  
  if (ftdi_xvc_init_mpsse() < 0) 
    return 1;
	
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s<0) {
    perror("socket");
    return 1;
  }
  i = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(2542);
  address.sin_family = AF_INET;
  if (bind(s, (struct sockaddr*)&address, sizeof(address)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(s, 0) < 0) {
    perror("listen");
    return 1;
  }
  fd_set conn;
  int maxfd = 0;
  FD_ZERO(&conn);
  FD_SET(s, &conn);
  maxfd = s;
   DEBUG(0, "xvcd: running...\n");
   while (1) {
    fd_set read = conn, except = conn;
    int fd;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGQUIT);
    sigprocmask(SIG_BLOCK, &sigset, &oldset);
  
    if (pselect(maxfd+1, &read, 0, &except, 0, &oldset) < 0) {
      perror("pselect");
      break;
    }
    for (fd = 0; fd <= maxfd; ++fd) {
      if (FD_ISSET(fd, &read)) {
	if (fd == s) {
	  // Connection attempt.
	  DEBUG(1, "xvcd: accepting connection.\n");
	  int newfd;
	  socklen_t nsize = sizeof(address);
	  newfd = accept(s, (struct sockaddr *)&address, &nsize);
	  if (newfd < 0) {
	    perror("accept");
	  } else {
	    DEBUG(1, "xvcd: connection accepted.\n");
	    if (!nconns) {
	       DEBUG(1, "xvcd: first connection.\n");
	    }
	    nconns++;
	    if (newfd > maxfd) maxfd = newfd;
	    FD_SET(newfd, &conn);
	  }
	} else if (handle_data(fd)) {
	  close(fd);
	  DEBUG(1, "xvcd: connection closed.\n");
	  FD_CLR(fd, &conn);
	  if (nconns) nconns--;
	  if (!nconns) {
	    DEBUG(1, "xvcd: last connection.\n");
	  }
	}
      } else if (FD_ISSET(fd, &except)) {
	close(fd);
	DEBUG(1, "xvcd: connection aborted.\n");
	FD_CLR(fd, &conn);
	if (nconns) nconns--;
	if (!nconns) {
	  DEBUG(1, "xvcd: last connection.\n");
	}
	if (fd == s) break;
      }
    }
  }

  DEBUG(0, "xvcd: exiting.\n");
  close(s);
  ftdi_xvc_close_device();
 
  return 0;
}

/** \brief Utility function for reading from a socket */
int sread(int fd, void *target, int len)
{
        unsigned char *t = target;
        while (len)
        {
                int r = read(fd, t, len);
                if (r <= 0)
                        return r;
                t += r;
                len -= r;
        }
        return 1;
}

int handle_data(int fd) {
  int i;
  char cmd[7];
  int len;
  unsigned char *buffer;
  unsigned char *result;
  int nr_bytes;
  do {
    if (sread(fd, cmd, 6) != 1) return 1;
    cmd[6] = 0;
    if (memcmp(cmd, "shift:", 6)) 
       {
	  fprintf(stderr, "xvcd: %s : unknown command '%s'\n", __FUNCTION__, cmd);
	  return 1;
       }     
    if (sread(fd, &len, 4) != 1) return 1;
    nr_bytes = (len + 7)/8;
    buffer = malloc(sizeof(unsigned char)*nr_bytes*2);
    result = malloc(sizeof(unsigned char)*nr_bytes);
    if (!buffer || !result) return 1;
    if (sread(fd, buffer, nr_bytes * 2) != 1) return 1;
    if (DEBUGCOND(3)) {
       DEBUGPRINTF("xvcd: %s : TMS data: ", __FUNCTION__);
       for (i=0;i<nr_bytes;i++) {
	  DEBUGPRINTF("%2.2x", buffer[i]);
       }
       DEBUGPRINTF("\n");
       DEBUGPRINTF("xvcd: %s : TDI data: ", __FUNCTION__);
       for (i=nr_bytes;i<nr_bytes*2;i++) {
	  DEBUGPRINTF("%2.2x", buffer[i]);
       }
       DEBUGPRINTF("\n");
    }
    if (len == 12) 
       {
	  if (buffer[2] == 0x00 && buffer[3] == 0x0d) 
	    {
	       DEBUGPRINTF("xvcd: %s : TEMP fix bogus iMPACT instruction register\n", __FUNCTION__);
	       buffer[2] = 0xFF;
	       buffer[3] = 0x0F;
	    }	  
       }
    if (ftdi_xvc_shift_command(len, buffer, result)) return 1;
    if (write(fd,result, nr_bytes) != nr_bytes) {
      perror("write");
      return 1;
    }   
     
    free(buffer);
    free(result);     
  } while(0); // This condition should be changed to make sure we only exit when in T-L-R.
  return 0;
}

int tisc_command_byte(unsigned char cb) {
  struct ftdi_context *ctx;
  unsigned char byte;
  int i;
   
  DEBUG(1, "xvcd: %s : setting command byte to %2.2x\n", __FUNCTION__, cb);
  ctx = ftdi_xvc_get_context();
       
  ftdi_set_bitmode(ctx, 0x0B, BITMODE_BITBANG);
  byte = 0x00;
  if (ftdi_write_data(ctx, &byte, 1) != 1) return -1;
  ftdi_set_bitmode(ctx, 0x3B, BITMODE_BITBANG);
  if (ftdi_write_data(ctx, &byte, 1) != 1) return -1;
   // The byte needs to be clocked out MSB first.
   for (i=0;i<8;i++) 
     {
	if (cb & 0x80) byte = 0x20;
	else byte = 0x00;
	if (ftdi_write_data(ctx, &byte, 1) != 1) return -1;
	byte |= 0x10;
	if (ftdi_write_data(ctx, &byte, 1) != 1) return -1;
	cb = cb << 1;
     }
  ftdi_set_bitmode(ctx, 0x0B, BITMODE_BITBANG);
  return 0;
}

