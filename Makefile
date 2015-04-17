LDLIBS=-lusb -lftdi

all:		xvcd-anita
xvcd-anita:	xvcd-anita.o ftdi_xvc_core.o
