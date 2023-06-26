# pear-esp32-examples
Stream JPEG over WebRTC datachannel with ESP32-EYE.

## Hardware
[ESP32-EYE](https://github.com/espressif/esp-who/blob/master/docs/en/get-started/ESP-EYE_Getting_Started_Guide.md)

<img src="https://www.espressif.com/sites/default/files/esp-eye-2-190116.png" width="128">

## Instructions

### Install esp-idf
at lease v5.0.0
```bash
$ git clone -b v5.0.2 https://github.com/espressif/esp-idf.git
$ cd esp-idf
$ source install.sh
$ source export.sh
```

### Download
```bash
$ git clone https://github.com/sepfy/pear-esp32-examples.git
$ cd pear-esp32-examples
$ git submodule init && git submodule update
$ cd components/srtp
$ git submodule init && git submodule update
```

### Configure
```bash
$ idf.py menuconfig
# Choose Example Configuration and change the SSID and password
```

### Build 
```bash
$ idf.py build
```

### Test
```bash
$ idf.py flash
```
Check the serial port message:
```
I (12936) webrtc: MQTT_EVENT_CONNECTED
I (12946) webrtc: sent subscribe successful, msg_id=5876
I (13246) webrtc: MQTT_EVENT_SUBSCRIBED, msg_id=5876
I (13246) webrtc: open browser and visit https://sepfy.github.io/webrtc?deviceId=esp32-xxxxxxxxxxxx
```
Open the browser and visit the website showing by serial port message
![image](https://github.com/sepfy/pear-esp32-examples/assets/22016807/ccb82e25-8017-4ed3-8b99-13d5445a7c92)


