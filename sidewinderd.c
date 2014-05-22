/*
 * Sidewinder daemon - used for supporting the special keys of Microsoft
 * Sidewinder X4 / X6 gaming keyboards.
 *
 * Copyright (c) 2014 Tolga Cakir <tolga@cevel.net>
 * 
 * Special Thanks to Filip Wieladek, Andreas Bader and Alan Ott. Without
 * these guys, I wouldn't be able to do anything.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <libconfig.h>
#include <libudev.h>
#include <locale.h>

#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

/* VIDs & PIDs*/
#define VENDOR_ID_MICROSOFT			0x045e
#define PRODUCT_ID_SIDEWINDER_X6	0x074b
#define PRODUCT_ID_SIDEWINDER_X4	0x0768

/* constants */
#define MAX_BUF		8
#define SIZE_SKEYS	5
#define SIZE_MKEYS	8
#define MAX_EVENTS	1
#define MIN_PROFILE	0
#define MAX_PROFILE	2
#define SIDEWINDER_X6_MAX_SKEYS	30
#define SIDEWINDER_X4_MAX_SKEYS	6

/* special keys */
#define SKEY_S01	0x1
#define SKEY_S02	0x1 << 1
#define SKEY_S03	0x1 << 2
#define SKEY_S04	0x1 << 3
#define SKEY_S05	0x1 << 4
#define SKEY_S06	0x1 << 5
#define SKEY_S07	0x1 << 6
#define SKEY_S08	0x1 << 7
#define SKEY_S09	0x1 << 8
#define SKEY_S10	0x1 << 9
#define SKEY_S11	0x1 << 10
#define SKEY_S12	0x1 << 11
#define SKEY_S13	0x1 << 12
#define SKEY_S14	0x1 << 13
#define SKEY_S15	0x1 << 14
#define SKEY_S16	0x1 << 15
#define SKEY_S17	0x1 << 16
#define SKEY_S18	0x1 << 17
#define SKEY_S19	0x1 << 18
#define SKEY_S20	0x1 << 19
#define SKEY_S21	0x1 << 20
#define SKEY_S22	0x1 << 21
#define SKEY_S23	0x1 << 22
#define SKEY_S24	0x1 << 23
#define SKEY_S25	0x1 << 24
#define SKEY_S26	0x1 << 25
#define SKEY_S27	0x1 << 26
#define SKEY_S28	0x1 << 27
#define SKEY_S29	0x1 << 28
#define SKEY_S30	0x1 << 29

/* media keys */
#define MKEY_GAMECENTER		0x10
#define MKEY_RECORD			0x11
#define MKEY_PROFILE		0x14

/* global variables */
volatile uint8_t active = 1;
int32_t fd, uifd, epfd;

/* TODO: struct for special keys, including is_pressed and macro_path */
/* TODO: macro player and xml parser */

/* global structs */
struct uinput_user_dev *uidev;
struct input_event *inev;
struct epoll_event *epev;
struct sidewinder_data {
	uint16_t device_id;
	uint8_t profile;
	uint8_t auto_led;
	uint8_t record_led;
	uint8_t macropad;
	const char *device_node;
} *sw;

void handler() {
	active = 0;
}

void setup_udev() {
	/* udev */
	struct udev *udev;
	struct udev_device *dev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	char vid_microsoft[4], pid_sidewinder_x6[4], pid_sidewinder_x4[4];

	/* converting integers to strings */
	snprintf(vid_microsoft, sizeof(vid_microsoft) + 1, "%04x", VENDOR_ID_MICROSOFT);
	snprintf(pid_sidewinder_x6, sizeof(pid_sidewinder_x6) + 1, "%04x", PRODUCT_ID_SIDEWINDER_X6);
	snprintf(pid_sidewinder_x4, sizeof(pid_sidewinder_x4) + 1, "%04x", PRODUCT_ID_SIDEWINDER_X4);

	udev = udev_new();

	if (!udev) {
		printf("Can't create udev\n");
		exit(1);
	}

	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "hidraw");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path, *temp_path;

		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		temp_path = udev_device_get_devnode(dev);
		dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_interface");

		if (strcmp(udev_device_get_sysattr_value(dev, "bInterfaceNumber"), "01") == 0) {
			dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");

			if (strcmp(udev_device_get_sysattr_value(dev, "idVendor"), vid_microsoft) == 0) {
				if (strcmp(udev_device_get_sysattr_value(dev, "idProduct"), pid_sidewinder_x6) == 0) {
					sw->device_node = temp_path;
					sw->device_id = PRODUCT_ID_SIDEWINDER_X6;
				} else if (strcmp(udev_device_get_sysattr_value(dev, "idProduct"), pid_sidewinder_x4) == 0) {
					sw->device_node = temp_path;
					sw->device_id = PRODUCT_ID_SIDEWINDER_X4;
				}
			}
		}

		udev_device_unref(dev);
	}

	/* Free the enumerator object */
	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}

