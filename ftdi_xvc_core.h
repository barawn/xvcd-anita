#ifndef FTDI_XVC_CORE_H
#define FTDI_XVC_CORE_H

#include <ftdi.h>

void ftdi_xvc_init(unsigned int verbosity);

void ftdi_xvc_close_device();

struct ftdi_context *ftdi_xvc_get_context();

int ftdi_xvc_init_mpsse();

int ftdi_xvc_shift_command(unsigned int len,
			   unsigned char *buffer,
			   unsigned char *result);

int ftdi_xvc_open_device(int vendor, int product);

#endif // FTDI_XVC_CORE_H