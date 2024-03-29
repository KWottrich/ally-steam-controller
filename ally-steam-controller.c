// SPDX-License-Identifier: MIT
/*
 * UHID Example
 *
 * Copyright (c) 2023 Kenny Wottrich <kenny.wottrich@gmail.com>
 *
 * The code may be used by anyone for any purpose.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/uhid.h>

#define VENDOR_ID 0x28de
#define PRODUCT_ID 0x1205

static unsigned char reportDescriptor[] = {
	0x06, 0xFF, 0xFF,  // Usage Page (Vendor Defined 0xFFFF)
	0x09, 0x01,        // Usage (0x01)
	0xA1, 0x01,        // Collection (Application)
	0x15, 0x00,        //   Logical Minimum (0)
	0x26, 0xFF, 0x00,  //   Logical Maximum (255)
	0x75, 0x08,        //   Report Size (8)
	0x95, 0x40,        //   Report Count (64)
	0x09, 0x01,        //   Usage (0x01)
	0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
	0x09, 0x01,        //   Usage (0x01)
	0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
	0xC0,              // End Collection
};

static int uhid_write(int fd, const struct uhid_event *ev)
{
	ssize_t ret;

	ret = write(fd, ev, sizeof(*ev));
	if (ret < 0) {
		fprintf(stderr, "Cannot write to uhid: %m\n");
		return -errno;
	} else if (ret != sizeof(*ev)) {
		fprintf(stderr, "Wrong size written to uhid: %zd != %zu\n",
			ret, sizeof(ev));
		return -EFAULT;
	} else {
		return 0;
	}
}

static int create(int fd)
{
	struct uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_CREATE2;
	strcpy((char*)ev.u.create2.name, "ally-deck-controller");
	strcpy((char*)ev.u.create2.rd_data, reportDescriptor);
	ev.u.create2.rd_size = sizeof(reportDescriptor);
	ev.u.create2.bus = BUS_USB;
	ev.u.create2.vendor = VENDOR_ID;
	ev.u.create2.product = PRODUCT_ID;
	ev.u.create2.version = 0;
	ev.u.create2.country = 0;

	return uhid_write(fd, &ev);
}

static void destroy(int fd)
{
	struct uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_DESTROY;

	uhid_write(fd, &ev);
}

/* This parses raw output reports sent by the kernel to the device. A normal
 * uhid program shouldn't do this but instead just forward the raw report.
 * However, for ducomentational purposes, we try to detect LED events here and
 * print debug messages for it. */
static void handle_output(struct uhid_event *ev)
{
	fprintf(stdout, "Kernel sent output report of type 0x%x\n",
		ev->u.output.data[1]);
	return;
}

static int event(int fd)
{
	struct uhid_event ev;
	ssize_t ret;

	memset(&ev, 0, sizeof(ev));
	ret = read(fd, &ev, sizeof(ev));
	if (ret == 0) {
		fprintf(stderr, "Read HUP on uhid-cdev\n");
		return -EFAULT;
	} else if (ret < 0) {
		fprintf(stderr, "Cannot read uhid-cdev: %m\n");
		return -errno;
	} else if (ret != sizeof(ev)) {
		fprintf(stderr, "Invalid size read from uhid-dev: %zd != %zu\n",
			ret, sizeof(ev));
		return -EFAULT;
	}

	switch (ev.type) {
	case UHID_START:
		fprintf(stderr, "UHID_START from uhid-dev\n");
		break;
	case UHID_STOP:
		fprintf(stderr, "UHID_STOP from uhid-dev\n");
		break;
	case UHID_OPEN:
		fprintf(stderr, "UHID_OPEN from uhid-dev\n");
		break;
	case UHID_CLOSE:
		fprintf(stderr, "UHID_CLOSE from uhid-dev\n");
		break;
	case UHID_OUTPUT:
		fprintf(stderr, "UHID_OUTPUT from uhid-dev\n");
		handle_output(&ev);
		break;
	case UHID_OUTPUT_EV:
		fprintf(stderr, "UHID_OUTPUT_EV from uhid-dev\n");
		break;
	default:
		fprintf(stderr, "Invalid event from uhid-dev: %u\n", ev.type);
	}

	return 0;
}

static bool btn1_down;
static bool btn2_down;
static bool btn3_down;
static signed char abs_hor;
static signed char abs_ver;
static signed char wheel;

