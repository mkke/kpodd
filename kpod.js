var BEEP_1000HZ = 0;
var BEEP_1500HZ = 1;
var BEEP_2000HZ = 2;
var BEEP_500HZ = 3;

var BEEP_LEVEL_LOW    = 0;
var BEEP_LEVEL_MEDIUM = 1;
var BEEP_LEVEL_HIGH   = 2;

var SCALE_200CPR = 0;
var SCALE_100CPR = 1;

function onDeviceAdded(kpod) {
	print(kpod.manufacturer + " " + kpod.product + " " + kpod.getId() + " (V " + (kpod.version()) + ") added");
	kpod.beep(BEEP_500HZ, BEEP_LEVEL_HIGH, 5);
	kpod.ledAuxControl(true, true);
	kpod.configure(SCALE_200CPR, true);
	
	try {
		kpod.freq = rig.get_freq();
		kpod.lastUpdate = Date.now();
	} catch (e) {
		print("cannot get freq: " + e);
	}
}

function onDeviceRemoved(kpod) {
	print("kpod removed");
}

// the KPod prototype
var KPod = {
	freq: 0,
	speed: 1,
	lastUpdate: 0,
	onUpdateReport: function(report) {
		var ticks = (report[2] * 256 + report[1]);
		if (ticks & 0x8000) {
			ticks = -65536 + ticks;
		}
		var button = report[3] & 0x0f;
		var hold = !!(report[3] & 0x10);
		var ri = (report[3] & 0x60) >> 5;
		var rocker = ri == 0x00 ? "center" : ri == 0x01 ? "right" : ri == 0x02 ? "left" : "error";

		if (Date.now() - this.lastUpdate > 1000) {
			try {
				this.freq = rig.get_freq();
				this.lastUpdate = Date.now();
			} catch (e) {
				print(e);
				return; // no point in continuing
			}
		}
		
		if (ticks != 0) {
			this.freq += ticks * this.speed;
			print("freq = " + this.freq + "; ticks = " + ticks + "; button = " + button + "; "
					+ "hold = " + hold + "; rocker = " + rocker);
			try {
				rig.set_freq(this.freq);
				this.lastUpdate = Date.now();
			} catch (e) {
				print(e);
			}
		}
		
		switch (button) {
		case 1:
			// this will only work on MacOS
			system("say '" + (this.freq / 1000000).toFixed(3).replace("\\.", ",") + " megahertz'");
			break;
		case 5:
			if (hold) {
				this.speed = 1;
			} else if (this.speed < 1000) {
				this.speed *= 10;
			}
			switch (this.speed) {
			case 1:
				this.beep(BEEP_500HZ, BEEP_LEVEL_HIGH, 20);
				break;
			case 10:
				this.beep(BEEP_1000HZ, BEEP_LEVEL_HIGH, 10);
				break;
			case 100:
				this.beep(BEEP_1500HZ, BEEP_LEVEL_HIGH, 10);
				break;
			case 1000:
				this.beep(BEEP_2000HZ, BEEP_LEVEL_HIGH, 10);
				break;
			}
			break;
		}
	},
	beep: function(toneFrequency, toneLevel, duration) {
		return this.send(0x5a, toneFrequency, toneLevel, duration);
	},
	configure: function(scale, mute) {
		return this.send(0x43, (scale == SCALE_100CPR ? 2 : 0) + (mute ? 1 : 0));
	},
	getId: function() {
		var report = this.send(0x3d);
		var id = "";
		for (var i = 1; i < report.length; i++) {
			if (report[i] == 0) {
				break;
			}
			id += String.fromCharCode(report[i]);
		}
		return id;
	},
	ledAuxControl: function(led1, led2, led3, led4, rocker_led, aux1, aux2, aux3) {
		return this.send(0x4f, (rocker_led ? 0x80 : 0) | 
				(led4 ? 0x40 : 0) | (led3 ? 0x20 : 0) | (led2 ? 0x10 : 0) | (led1 ? 0x08 : 0) |
				(aux3 ? 0x04 : 0) | (aux2 ? 0x02 : 0) | (aux1 ? 0x01 : 0));
	},
	reset: function() {
		return this.send(0x72);
	},
	version: function() {
		var report = this.send(0x76);
		return (report[2] * 256 + report[1]) / 100;
	}
};

function Rig(server, port) {
	this.server = server;
	this.port = port;
	this.socket = Socket.connect(server, port);
}

// sends a command and wait for response in '+' format (newline-separated)
Rig.prototype.send = function(cmd) {
	if (this.socket === null) {
		this.socket = Socket.connect(this.server, this.port);
	}
	if (this.socket === null) {
		throw "rig://" + this.server + ":" + this.port + ": connect failed";
	}
	if (this.socket.send(cmd + "\n") == 0) {
		this.socket = null;
		throw "rig://" + this.server + ":" + this.port + ": send failed";
	}
	
	var response = "";
	while (response.indexOf("\nRPRT") < 0) {
		var resp = this.socket.recv();
		if (resp === null || resp === "") {
			this.socket = null;
			throw "rig://" + this.server + ":" + this.port + ": client disconnected";
		}
		
		response += resp;
	}
	
	var lines = response.split("\n");
	var responseObj = {
		"cmd": lines.shift()	
	};
	
	lines.forEach(function (line) {
		if (line.indexOf("RPRT ") == 0) {
			var code = line.substr(5);
			if (code != 0) {
				throw "rig://" + this.server + ":" + this.port + ": '" + cmd + "' return code " + code;
			}
		} else {
			var idx = line.indexOf(":");
			if (idx > 0) {
				responseObj[line.substr(0, idx)] = line.substr(idx + 2);
			}
		}
	});
	
	return responseObj;
};

Rig.prototype.get_freq = function() {
	return +this.send("+f")["Frequency"];
};

Rig.prototype.set_freq = function(freq) {
	this.send("+F" + freq);
};

print("connecting to rigctld at " + options.server + ":" + options.port);
var rig = new Rig(options.server, options.port);

