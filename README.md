## Overview
KPodd connects to a [K-Pod](http://www.elecraft.com/k-pod/k-pod.htm) input
device via it's USB HID interface.

## Installation
KPodd is designed either to be called with a single device path, or to run
continuously and scan for new devices.

The configuration file is expected in $HOME/.kpod or must be specified
explicitly with '-c <path>'.

### MacOS
KPodd should run without any further preparations.

### Linux
KPodd needs access to the hidraw device which is usually only allowed for root.
To be able to run kpodd as user, a udev rule is needed:
    ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="f12d", GROUP="kpod"

## Configuration
The configuration file is a JavaScript file that is executed at program start
and must provide callbacks for onDeviceAdded and onDeviceRemoved.
Also, the KPod prototype object and it's onUpdateReport must be provided.
For every device found, a new object is created with the KPod object as
prototype.

## License
Kpodd uses signal11's hidapi library (http://www.signal11.us/oss/hidapi/),
libpopt for command-line processing (http://rpm5.org/files/popt/),
the V7 javascript engine (https://github.com/cesanta/v7/)
and the hamlib network protocol (http://hamlib.sourceforge.net).

V7 is used under GPL v2 license
To be compatible, Hidapi is used under BSD license (see hidapi/LICENSE-bsd.txt).
Libpopt is used under the MIT license (see popt-1.16/COPYING).

Kpodd itself is licensed under GPL v2.
