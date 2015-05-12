// Basic XVC core for an FTDI device in MPSSE mode, using
// the libftdi library under Linux. I've tested this under Linux:
// I guess it could work under Windows as well, but since XVC
// is network based, I never saw the point.

// Thanks to tmbinc for the original xvcd implementation:
// this code however is (I'm pretty sure) a total rewrite of the
// physical layer code.

// Author: P.S. Allison (allison.122@osu.edu)
// This code is in the public domain (CC0):
// see https://wiki.creativecommons.org/CC0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <ftdi.h>
#include <usb.h>

unsigned int ftdi_verbosity;
#define DEBUGCOND(lvl) (lvl<=ftdi_verbosity)
#define DEBUG(lvl,...) if (lvl<=ftdi_verbosity) printf(__VA_ARGS__)
#define DEBUGPRINTF(...) printf(__VA_ARGS__)

struct ftdi_context ftdi;

/** \brief Send a TMS-shifting command to the FTDI device. */
int ftdi_xvc_tms_command(unsigned char len, unsigned char tms) {
  unsigned char buf[3];
  buf[0] = MPSSE_WRITE_TMS|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
  buf[1] = len-1;
  buf[2] = tms;
  if (ftdi_write_data(&ftdi, buf, 3) != 3) {
    return 1;
  }
  return 0;
}

/** \brief Read bytes from the FTDI device, possibly in multiple chunks. */
void ftdi_xvc_read_bytes(unsigned int len, unsigned char *buf) {
  int read, to_read, last_read;
  to_read = len;
  read = 0;
  last_read = ftdi_read_data(&ftdi, buf, to_read);
  if (last_read > 0) read += last_read;
  while (read < to_read) {
    last_read = ftdi_read_data(&ftdi, buf+read, to_read-read);
    if (last_read > 0) read += last_read;
  }
}

