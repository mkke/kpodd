function onDeviceAdded(kpod) {
	print(kpod.manufacturer + " " + kpod.product + " " + kpod.getId() + " (V " + (kpod.version()) + ") added");
	kpod.beep(3, 2, 5);
	kpod.ledAuxControl(true, true);
	kpod.configure(200, true);
}

function onDeviceRemoved(kpod) {
	print("kpod removed");
}

var KPod = {
	pos: 0,
	onUpdateReport: function(report) {
		var ticks = (report[2] * 256 + report[1]);
		if (ticks & 0x8000) {
			ticks = -65536 + ticks;
		}
		var button = report[3] & 0x0f;
		var hold = !!(report[3] & 0x10);
		var ri = (report[3] & 0x60) >> 5;
		var rocker = ri == 0x00 ? "center" : ri == 0x01 ? "right" : ri == 0x02 ? "left" : "error";
		
		this.pos += ticks;
		print("pos = " + this.pos + "; ticks = " + ticks + "; button = " + button + "; "
				+ "hold = " + hold + "; rocker = " + rocker);
	},
	beep: function(toneFrequency, toneLevel, duration) {
		return this.send(0x5a, toneFrequency, toneLevel, duration);
	},
	configure: function(scale, mute) {
		return this.send(0x43, (scale == 100 ? 2 : 0) + (mute ? 1 : 0));
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

print("kpod.js loaded");
/*
rig.getFrequency(function (rigFrequency) {
	print("rig frequency = " + rigFrequency);
	freq = rigFrequency;
});
*/