void setup_hidraw() {
	fd = open(sw->device_node, O_RDWR | O_NONBLOCK);

	if (fd < 0) {
		printf("Can't open hidraw interface");
		exit(1);
	}
}

void setup_uidev() {
	uidev = calloc(7, sizeof(struct uinput_user_dev));
	inev = calloc(3, sizeof(struct input_event));
	uifd = open("/dev/uinput", O_WRONLY);

	if (uifd < 0) {
		uifd = open("/dev/input/uinput", O_WRONLY);

		if (uifd < 0) {
			printf("Can't open uinput");
			exit(1);
		}
	}

	/* TODO: dynamically get needed keys by macro_player(), and set_keybit() */
	ioctl(uifd, UI_SET_EVBIT, EV_KEY);
	ioctl(uifd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
	ioctl(uifd, UI_SET_KEYBIT, KEY_T);
	ioctl(uifd, UI_SET_KEYBIT, KEY_LEFTCTRL);
	snprintf(uidev->name, UINPUT_MAX_NAME_SIZE, "sidewinderd");
	uidev->id.bustype = BUS_USB;
	uidev->id.vendor = 0x1;
	uidev->id.product = 0x1;
	uidev->id.version = 1;
	write(uifd, uidev, sizeof(struct uinput_user_dev));
	ioctl(uifd, UI_DEV_CREATE);
}

void setup_epoll() {
	epev = calloc(2, sizeof(struct epoll_event));
	epfd = epoll_create1(0);
	epev->data.fd = fd;
	epev->events = EPOLLIN | EPOLLET;
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, epev);
}

