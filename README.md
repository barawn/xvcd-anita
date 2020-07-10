# xvcd-anita

xvcd-anita is a Xilinx Virtual Cable daemon for ANITA devices with an
FT2232 connected to the JTAG port via interface A.

The base daemon is in "xvcd-anita.c", and has code for programming
the control register on the scan-chain linker PLD on multi-FPGA boards.
Other than that it is a pretty generic XVC implementation, though
it doesn't have the version check stuff that Vivado needs (yet!).

ftdi_xvc_core is the underlying 'physical layer' portion of the XVC
implementation. Note that it uses MPSSE, rather than the synchronous
bit-bang used before, so it is ridiculously faster. That part is not
ANITA-specific and can be reused pretty easily.

Seriously, it's actually faster than the Xilinx programming cable
directly. Significantly.

BUILDING:

make

You need libftdi and libusb.

NOTES:

1) Multi-FPGA chains DO NOT WORK with iMPACT over XVC. NOT AT ALL.
   This is not xvcd-anita's fault, this is iMPACT's fault.
   I may eventually implement a workaround which detects IR shifts
   other than 'expected' ones, but this might be hard.
   
2) There may still be a bug lurking somewhere, since after programming
   in ChipScope, the status register shows bizarreness. I say 'may'
   because this might be ChipScope's fault, like iMPACT.
   
3) This code doesn't work with Vivado because it has to work around
   dumb bugs in iMPACT/ChipScope. Other people have builds that work
   with Vivado, use theirs (but then it won't work back with ChipScope
   or iMPACT again). Sadly, there's no way for the XVC server to tell
   whether ChipScope or iMPACT is connecting to it. And of course, for
   those who would not that ISE is a billion years old at this point,
   of course you're right, but Vivado doesn't support Spartan-6s,
   and the device this was built for uses a mix of Artix-7 and Spartan-6s.

STILL TO DO:

1) Implement JTAG tracking to allow 'sharing' so long as the JTAG TAP
   state is Test-Logic-Reset.

2) Look into workaround for detecting iMPACT's bogus IR-shift. Sadly,
   this might be pretty hard because we will have to know both the
   IR length for all devices PLUS *all* valid instructions!

   The 'best' trick here would actually be to have a hack which
   virtually slices the JTAG chain up and presents only one device
   back to iMPACT. That's a *lot* of work, however!

The iMPACT bug in question is here:

https://forums.xilinx.com/t5/Vivado-Debug-and-Power/iMPACT-XVC-broken-with-multiple-devices/td-p/496232

USAGE:

xvcd-anita [-x #] [-v [#]]

-x # sets the CPLD control register to #. This number is parsed with strtoul()
with auto-base checking: so prefix with "0x" if you send it in hex.

-v # sets the verbosity to #. # is optional: without it "-v" increments
the verbosity so you can do silliness like "-vvvv". Verbosity goes up to 6.

USAGE NOTES!!

1) Make sure you have permissions to access the FTDI device.
2) Make sure ftdi_sio is not loaded (/sbin/rmmod ftdi_sio).

Licensing:

"ftdi_xvc_core.c" is licensed CC0 (https://wiki.creativecommons.org/CC0). The remaining code is specific to the ANITA project, and has rights reserved.
