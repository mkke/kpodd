/*
 * Kpod daemon
 *
 * Copyright (C) 2016 by Michael Stuermer <ms@mallorn.de>
 *
 * based on the hidapi example file hidtest.c
 */

#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include "hidapi.h"
#include "v7.h"
#include "popt.h"

#ifdef _WIN32
	#include <windows.h>
#else
	#include <unistd.h>
	#include <signal.h>
#endif

#define HID_READ_TIMEOUT 20
#define HID_RESPONSE_LENGTH 8

struct KPod_cmd_packet {
	unsigned char cmd;
	unsigned char data[7];
} __attribute__((packed));

struct KPod_report_packet {
	unsigned char cmd; // command reply
	int16_t ticks; // encoder tick count, signed 16 bit
	unsigned char controls; // button, tap/hold and rocker state
	unsigned char spare[4]; // spares, TBD
} __attribute__((packed));

struct KPod_id_report_packet {
	unsigned char cmd; // will contain ‘=’
	char id_string[7]; // will contain “KPOD”
} __attribute__((packed));

struct KPod_device_list {
    struct KPod_device *head;
    struct KPod_device *tail;
};

struct KPod_device {
	char* path;
	hid_device* hid_device;
	v7_val_t js_object;
	unsigned char remove_pending;
    struct KPod_device *prev;
    struct KPod_device *next;
} __attribute__((packed));

const char* ROCKER_CENTER = "center";
const char* ROCKER_RIGHT = "right";
const char* ROCKER_LEFT = "left";
const char* ROCKER_ERROR = "error";

void kpod_sleep(long duration) {
	#ifdef WIN32
	Sleep(duration);
	#else
	usleep(duration * 1000);
	#endif
}

void usage(poptContext cmd, int exitcode, char* error, char* addl) {
	poptPrintUsage(cmd, stderr, 0);
	if (error) {
		fprintf(stderr, "%s: %s0", error, addl);
	}
	exit(exitcode);
}

enum v7_err throw_hid_exception(struct v7 *v7, const char* prefix, hid_device* handle) {
	char msg[255] = {0};
	// hid_error is always null on MacOS
	snprintf((char*) &msg, sizeof(msg), "%s: %ls", prefix, hid_error(handle));
	return v7_throwf(v7, "Error", (const char*) &msg);
}

void add_kpod_device(struct KPod_device_list* list, struct KPod_device* kpod) {
	if (list->head == NULL) {
		list->head = kpod;
		list->tail = kpod;
		kpod->prev = NULL;
		kpod->next = NULL;
	} else {
		kpod->prev = list->tail;
		kpod->next = NULL;
		list->tail = kpod;
	}
}

void remove_kpod_device(struct KPod_device_list* list, struct KPod_device* kpod) {
	if (kpod->prev == NULL) {
		list->head = kpod->next;
	} else {
		kpod->prev->next = kpod->next;
	}
	if (kpod->next == NULL) {
		list->tail = kpod->prev;
	} else {
		kpod->next->prev = kpod->prev;
	}
}

static void js_array_for_kpod_report(struct v7* v7, unsigned char kpod_report[8], v7_val_t* js_report) {
	*js_report = v7_mk_array(v7);
	for (int i = 0; i < 8; i++) {
		v7_array_push(v7, *js_report, v7_mk_number(v7, (double) kpod_report[i]));
	}
}

static enum v7_err js_kpod_send(struct v7 *v7, v7_val_t* result) {
	struct KPod_device* kpod = v7_get_user_data(v7, v7_get_this(v7));