static int send_event(int fd)
{
	struct uhid_event ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = UHID_INPUT;
	ev.u.input.size = 5;

	ev.u.input.data[0] = 0x1;
	if (btn1_down)
		ev.u.input.data[1] |= 0x1;
	if (btn2_down)
		ev.u.input.data[1] |= 0x2;
	if (btn3_down)
		ev.u.input.data[1] |= 0x4;

	ev.u.input.data[2] = abs_hor;
	ev.u.input.data[3] = abs_ver;
	ev.u.input.data[4] = wheel;

	return uhid_write(fd, &ev);
}

static int keyboard(int fd)
{
	char buf[128];
	ssize_t ret, i;

	ret = read(STDIN_FILENO, buf, sizeof(buf));
	if (ret == 0) {
		fprintf(stderr, "Read HUP on stdin\n");
		return -EFAULT;
	} else if (ret < 0) {
		fprintf(stderr, "Cannot read stdin: %m\n");
		return -errno;
	}

	for (i = 0; i < ret; ++i) {
		switch (buf[i]) {
		case '1':
			btn1_down = !btn1_down;
			ret = send_event(fd);
			if (ret)
				return ret;
			break;
		case '2':
			btn2_down = !btn2_down;
			ret = send_event(fd);
			if (ret)
				return ret;
			break;
		case '3':
			btn3_down = !btn3_down;
			ret = send_event(fd);
			if (ret)
				return ret;
			break;
		case 'a':
			abs_hor = -20;
			ret = send_event(fd);
			abs_hor = 0;
			if (ret)
				return ret;
			break;
		case 'd':
			abs_hor = 20;
			ret = send_event(fd);
			abs_hor = 0;
			if (ret)
				return ret;
			break;
		case 'w':
			abs_ver = -20;
			ret = send_event(fd);
			abs_ver = 0;
			if (ret)
				return ret;
			break;
		case 's':
			abs_ver = 20;
			ret = send_event(fd);
			abs_ver = 0;
			if (ret)
				return ret;
			break;
		case 'r':
			wheel = 1;
			ret = send_event(fd);
			wheel = 0;
			if (ret)
				return ret;
			break;
		case 'f':
			wheel = -1;
			ret = send_event(fd);
			wheel = 0;
			if (ret)
				return ret;
			break;
		case 'q':
			return -ECANCELED;
		default:
			fprintf(stderr, "Invalid input: %c\n", buf[i]);
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	int fd;
	const char *path = "/dev/uhid";
	struct pollfd pfds[2];
	int ret;
	struct termios state;

	ret = tcgetattr(STDIN_FILENO, &state);
	if (ret) {
		fprintf(stderr, "Cannot get tty state\n");
	} else {
		state.c_lflag &= ~ICANON;
		state.c_cc[VMIN] = 1;
		ret = tcsetattr(STDIN_FILENO, TCSANOW, &state);
		if (ret)
			fprintf(stderr, "Cannot set tty state\n");
	}

	if (argc >= 2) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			fprintf(stderr, "Usage: %s [%s]\n", argv[0], path);
			return EXIT_SUCCESS;
		} else {
			path = argv[1];
		}
	}

	fprintf(stderr, "Open uhid-cdev %s\n", path);
	fd = open(path, O_RDWR | __O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "Cannot open uhid-cdev %s: %m\n", path);
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Create uhid device\n");
	ret = create(fd);
	if (ret) {
		close(fd);
		return EXIT_FAILURE;
	}

	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = fd;
	pfds[1].events = POLLIN;

	fprintf(stderr, "Press 'q' to quit...\n");
	while (1) {
		ret = poll(pfds, 2, -1);
		if (ret < 0) {
			fprintf(stderr, "Cannot poll for fds: %m\n");
			break;
		}
		if (pfds[0].revents & POLLHUP) {
			fprintf(stderr, "Received HUP on stdin\n");
			break;
		}
		if (pfds[1].revents & POLLHUP) {
			fprintf(stderr, "Received HUP on uhid-cdev\n");
			break;
		}

		if (pfds[0].revents & POLLIN) {
			ret = keyboard(fd);
			if (ret)
				break;
		}
		if (pfds[1].revents & POLLIN) {
			ret = event(fd);
			if (ret)
				break;
		}
	}

	fprintf(stderr, "Destroy uhid device\n");
	destroy(fd);
	return EXIT_SUCCESS;
}