/** \brief Send a TDI-shifting command to the FTDI device, optionally with 1 final TMS movement (to exit Shift-IR/DR). */
int ftdi_xvc_tdi_command(unsigned int len,
			 unsigned char *inbuf,
			 unsigned char *outbuf,
			 unsigned char last_tms) {
  unsigned char *cmd;
  unsigned char *p;
  unsigned char *res;
  unsigned int nbytes = len/8;
  unsigned char nbits = len % 8;
  unsigned int cmd_len;
  unsigned int rd_len;
  // Build the shift command.
  // 
  // For that, we need to:
  // 1) determine how many bits we can send via a TDI commmand. If last_tms is 1,
  //    we can only send "len-1" bits that way.
  // 2) determine how many of those bits can be sent as bytes.
  // 2) determine how many remaining bits need to be sent.
  // 
  // Note that to *enter* any shift state, we need
  // TMS = 0. So TMS = 0 here already.

  // If 'last_tms' is high, we're truncating the entire thing by 1.
  // The last bit will be clocked in/out via a TMS command.
  if (last_tms) len = len - 1;
  // Number of bits that can be sent as bytes. 
  nbytes = len/8;
  // Number of bits that have to be sent as bits.
  nbits = len % 8;
  // Total length of the command.
  cmd_len = 0;
  
  // If we have any bytes, add that command length.
  if (nbytes) cmd_len = 3 + nbytes;
  // If we have any leftover bits, add that command length.
  if (nbits) cmd_len += 3;
  // If we have to send one as a TMS command, add that command length.
  if (last_tms) cmd_len += 3;
  
  // Total number of bytes to read back. 
  rd_len = nbytes;
  // If we have to send leftover bits, add that response.
  if (nbits) rd_len += 1;
  // If we have to send one as a TMS command, add that response.
  if (last_tms) rd_len += 1;
   
  // Allocate buffers for command and response.
  cmd = malloc(cmd_len*sizeof(unsigned char));
  if (!cmd) return 1;
  res = malloc(rd_len*sizeof(unsigned char));
  if (!res) return 1;

  // Initialize pointer to proper position.
  p = cmd;
  DEBUG(4, "xvcd: %s : shifting %d bits.\n", __FUNCTION__, len);
  // Build byte shift command.
  if (nbytes) {
    DEBUG(4, "xvcd: %s : shift command of %d bytes\n", __FUNCTION__, nbytes);
    *p++ = MPSSE_DO_READ|MPSSE_DO_WRITE|MPSSE_LSB|MPSSE_WRITE_NEG;
    *p++ = (nbytes-1) & 0xFF;
    *p++ = ((nbytes-1) & 0xFF00)>>8;
    memcpy(p, inbuf, nbytes);
    p += nbytes;
  }
  // Build bit shift command.
  if (nbits) {
    DEBUG(4, "xvcd: %s : shift command of %d bits\n", __FUNCTION__, nbits);
    *p++ = MPSSE_DO_READ|MPSSE_DO_WRITE|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
    *p++ = nbits - 1;
    *p++ = inbuf[nbytes] & ~(1<<nbits);
  }
  // Build TMS command.
  if (last_tms) {
    DEBUG(4, "xvcd: %s : shifting final bit (%d) via TMS command.\n", __FUNCTION__, inbuf[nbytes] & (1<<nbits));
    // The last bit is at inbuf[nbytes] & (1<<nbits);
    *p++ = MPSSE_WRITE_TMS|MPSSE_DO_READ|MPSSE_LSB|MPSSE_BITMODE|MPSSE_WRITE_NEG;
    *p++ = 0;
    if (inbuf[nbytes] & (1<<nbits)) *p++ = 0x81;
    else *p++ = 0x01;
  }
  // Send off all commands.
  if (ftdi_write_data(&ftdi, cmd, cmd_len) != cmd_len) {
    return 1;
  }
  free(cmd);
  // Read response.
  ftdi_xvc_read_bytes(rd_len, res);
  DEBUG(4, "xvcd: %s : read %d bytes.\n", __FUNCTION__, rd_len);
  // Copy the normally-shifted bits.
  memcpy(outbuf, res, nbytes + (nbits != 0));
  // Downshift the last bits to align to bit 0.
  if (nbits) 
     {
	outbuf[nbytes] = outbuf[nbytes] >> (8-nbits);
     }
  // Add the bit shifted from the TMS command in proper spot.
  if (last_tms) 
     {
	int i;
	if (DEBUGCOND(5)) 
	  {
	     DEBUGPRINTF("xvcd: %s : read back ", __FUNCTION__);
	     for (i=0;i<rd_len;i++) DEBUGPRINTF("%2.2x", res[i]);
	     DEBUGPRINTF("\n");
	  }	
	// This is actually a strong debugging condition, due to the way
	// the MPSSE works.
	// The TMS read byte should just be the *previous* read byte,
	// downshifted by 1, with the new bit that's read out at the top.
	// If it's *not*, that typically means that we lost synchronization
	// in the MPSSE stream (probably an invalid command received).
	DEBUG(4, "xvcd: %s : TMS-shifted read byte is %2.2x (prev %2.2x)\n",
	      __FUNCTION__,
	      res[rd_len-1],
	      res[rd_len-2]);
	// Set the appropriate bit in the stream.
	// Remember the bit to test is the TOP bit, since
	// we're shifting in LSB first.
	if (res[rd_len-1] & 0x80) outbuf[nbytes] |= (1<<nbits);
     }
  // And we're done.
  return 0;
}

/** \brief Close the FTDI device. */
void ftdi_xvc_close_device() {
  ftdi_usb_reset(&ftdi);
  ftdi_usb_close(&ftdi);
  ftdi_deinit(&ftdi);
}

/** \brief Initialize the FTDI library. */
void ftdi_xvc_init(unsigned int verbosity) 
{
   ftdi_init(&ftdi);
   ftdi_verbosity = verbosity;
}

/** \brief Open the FTDI device. */
int ftdi_xvc_open_device(int vendor, int product) 
{
   if (ftdi_usb_open_desc(&ftdi, vendor, product, NULL, NULL) < 0) 
     {
	fprintf(stderr, "xvcd: %s : can't open device.\n", __FUNCTION__);
	return -1;
     }
   ftdi_usb_reset(&ftdi);
   ftdi_set_interface(&ftdi, INTERFACE_A);
   ftdi_set_latency_timer(&ftdi, 1);
   return 0;
}

/** \brief Fetch the FTDI context, in case someone else wants to muck with the device before we do. */
struct ftdi_context *ftdi_xvc_get_context() 
{
   return &ftdi;
}

