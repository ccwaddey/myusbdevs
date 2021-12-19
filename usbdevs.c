/*	$OpenBSD: usbdevs.c,v 1.33 2019/12/22 14:02:38 semarie Exp $	*/
/*	$NetBSD: usbdevs.c,v 1.19 2002/02/21 00:34:31 christos Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (augustss@netbsd.org).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <dev/usb/usb.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <vis.h>
#include <string.h>
#include <unistd.h>

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

#define MINIMUM(a, b) (((a) < (b)) ? (a) : (b))

#define USBDEV "/dev/usb"
#define USB_MAX_CONFIGS 255

#define COMM_INFO 0
#define COMM_STAT 1
#define COMM_DDSC 2
#define COMM_CDSC 3
#define COMM_FDSC 4

#define C_DESC 2
#define S_DESC 3
#define I_DESC 4
#define E_DESC 5

int verbose = 0;
char done[USB_MAX_DEVICES];

void usage(void);
void dump_device(int, uint8_t);
void dump_config(char *, int, uint8_t, int);
void dump_controller(char *, int, uint8_t);
void full_dump(char *, int, uint8_t, int);
uint16_t print_config(int, uint8_t, int, bool);
void print_device(int, uint8_t);
void dump_device_desc(char *, int, uint8_t);
int main(int, char **);

extern char *__progname;

void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] [-a addr] [-d usbdev]\n", __progname);
	exit(1);
}

void
dump_device(int fd, uint8_t addr)
{
	struct usb_device_info di;
	int i;
	char vv[sizeof(di.udi_vendor)*4], vp[sizeof(di.udi_product)*4];
	char vr[sizeof(di.udi_release)*4], vs[sizeof(di.udi_serial)*4];

	di.udi_addr = addr;
	if (ioctl(fd, USB_DEVICEINFO, &di) == -1) {
		if (errno != ENXIO)
			warn("addr %u", addr);
		return;
	}

	done[addr] = 1;

	strvis(vv, di.udi_vendor, VIS_CSTYLE);
	strvis(vp, di.udi_product, VIS_CSTYLE);
	printf("addr %02u: %04x:%04x %s, %s, usb_bus: %hhu",
	    addr, UGETW(&di.udi_vendorNo), UGETW(&di.udi_productNo),
	    vv, vp, di.udi_bus);

	if (verbose) {
		printf("\n\t ");
		switch (di.udi_speed) {
		case USB_SPEED_LOW:
			printf("low speed");
			break;
		case USB_SPEED_FULL:
			printf("full speed");
			break;
		case USB_SPEED_HIGH:
			printf("high speed");
			break;
		case USB_SPEED_SUPER:
			printf("super speed");
			break;
		default:
			break;
		}

		if (di.udi_power)
			printf(", power %d mA", UGETDW(&di.udi_power));
		else
			printf(", self powered");

		if (di.udi_config)
			printf(", config %d", di.udi_config);
		else
			printf(", unconfigured");

		strvis(vr, di.udi_release, VIS_CSTYLE);
		printf(", rev %s (0x%hx)", vr, UGETW(&di.udi_releaseNo));

		printf("\n\t class: %hhu, subclass: %hhu, protocol: %hhu",
		    di.udi_class, di.udi_subclass, di.udi_protocol);

		if (di.udi_serial[0] != '\0') {
			strvis(vs, di.udi_serial, VIS_CSTYLE);
			printf(", iSerial %s", vs);
		}
	}
	printf("\n");

	if (verbose)
		for (i = 0; i < USB_MAX_DEVNAMES; i++)
			if (di.udi_devnames[i][0] != '\0')
				printf("\t driver: %s\n", di.udi_devnames[i]);

	if (verbose > 1) {
		int port, nports;

		nports = MINIMUM(UGETDW(&di.udi_nports), nitems(di.udi_ports));
		for (port = 0; port < nports; port++) {
			uint16_t status, change;

			status = UGETDW(&di.udi_ports[port]) & 0xffff;
			change = UGETDW(&di.udi_ports[port]) >> 16;

			printf("\t port %02u: %04x.%04x", port+1, change,
			    status);

			if (status & UPS_CURRENT_CONNECT_STATUS)
				printf(" connect");

			if (status & UPS_PORT_ENABLED)
				printf(" enabled");

			if (status & UPS_SUSPEND)
				printf(" supsend");

			if (status & UPS_OVERCURRENT_INDICATOR)
				printf(" overcurrent");

			if (di.udi_speed < USB_SPEED_SUPER) {
				if (status & UPS_PORT_L1)
					printf(" l1");

				if (status & UPS_PORT_POWER)
					printf(" power");
			} else {
				if (status & UPS_PORT_POWER_SS)
					printf(" power");

				switch (UPS_PORT_LS_GET(status)) {
				case UPS_PORT_LS_U0:
					printf(" U0");
					break;
				case UPS_PORT_LS_U1:
					printf(" U1");
					break;
				case UPS_PORT_LS_U2:
					printf(" U2");
					break;
				case UPS_PORT_LS_U3:
					printf(" U3");
					break;
				case UPS_PORT_LS_SS_DISABLED:
					printf(" SS.disabled");
					break;
				case UPS_PORT_LS_RX_DETECT:
					printf(" Rx.detect");
					break;
				case UPS_PORT_LS_SS_INACTIVE:
					printf(" ss.inactive");
					break;
				case UPS_PORT_LS_POLLING:
					printf(" polling");
					break;
				case UPS_PORT_LS_RECOVERY:
					printf(" recovery");
					break;
				case UPS_PORT_LS_HOT_RESET:
					printf(" hot.reset");
					break;
				case UPS_PORT_LS_COMP_MOD:
					printf(" comp.mod");
					break;
				case UPS_PORT_LS_LOOPBACK:
					printf(" loopback");
					break;
				}
			}

			printf("\n");
		}
	}
}

void
print_device(int fd, uint8_t addr)
{
	struct usb_device_ddesc ddd;
	ddd.udd_addr = addr;

	if (ioctl(fd, USB_DEVICE_GET_DDESC, &ddd) == -1) {
		if (errno != ENXIO)
			warn("addr %u", addr);
		return;
	}

	printf("addr %02u: max packet: %2u, num configs: %u, "
	    "iManufacturer: %hhu\n",
	    addr, ddd.udd_desc.bMaxPacketSize,
	    ddd.udd_desc.bNumConfigurations,
	    ddd.udd_desc.iManufacturer);
}

uint16_t
print_config(int fd, uint8_t addr, int config, bool quiet)
{
	struct usb_device_cdesc cd;
	cd.udc_addr = addr;
	cd.udc_config_index = config;

	if (ioctl(fd, USB_DEVICE_GET_CDESC, &cd) == -1) {
		if (errno != ENXIO)
			warn("addr %u", addr);
		return 0;
	}

	if (quiet)
		goto end;

	printf("addr %02u, config %02u: interfaces: %u, "
	    "max-power: %umA", addr, cd.udc_desc.bConfigurationValue,
	    cd.udc_desc.bNumInterfaces,
	    cd.udc_desc.bMaxPower * UC_POWER_FACTOR);
	printf("\n\t attr 0x%02x:", cd.udc_desc.bmAttributes);
	if (cd.udc_desc.bmAttributes & UC_BUS_POWERED)
		printf(" bus-powered");
	if (cd.udc_desc.bmAttributes & UC_SELF_POWERED)
		printf(" self-powered");
	if (cd.udc_desc.bmAttributes & UC_REMOTE_WAKEUP)
		printf(" remote-wakeup");

	puts("");
end:
	return UGETW(&cd.udc_desc.wTotalLength);
}

void
print_fconfig(u_char* cur)
{
	printf("config %02u:\n", *(cur + 5));
}

void
print_iface(u_char* cur)
{
	printf("\t iface: %02u, altset: %02u, numendpts: %02u, "
	    "class: %02u, subclass: %02u, protocol: %02u\n",
	    *(cur + 2), *(cur + 3), *(cur + 4), *(cur + 5),
	    *(cur + 6), *(cur + 7));
}

void
print_endpt(u_char* cur)
{
	printf("\t \t endpt_addr: %02u, dir: %s, ",
	    (*(cur + 2) & 0x3), *(cur + 2) & 0x7 ? "in" : "out");

	switch (*(cur +3) & 0x3) {
	case 0:
		printf("control, ");
		break;
	case 1:
		printf("isochronous, ");
		printf("sync_type: ");
		switch (*(cur + 3) & 0xC) {
		case 0:
			printf("none, ");
			break;
		case 1:
			printf("async, ");
			break;
		case 2:
			printf("adaptive, ");
			break;
		case 3:
			printf("sync, ");
			break;
		}
		break;
	case 2:
		printf("bulk, ");
		break;
	case 3:
		printf("interrupt, ");
		break;
	}

	printf("max_packet: %u, polling_interval: %02u\n",
	    UGETW(cur + 4), *(cur + 6));
}

void
print_unknown(u_char* cur)
{
	printf("\t unknown: %02u", *(cur + 1));
	for (size_t i = 0; i < *cur; i++) {
		if (i % 10 == 0)
			printf("\n\t ");
		printf("0x%02x ", *(cur + i));
	}
	printf("\n");
}

void
print_full(int fd, uint8_t addr, int config, uint16_t wtotlen)
{
	struct usb_device_fdesc dfd;
	dfd.udf_addr = addr;
	dfd.udf_config_index = config;
	dfd.udf_size = wtotlen;
	if ((dfd.udf_data = (u_char *)malloc(wtotlen)) == NULL)
		errx(1, "malloc");

	if (ioctl(fd, USB_DEVICE_GET_FDESC, &dfd) == -1) {
		if (errno != ENXIO)
			warn("addr %u", addr);
		return;
	}

	printf("addr %02u, ", addr);
	for (u_char* cur = dfd.udf_data; cur < dfd.udf_data + wtotlen;
	    cur += *cur) {
		switch (*(cur + 1)) {
		case C_DESC:
			print_fconfig(cur);
			break;
		case I_DESC:
			print_iface(cur);
			break;
		case E_DESC:
			print_endpt(cur);
			break;
		case S_DESC:			
		default:
			print_unknown(cur);
			break;
		}
	}
}

void
dump_controller(char *name, int fd, uint8_t addr)
{
	memset(done, 0, sizeof(done));

	if (addr) {
		dump_device(fd, addr);
		return;
	}

	printf("Controller %s:\n", name);
	for (addr = 1; addr < USB_MAX_DEVICES; addr++)
		if (!done[addr])
			dump_device(fd, addr);
}

void
dump_stats(char *name, int fd)
{
	struct usb_device_stats ds;
	
	printf("Controller %s:", name);
	if (ioctl(fd, USB_DEVICESTATS, &ds) == -1) {
		if (errno != ENXIO)
			warn("controller %s", name);
		return;
	}
	
	printf("\n\t Transfers completed:");
	printf("\n\t Control: %lu", ds.uds_requests[0]);
	printf("\n\t Isochronous: %lu", ds.uds_requests[1]);
	printf("\n\t Bulk: %lu", ds.uds_requests[2]);
	printf("\n\t Interrupt: %lu\n", ds.uds_requests[3]);
}

void
dump_device_desc(char *name, int fd, uint8_t addr)
{
	if (addr) {
		print_device(fd, addr);
		return;
	}

	printf("Controller %s:\n", name);
	for (addr = 1; addr < USB_MAX_DEVICES; addr++)
		print_device(fd, addr);
}

void
dump_config(char *name, int fd, uint8_t addr, int config)
{
	if (addr) {
		print_config(fd, addr, config, false);
		return;
	}

	printf("Controller %s:\n", name);
	for (addr = 1; addr < USB_MAX_DEVICES; addr++)
		print_config(fd, addr, config, false);
}

void
full_dump(char *name, int fd, uint8_t addr, int config)
{
	uint16_t wtotlen;
	if (addr) {
		wtotlen = print_config(fd, addr, config, true);
		print_full(fd, addr, config, wtotlen);
		return;
	}

	printf("Controller %s:\n", name);
	for (addr = 1; addr < USB_MAX_DEVICES; addr++) {
		wtotlen = print_config(fd, addr, config, true);
		print_full(fd, addr, config, wtotlen);
	}
}
	

int
main(int argc, char **argv)
{
	int ch, fd;
	int cfgnum = USB_CURRENT_CONFIG_INDEX;
	int command = COMM_INFO;
	char *controller = NULL;
	uint8_t addr = 0;
	const char *errstr;

	while ((ch = getopt(argc, argv, "a:c::d:ef::sv?")) != -1) {
		switch (ch) {
		case 'a':
			addr = strtonum(optarg, 1, USB_MAX_DEVICES-1, &errstr);
			if (errstr)
				errx(1, "addr %s", errstr);
			break;
		case 'c':
			command = COMM_CDSC;
			if (optarg) {
				cfgnum = strtonum(optarg, 1,
				    USB_MAX_CONFIGS, &errstr) - 1;
				if (errstr)
					errx(1, "config %s", errstr);
			}				
			break;
		case 'd':
			controller = optarg;
			break;
		case 'e':
			command = COMM_DDSC;
			break;
		case 'f':
			command = COMM_FDSC;
			if (optarg) {
				cfgnum = strtonum(optarg, 1,
				    USB_MAX_CONFIGS, &errstr) - 1;
				if (errstr)
					errx(1, "config %s", errstr);
			}
			break;
		case 's':
			command = COMM_STAT;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

	if (unveil("/dev", "r") == -1)
		err(1, "unveil");
	if (unveil(NULL, NULL) == -1)
		err(1, "unveil");

	if (controller == NULL) {
		int i;
		int ncont = 0;

		for (i = 0; i < 10; i++) {
			char path[PATH_MAX];

			snprintf(path, sizeof(path), "%s%d", USBDEV, i);
			if ((fd = open(path, O_RDONLY)) < 0) {
				if (errno != ENOENT && errno != ENXIO)
					warn("%s", path);
				continue;
			}

			switch (command) {
			case COMM_INFO:
				dump_controller(path, fd, addr);
				break;
			case COMM_CDSC:
				dump_config(path, fd, addr, cfgnum);
				break;
			case COMM_DDSC:
				dump_device_desc(path, fd, addr);
				break;
			case COMM_FDSC:
				full_dump(path, fd, addr, cfgnum);
				break;
			case COMM_STAT:
				dump_stats(path, fd);
				break;
			}
			close(fd);
			ncont++;
		}
		if (verbose && ncont == 0)
			printf("%s: no USB controllers found\n",
			    __progname);
	} else {
		if ((fd = open(controller, O_RDONLY)) < 0)
			err(1, "%s", controller);

		switch (command) {
		case COMM_INFO:
			dump_controller(controller, fd, addr);
			break;
		case COMM_CDSC:
			dump_config(controller, fd, addr, cfgnum);
			break;
		case COMM_DDSC:
			dump_device_desc(controller, fd, addr);
			break;
		case COMM_FDSC:
			full_dump(controller, fd, addr, cfgnum);
			break;
		case COMM_STAT:
			dump_stats(controller, fd);
			break;
		}
		
		close(fd);
	}

	return 0;
}