void setup_config() {
	struct config_t *cfg;
	struct config_setting_t *root, *group, *setting;
	static const char *cfg_file = ".sidewinderd.conf";
	int ret;
	cfg = calloc(12, sizeof(struct config_t));

	config_init(cfg);
	root = config_root_setting(cfg);

	/* default global settings */
	setting = config_setting_add(root, "user", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "nobody");
	setting = config_setting_add(root, "group", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "nobody");
	setting = config_setting_add(root, "macro_path", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "/home/nobody/.sidewinderd/");
	setting = config_setting_add(root, "default_profile", CONFIG_TYPE_INT);
	config_setting_set_string(setting, "1");
	setting = config_setting_add(root, "ms_compat_mode", CONFIG_TYPE_BOOL);
	config_setting_set_string(setting, "false");

	/* TODO: use for-loop */

	/* profile 1 special keys */
	group = config_setting_add(root, "profile_1", CONFIG_TYPE_GROUP);
	setting = config_setting_add(group, "S01", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S02", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S03", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S04", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S05", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S06", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");

	/* profile 2 special keys */
	group = config_setting_add(root, "profile_2", CONFIG_TYPE_GROUP);
	setting = config_setting_add(group, "S01", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S02", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S03", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S04", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S05", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S06", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");

	/* profile 3 special keys */
	group = config_setting_add(root, "profile_3", CONFIG_TYPE_GROUP);
	setting = config_setting_add(group, "S01", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S02", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S03", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S04", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S05", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");
	setting = config_setting_add(group, "S06", CONFIG_TYPE_STRING);
	config_setting_set_string(setting, "unknown.mhm");

	if (config_read_file(cfg, cfg_file) == CONFIG_FALSE) {
		if (config_write_file(cfg, cfg_file) == CONFIG_FALSE) {
			/* TODO: error handling */
		}
	}

	config_destroy(cfg);
	free(cfg);
}

void feature_request() {
	unsigned char buf[2];

	/* buf[0] is Report Number, buf[1] is the control byte */
	buf[0] = 0x7;
	buf[1] = 0x04 << sw->profile;
	buf[1] |= sw->macropad;
	ioctl(fd, HIDIOCSFEATURE(sizeof(buf)), buf);
}

void toggle_macropad() {
	sw->macropad ^= 1;
	feature_request();
}

void switch_profile() {
	sw->profile++;

	if (sw->profile > MAX_PROFILE) {
		sw->profile = MIN_PROFILE;
	}

	feature_request();
}

void init_device() {
	sw->profile = 0;
	sw->auto_led = 0;
	sw->record_led = 0;
	sw->macropad = 0;
	feature_request();
}

/* TODO: general design, for use with play_macro() */
/* In this state, the function will open a terminal tab in GNOME 3.10 */
void send_key(uint32_t skey) {
	inev->type = EV_KEY;
	inev->code = KEY_LEFTCTRL;
	inev->value = 1;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_KEY;
	inev->code = KEY_LEFTSHIFT;
	inev->value = 1;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_KEY;
	inev->code = KEY_T;
	inev->value = 1;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_KEY;
	inev->code = KEY_LEFTCTRL;
	inev->value = 0;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_KEY;
	inev->code = KEY_LEFTSHIFT;
	inev->value = 0;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_KEY;
	inev->code = KEY_T;
	inev->value = 0;
	write(uifd, inev, sizeof(struct input_event));

	inev->type = EV_SYN;
	inev->code = 0;
	inev->value = 0;
	write(uifd, inev, sizeof(struct input_event));
}

/* TODO: improve multiple inputs */
void process_input(uint8_t nbytes, unsigned char *buf) {
	if (nbytes == SIZE_SKEYS && buf[0] == 8) {
		int i;

		/* cutting off buf[0] */
		for (i = 1; i < nbytes; i++) {
			uint32_t key;

			if (buf[i]) {
				int j;

				key = buf[i] << (8 * (i - 1));

				for (j = 0; j < SIDEWINDER_X4_MAX_SKEYS; j++) {
					int skey = 1 << j;

					if (key & skey) {
						send_key(skey);
					}
				}

				if (sw->device_id == PRODUCT_ID_SIDEWINDER_X6) {
					for (j = SIDEWINDER_X4_MAX_SKEYS; j < SIDEWINDER_X6_MAX_SKEYS; j++) {
						int skey = 1 << j;

						if (key & skey) {
							send_key(skey);
						}
					}
				}
			}
		}
	} else if (nbytes == SIZE_MKEYS && buf[0] == 1) {
		int i;
		uint32_t key;
		/* buf[0] == 1 means media keys, buf[6] shows pressed key */
		key = buf[6];

		switch(key) {
			case MKEY_GAMECENTER: toggle_macropad();	break;
			case MKEY_RECORD: printf("Record pressed\n");			break;
			case MKEY_PROFILE: switch_profile();					break;
		}
	}
}

void cleanup() {
	ioctl(uifd, UI_DEV_DESTROY);
	close(uifd);
	close(epfd);
	close(fd);
	free(epev);
	free(inev);
	free(uidev);
	free(sw);
}

int main(int argc, char **argv) {
	uint8_t nbytes;
	unsigned char buf[MAX_BUF];

	sw = calloc(4, sizeof(struct sidewinder_data));

	signal(SIGINT, handler);
	signal(SIGHUP, handler);
	signal(SIGTERM, handler);
	signal(SIGKILL, handler);

	setup_udev();		/* get device node */
	setup_hidraw();		/* setup hidraw interface */
	setup_uidev();		/* sending input events */
	setup_epoll();		/* watching hidraw device events */
	setup_config();		/* parsing config files */

	/* setting initial profile */
	init_device();

	while (active) {
		epoll_wait(epfd, epev, MAX_EVENTS, -1);
		nbytes = read(fd, buf, MAX_BUF);
		process_input(nbytes, buf);
	}

	cleanup();
	exit(0);
}