/** \brief Initialize the MPSSE engine on the FTDI device. */
int ftdi_xvc_init_mpsse() {
   int res;
   unsigned char byte;
   
   unsigned char buf[7] = {
    SET_BITS_LOW, 0x08, 0x0B,  // Set TMS high, TCK/TDI/TMS as outputs.
    TCK_DIVISOR, 0x01, 0x00,   // Set TCK clock rate = 6 MHz.
    SEND_IMMEDIATE
  };   
  ftdi_set_bitmode(&ftdi, 0x0B, BITMODE_BITBANG);
  ftdi_set_bitmode(&ftdi, 0x0B, BITMODE_MPSSE);
  while (res = ftdi_read_data(&ftdi, &byte, 1));
  if (ftdi_write_data(&ftdi, buf, 7) != 7) 
     {
	fprintf(stderr, "xvcd: %s : FTDI initialization failed.\n", __FUNCTION__);
	return -1;
     }
   return 0;
}

/** \brief Handle a 'shift:' command sent via XVC. */
int ftdi_xvc_shift_command(unsigned int len,
			   unsigned char *buffer,
			   unsigned char *result) 
{
   int i;
   int nr_bytes;
   
   nr_bytes = (len+7)/8;
    // ChipScope and iMPACT are obviously very predictable in
    // how they manipulate the JTAG chain, so we actually just
    // parse what they want.

    // Anything 5 or less is pure JTAG state machine movement.
    // For that, we ignore TDO, and they can all be batched.
    if (len < 6) {
      if (DEBUGCOND(2)) 
	 {
	    DEBUGPRINTF("xvcd: %s : JTAG state movement:", __FUNCTION__);
	    for (i=0;i<len;i++) 
	      {
		 if (buffer[0] & (1<<i)) DEBUGPRINTF(" 1");
		 else DEBUGPRINTF(" 0");
	      }
	    DEBUGPRINTF(" (TMS: %2.2x TDI: %2.2x)\n", buffer[0], buffer[1]);
	 }
      if (ftdi_xvc_tms_command(len, buffer[0])) {
	return 1;
      }
      result[0] = 0;
    }
    // Anything *more* than 5 is a shift input or output, and
    // may conclude with a TMS movement. Therefore we may
    // have to split it up into a "clock data in and out"
    // (with 1 less bit), and then follow it with a "clock
    // data to TMS pin with read" with the last bit value.
    
    // Sadly we don't have a way of knowing whether or not we need
    // the data... so therefore, we need it.
    else {
      int tms_movement;
      unsigned char last_tms;
      unsigned char last_bit;
      DEBUG(2, "xvcd: %s : instruction/data shift: %d bits.\n", __FUNCTION__, len);
      tms_movement = 0;
      for (i=0;i<nr_bytes-1;i++) 
	 if (buffer[i] != 0x00) tms_movement = 1;
      last_tms = buffer[nr_bytes-1];
      last_bit = (len-1) % 8;
      if (last_tms & ~(1<<last_bit)) 
	 {
	    tms_movement = 1;
	 }
      // TMS movement anywhere else other than the last bit is illegal.
      if (tms_movement == 1) 
	 {
	    fprintf(stderr,"xvcd: %s : TMS movement inside data shift, don't know how to handle.\n", __FUNCTION__);
	    fprintf(stderr,"xvcd: %s : offending data:\n", __FUNCTION__);
	    fprintf(stderr,"xvcd: %s : TMS: ", __FUNCTION__);
	    for (i=0;i<nr_bytes;i++) fprintf(stderr, "%2.2x", buffer[i]);
	    fprintf(stderr, "\nxvcd: %s : TDI: ", __FUNCTION__);
	    for (i=nr_bytes;i<nr_bytes*2;i++) fprintf(stderr, "%2.2x", buffer[i]);
	    fprintf(stderr, "\n");	    
	    return 1;
	 }
      // Now check the very last bit. TMS movement here is legal.
      if ((last_tms >> last_bit) & 0x1) tms_movement = 1;
       
      if (ftdi_xvc_tdi_command(len, buffer+nr_bytes, result, tms_movement))
	return 1;
      if (DEBUGCOND(3)) {
	 DEBUGPRINTF("xvcd: %s : TDO data: ", __FUNCTION__);
	 for (i=0;i<nr_bytes;i++) {
	    DEBUGPRINTF("%2.2x", result[i]);
	 }
	 DEBUGPRINTF("\n");
      }       
    }     
   return 0;
}