	int len = v7_argc(v7);
	if (len > 0) {
		struct KPod_cmd_packet kpod_cmd = {0};
		kpod_cmd.cmd = (unsigned char) v7_get_int(v7, v7_arg(v7, 0));
		for (int i = 1; i < len; i++) {
			kpod_cmd.data[i - 1] = (unsigned char) v7_get_int(v7, v7_arg(v7, i));
		}

		if (hid_write(kpod->hid_device, (const unsigned char*) &kpod_cmd, sizeof(kpod_cmd)) == sizeof(kpod_cmd)) {
			unsigned char kpod_report[256] = {0};
			int err = hid_read_timeout(kpod->hid_device, (unsigned char*) &kpod_report, HID_RESPONSE_LENGTH, HID_READ_TIMEOUT);
			if (err > 0 && err != HID_RESPONSE_LENGTH) {
				kpod->remove_pending = 1;
				*result = v7_mk_null();
				return v7_throwf(v7, "Error", "short hid_read: got %d expected %d\n", err, HID_RESPONSE_LENGTH);
			} else if (err == 0) {
				kpod->remove_pending = 1;
				*result = v7_mk_null();
				return v7_throwf(v7, "Error", "hid_read timeout\n");
			} else if (err < 0) {
				kpod->remove_pending = 1;
				*result = v7_mk_null();
				return throw_hid_exception(v7, "hid_read failed\n", kpod->hid_device);
			}

			js_array_for_kpod_report(v7, kpod_report, result);

			return V7_OK;
		} else {
			kpod->remove_pending = 1;
			*result = v7_mk_null();
			return throw_hid_exception(v7, "hid_write failed\n", kpod->hid_device);
		}
	} else {
		*result = v7_mk_null();
		return v7_throwf(v7, "Error", "cmd data missing\n");
	}
}

enum v7_err call_onDevice_listener(struct v7* v7, struct KPod_device* kpod, const char* listener_name, const char* ctx) {
	// call the JS event listener
	v7_val_t func, result, args;
	enum v7_err err;
	func = v7_get(v7, v7_get_global(v7), listener_name, ~0);

	args = v7_mk_array(v7);
	v7_array_push(v7, args, kpod->js_object);

	if ((err = v7_apply(v7, func, v7_mk_undefined(), args, &result)) != V7_OK) {
		v7_print_error(stderr, v7, ctx, result);
	}

	return err;
}

int open_kpod(struct v7* v7, struct KPod_device_list* open_kpods, char* devicePath) {
	hid_device* handle = hid_open_path(devicePath);
	if (!handle) {
		return 3;
	}

	// Set the hid_read() function to be blocking.
	hid_set_nonblocking(handle, 0);

	// directly after the hid device is opened, it must be added to the open_list
	struct KPod_device* new_kpod = (struct KPod_device*) malloc(sizeof(struct KPod_device));
	new_kpod->path = strdup(devicePath);
	new_kpod->hid_device = handle;
	new_kpod->remove_pending = 0;
	add_kpod_device(open_kpods, new_kpod);

	// build JS object for the kpod
	v7_val_t proto = v7_get(v7, v7_get_global(v7), "KPod", ~0);
	new_kpod->js_object = v7_mk_object(v7);
	v7_own(v7, &new_kpod->js_object);
	v7_set_proto(v7, new_kpod->js_object, proto);
	v7_set_user_data(v7, new_kpod->js_object, (void*) new_kpod);
	v7_set(v7, new_kpod->js_object, "path", ~0, v7_mk_string(v7, devicePath, ~0, 1));
    v7_set_method(v7, new_kpod->js_object, "send", &js_kpod_send);

	#define MAX_STR 255
	wchar_t wstr[MAX_STR];
	wstr[0] = 0x0000;
	char str[MAX_STR];
    if (hid_get_product_string(handle, (wchar_t*) &wstr, MAX_STR) >= 0) {
    	snprintf((char*) &str, MAX_STR, "%ls", (wchar_t*) &wstr);
    	v7_set(v7, new_kpod->js_object, "product", ~0, v7_mk_string(v7, (char*) &str, ~0, 1));
    }

    wstr[0] = 0x0000;
    if (hid_get_manufacturer_string(handle, (wchar_t*) &wstr, MAX_STR) >= 0) {
    	snprintf((char*) &str, MAX_STR, "%ls", (wchar_t*) &wstr);
    	v7_set(v7, new_kpod->js_object, "manufacturer", ~0, v7_mk_string(v7, (char*) &str, ~0, 1));
    }

    wstr[0] = 0x0000;
    if (hid_get_serial_number_string(handle, (wchar_t*) &wstr, MAX_STR) >= 0) {
    	snprintf((char*) &str, MAX_STR, "%ls", (wchar_t*) &wstr);
    	v7_set(v7, new_kpod->js_object, "serial", ~0, v7_mk_string(v7, (char*) &str, ~0, 1));
    }

    call_onDevice_listener(v7, new_kpod, "onDeviceAdded", "open_kpod");

	return 0;
}

void close_kpod(struct v7* v7, struct KPod_device_list* open_kpods, struct KPod_device* kpod) {
	call_onDevice_listener(v7, kpod, "onDeviceRemoved", "close_kpod");
	v7_disown(v7, &kpod->js_object);
	hid_close(kpod->hid_device);
	free(kpod->path);

	remove_kpod_device(open_kpods, kpod);
	free(kpod);
}

int close_kpods(struct v7* v7, struct KPod_device_list* open_kpods) {
	int code = 0;

	struct KPod_device* kpod = open_kpods->head;
	while (kpod) {
		struct KPod_device* old_kpod = kpod;
		kpod = kpod->next;

		close_kpod(v7, open_kpods, old_kpod);
	}

	return code;
}

int sigint = 0;
void sigint_handler(int signo) {
  sigint = 1;
}

void update_kpod(struct v7* v7, struct KPod_device* kpod) {
	struct KPod_cmd_packet kpod_cmd = {0};
	kpod_cmd.cmd = (unsigned char) 'u';

	unsigned char kpod_report[256] = {0};
	if (hid_write(kpod->hid_device, (const unsigned char*) &kpod_cmd, sizeof(kpod_cmd)) == sizeof(kpod_cmd)) {
		int err = hid_read_timeout(kpod->hid_device, (unsigned char*) &kpod_report, HID_RESPONSE_LENGTH, HID_READ_TIMEOUT);
		if (err > 0 && err != HID_RESPONSE_LENGTH) {
			kpod->remove_pending = 1;
			fprintf(stderr, "short hid_read: got %d expected %d\n", err, HID_RESPONSE_LENGTH);
		} else if (err == 0) {
			kpod->remove_pending = 1;
			fprintf(stderr, "hid_read timeout\n");
		} else if (err < 0) {
			kpod->remove_pending = 1;
			fprintf(stderr, "hid_read failed\n");
		}
	} else {
		kpod->remove_pending = 1;
		fprintf(stderr, "hid_write failed\n");
	}

	if (kpod->remove_pending) {
		return;
	}

	if (kpod_report[0] == 'u') {
		// build report object
		v7_val_t js_report;
		js_array_for_kpod_report(v7, kpod_report, &js_report);
		v7_own(v7, &js_report);

		// call the JS event listener
		v7_val_t func, result, args;
		enum v7_err err;
		func = v7_get(v7, kpod->js_object, "onUpdateReport", ~0);

		args = v7_mk_array(v7);
		v7_array_push(v7, args, js_report);

		if ((err = v7_apply(v7, func, kpod->js_object, args, &result)) != V7_OK) {
			v7_print_error(stderr, v7, "update_kpod", result);
		}

		v7_disown(v7, &js_report);
	}
}

int is_kpod_open(struct KPod_device_list* open_kpods, const struct hid_device_info* hid_device) {
	struct KPod_device* kpod = open_kpods->head;
	while (kpod) {
		if (strcmp(kpod->path, hid_device->path) == 0) {
			return 1;
		}
		kpod = kpod->next;
	}
	return 0;
}

void device_scan(unsigned short productId, unsigned short vendorId, struct v7* v7, struct KPod_device_list* open_kpods) {
	struct hid_device_info *devs, *cur_dev;
	devs = hid_enumerate(vendorId, productId);

	cur_dev = devs;
	while (cur_dev) {
		if (!is_kpod_open(open_kpods, cur_dev)) {
			open_kpod(v7, open_kpods, cur_dev->path);
		}
		cur_dev = cur_dev->next;
	}

	hid_free_enumeration(devs);
}

int main_hid(unsigned short productId, unsigned short vendorId,
		char* devicePath, long updateInterval, long deviceScanInterval,
		char* server, unsigned short port, struct v7 *v7) {
	struct KPod_device_list open_kpods = {0};
	long device_scan_count = 0;
	int code;

	if (hid_init()) {
		return 3;
	}

	if (devicePath != NULL) {
		if ((code = open_kpod(v7, &open_kpods, devicePath)) != 0) {
			return code;
		}
	}

	// start the event loop
	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	while (sigint == 0) {
		if ((deviceScanInterval > 0) && (device_scan_count-- == 0)) {
			device_scan(productId, vendorId, v7, &open_kpods);

			/* This is only an approximation, but we don't really care,
			   and it's not as expensive as checking time() every loop. */
			device_scan_count = deviceScanInterval / updateInterval;
		}

		struct KPod_device* kpod = open_kpods.head;
		while (kpod) {
			struct KPod_device* curr_kpod = kpod;
			kpod = kpod->next;

			update_kpod(v7, curr_kpod);

			if (curr_kpod->remove_pending) {
				close_kpod(v7, &open_kpods, curr_kpod);
				if (devicePath != NULL) {
					// if a device path was specified explicitly, we exit on device disconnect
					hid_exit();
					return 0;
				}
			}
		}
		kpod_sleep(updateInterval);
	}

	close_kpods(v7, &open_kpods);

	hid_exit();

	return code;
}

int main_V7(unsigned short productId, unsigned short vendorId,
		char* devicePath, long updateInterval, long deviceScanInterval,
		char* server, unsigned short port, char* configPath) {
    struct v7 *v7 = v7_create();

    enum v7_err err;
    v7_val_t v7_result;
    if ((err = v7_exec_file(v7, configPath, &v7_result)) != V7_OK) {
    	switch (err) {
    	case V7_SYNTAX_ERROR: // if js_code in not a valid code. result is undefined.
    		fprintf(stderr, "%s: syntax error\n", configPath);
    		return 2;
    	case V7_EXEC_EXCEPTION: // if js_code threw an exception. result stores an exception object.
    		//fprintf(stderr, "%s: exception occurred\n", configPath);
    		v7_print_error(stderr, v7, "load_config", v7_result);
    		return 2;
    	case V7_AST_TOO_LARGE: // if js_code contains an AST segment longer than 16 bit. result is undefined.
    		fprintf(stderr, "%s: AST too large\n", configPath);
    		return 2;
    	case V7_INTERNAL_ERROR:
    		fprintf(stderr, "%s: V7 internal error\n", configPath);
    		return 2;
    	case V7_OK:
    		break;
    	}
    }

    int code = main_hid(productId, vendorId, devicePath, updateInterval, deviceScanInterval, server, port, v7);

	v7_destroy(v7);
	return code;
}

int main(int argc, const char* argv[]) {
	poptContext cmd;
	unsigned int productId = 0xf12d;
	unsigned int vendorId = 0x04d8;
	char* server = "127.0.0.1";
	unsigned int port = 4532;
	char* devicePath = NULL;
	char* configPath = "~/.kpod";
	long updateInterval = 5;
	long deviceScanInterval = 1000;

    struct poptOption optionsTable[] = {
       { "product-id", 'P', POPT_ARG_INT, &productId, 0,
                           "Kpod USB product id", "id" },
	   { "vendor-id", 'V', POPT_ARG_INT, &vendorId, 0,
						   "Kpod USB vendor id", "id" },
	   { "device", 'd', POPT_ARG_STRING, &devicePath, 0,
						   "Kpod USB device path", "path" },
	   { "update-interval", 'u', POPT_ARG_LONG, &updateInterval, 0,
						   "Kpod update interval", "msecs" },
	   { "device-scan-interval", 'S', POPT_ARG_LONG, &deviceScanInterval, 0,
						   "Kpod device scan interval (0 = disable)", "msecs" },
	   { "server", 's', POPT_ARG_STRING, &server, 0,
						   "rigctld hostname", "name" },
	   { "port", 'p', POPT_ARG_INT, &port, 0,
						   "rigctld server port", "port" },
	   { "config", 'c', POPT_ARG_STRING, &configPath, 0,
						   "configuration file", "path" },
       POPT_AUTOHELP
       { NULL, 0, 0, NULL, 0 }
     };

    cmd = poptGetContext(NULL, argc, argv, optionsTable, 0);

    int c;
    while ((c = poptGetNextOpt(cmd)) >= 0) {
    }

    if (c < -1) {
       /* an error occurred during option processing */
       fprintf(stderr, "%s: %s\nTry '--help'\n",
               poptBadOption(cmd, POPT_BADOPTION_NOALIAS),
               poptStrerror(c));
       exit(1);
    } else if (updateInterval < 1) {
    	fprintf(stderr, "update interval must be >= 1 msec\n");
    }

    int configPathLen = strlen(configPath);
    if (configPathLen >= 2 && configPath[0] == '~' && configPath[1] == '/') {
    	char* home = getenv("HOME");
    	// the configPath has 2 extra chars, but we need space for a slash and terminating NUL
    	int len = configPathLen + strlen(home);
    	char* newConfigPath = malloc(len);
    	snprintf(newConfigPath, len, "%s/%s", home, configPath + 2);
    	configPath = newConfigPath;
    }

    int code = main_V7(productId, vendorId, devicePath, updateInterval, deviceScanInterval, server, port, configPath);

	poptFreeContext(cmd);
	return code;
}

