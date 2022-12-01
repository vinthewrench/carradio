# PiCar - Raspberry Pi Car Radio Project  

[PiCar](https://github.com/vinthewrench/carradio) is my attempt at building a Raspberry Pi based replacement radio for my Jeep JK.  I have (probably still are) written about this project extensively on my blog at [https://vinthewrench.substack.com ](https://vinthewrench.substack.com).

# Features
* VFD display to match the existing decor.
* Simple user interface, maybe just two knobs.
* Radio based on RTL-SDR software radio.
* 4 speaker audio (I am to old to care about subwoofers)
* Allow me to play audio from my iPhone
* Access to Jeep and GM CAN networks.
* GPS receiver.
* External temperature sensor
* Power management (don’t drain my battery)
* A clock that I don’t have to keep setting.

# Articles
Most of the story is documented under the PiCar - Raspberry Pi Car Radio Project:
* [Part 1](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project) - Why would anyone sane do this?
* [Part 2](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-6e7) - The Hardware.
* [Part 3](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-132) - 1-wire temperature sensors
* [Part 4](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-baf) - An overview of the Software
* [Part 5](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-f0b) - GPS, or where the heck are we?
* [Part 6](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-ca8) - Can you hear me now? - Raspberry Pi Audio.
* [Part 7](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-237) - Software Defined Radio.
* [Part 8](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-728) - Who ate all the CPU?
* [Part 9](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-5da) - Play that funky music ... 

In addition I talk about the various pieces of hardware I constructed:
* [Rings and Knobs](https://vinthewrench.substack.com/p/rings-and-knobs) - Rotary encoder and LED rings
* [Power Management part 1](https://vinthewrench.substack.com/p/building-a-simple-safe-shutdown-switch) - Safe shutoff switch
* [Power Management part 2](https://vinthewrench.substack.com/p/can-you-turn-your-raspberry-pi-on) - Turning on the Radio

A few articles about talking to te CAN bus and Hacking your Car Network
* [Part 1](https://vinthewrench.substack.com/p/hacking-your-car-network-part-1) - ODB vs CAN bus,
* [Part 2](https://vinthewrench.substack.com/p/hacking-your-car-network-part-2) - Connecting Raspberry Pi to CAN
* [Part 3](https://vinthewrench.substack.com/p/hacking-your-car-network-part-3) - Reverse Engineering CAN packets
* [Part 4](https://vinthewrench.substack.com/p/hacking-your-car-network-part-4) - Writing code to talk to CAN
* [Part 5](https://vinthewrench.substack.com/p/hacking-your-car-network-huh) - ODB and older cars

## Demo
<video src='https://www.youtube.com/watch?v=jUyjC5QgD2Y' width=180/>


## Status
PiCar is up and running on my workshop desk, I have tested it a few time in the vehicle but as of yet it hasn't been installed.

#Current build notes
I started out with a fresh of Raspberry Pi OS Lite (Buster) on a SD card. Here is a [blog post](https://vinthewrench.substack.com/p/vinnies-raspberry-pi-setup) on how I typically set up these devices.  As of recent I have taken to using the Raspberry Pi imager to setup te user account and Wifi as SSH key.

##### Always run the upgrade

```bash
$sudo apt update
$sudo apt-get upgrade
```

##### Install the LLVM toolchain

I chose to use [LLVM 14.0.6](https://github.com/llvm/llvm-project/releases/) as my last reliable build system. I had problems with version 15.
```bash
#download a copy of the tar file
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-14.0.6/clang+llvm-14.0.6-armv7a-linux-gnueabihf.tar.xz

#unpack the file into a directory. - this takes a few minutes.
tar -xvf clang*.tar.xz

#remove the tar file
rm *.tar.xz

#rename the directory to something more reasonable 
mv clang+llvm-14.0.6-armv7a-linux-gnueabihf/ clang_14.0.6/

#move it to /usr/local
sudo mv clang_14.0.6/ /usr/local/

#setup the paths ( btw copy these to your .bashrc file to make them permanent)
export PATH=/usr/local/clang_14.0.6/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/clang_14.0.6/lib:$LD_LIBRARY_PATH

#test the install
clang++ --version
```

##### Install Cmake

I have been using the Cmake system to generate the build files in my projects. [Here are some simple instructions](https://lindevs.com/install-cmake-on-raspberry-pi/) for installing CMake on Raspberry Pi.

```bash
#install cmake
sudo apt install -y cmake

#check the version
cmake --version
```

##### Install Git

As part of the development process, you will need a version of the Git distributed version control system. Even if it’s installed it probably a good idea to update to the latest version

```bash
sudo apt-get install git-core
```

##### Expand the Filesystem
 run the raspi-config and select  Advanced Options / Expand the Filesystem
 then it's a good time to reboot

##### Update the configuration file 

here is a copy of what I am using in my  /boot/config.txt  file:
``` 
# For more options and information see
# http://rpf.io/configtxt
# Some settings may impact device functionality. See link above for details

# Uncomment some or all of these to enable the optional hardware interfaces
dtparam=i2c_arm=on
#dtparam=i2c_arm_baudrate=50000

#dtparam=i2s=on
#for waveshare 2-CH CAN HAT
dtparam=spi=on
dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=25
dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=23

#DS3231 RTC
dtoverlay=i2c-rtc,ds3231

#shutdown/start
dtoverlay=gpio-shutdown,gpio_pin=17,active_low=1,gpio_pull=up,debounce=1000
dtoverlay=gpio-poweroff,gpiopin=16


#kill bluetooth
dtoverlay=disable-bt

# Additional overlays and parameters are documented /boot/overlays/README

# Enable audio (loads snd_bcm2835)
dtparam=audio=on


# Disable compensation for displays with overscan
disable_overscan=1

[cm4]
# Enable host mode on the 2711 built-in XHCI USB controller.
# This line should be removed if the legacy DWC2 controller is required
# (e.g. for USB device mode) or if USB support is not required.
otg_mode=1

[pi4]
# Run as fast as firmware / board allows
arm_boost=1

[all]
enable_uart=1
dtoverlay=i2s-mmap

#uart3 is used for GPS device
dtoverlay=uart3

dtoverlay=argonone,hysteresis=5
dtparam=fantemp0=65

# hook up on-wire for temp sensors
dtoverlay=w1-gpio,gpiopin=6

#shutoff wifi
#dtoverlay=disable-wifi

# fix problem  with USB drives
max_usb_current=1

-- WIFI power save ---

To make this permanent add the following line to /etc/rc.local:
/sbin/iw dev wlan0 set power_save off

# fix serial port for GPS
stty -F /dev/ttyAMA1 38400 -echo
```


##### Setup for CAN network

here is a copy of what I am using in my  /etc/network/interfaces  file:
``` 
# interfaces(5) file used by ifup(8) and ifdown(8)
# Include files from /etc/network/interfaces.d:
source /etc/network/interfaces.d/*

#setup CAN interfaces
auto can0
iface can0 inet manual
   pre-up /sbin/ip link set can0 type can bitrate 125000
   up /sbin/ifconfig can0 up txqueuelen 65536
   down /sbin/ifconfig can0 down

auto can1
iface can1 inet manual
    pre-up /sbin/ip link set can1 type can bitrate 500000
    up /sbin/ifconfig can1 up txqueuelen 65536
    down /sbin/ifconfig can1 down

```
##### A few more libraries 
```bash
#GPIO development 
sudo apt-get install gpiod libgpiod-dev

#CAN utilities for debugging
sudo apt-get install can-utils

#ASLA development libraries 
sudo apt-get install libasound2-de

#SQLITE DEV for ODB datbase, Read Only
sudo apt-get install libsqlite3-dev
```

##### Software Defined Radio Libs
you can read about it here [https://projects.osmocom.org/projects/rtl-sdr/wiki/Rtl-sdr
](https://projects.osmocom.org/projects/rtl-sdr/wiki/Rtl-sdr)
```bash
#Install libUSB
sudo apt-get install libusb-1.0-0-dev

#install and build RTL-SDR
cd
git clone git://git.osmocom.org/rtl-sdr.git
cd rtl-sdr/
mkdir build
cd build
cmake ../ -DINSTALL_UDEV_RULES=ON -DDETACH_KERNEL_DRIVER=ON
make
make install
sudo make install
sudo ldconfig
cd ~
sudo cp ./rtl-sdr/rtl-sdr.rules /etc/udev/rules.d/

#reboot now
sudo reboot now

 # optional install rtl-433  
sudo apt-get install rtl-433
```

##### Setup ALSA for audio device

I am using a  [StarTech ICUSBAUDIO7D 7.1 USB Audio Adapter](https://www.amazon.com/gp/product/B075F5VYG7/ref=ppx_yo_dt_b_asin_title_o01_s01?ie=UTF8&psc=1)

See [https://alsa.opensrc.org/Playing_stereo_on_surround_sound_setup_(Howto)](https://alsa.opensrc.org/Playing_stereo_on_surround_sound_setup_(Howto))

sudo nano /etc/asound.conf
```bash
pcm.duplicate {
    type plug
   slave.pcm surround40   
    slave.channels 4
     route_policy duplicate
}
```
update the /usr/share/alsa/alsa.conf file
```bash
defaults.ctl.card 1
defaults.pcm.card 1
```

##### Argon One FAN 
 I am using an [Argon Fan HAT](https://www.argon40.com/products/argon-fan-hat) for cooling) but for software I am  the [Argon One Daemon](https://gitlab.com/DarkElvenAngel/argononed) from DarkElvenAngel.

```bash
cd
git clone https://gitlab.com/DarkElvenAngel/argononed.git
cd argononed
./install

#test it 
argonone-cli --fan 100 --manual --commit

#turn it off
argonone-cli --fan 0 --manual --commit

#read status 
argonone-cli --decode
```
 BUT since I am also using GPIO4 for to talk to my GPS device, I have had to remove the push button function.

 edit the  src/argonone.dts and remove fragment@2 section and then do the .`/install` again.

#####Disable the "fake hwclock" which interferes with the 'real' hwclock

```
sudo apt-get -y remove fake-hwclock
sudo update-rc.d -f fake-hwclock remove
sudo systemctl disable fake-hwclock
```

Read the Hardware Clock.
`sudo hwclock -r`

Check result with `timedatectl` status 

```
timedatectl status
```

 ##### AirPlay functionality 
 I wrote about this pretty extensively in (part 9](https://vinthewrench.substack.com/p/picar-raspberry-pi-car-radio-project-5da).

 I build shairport-sync as a pipe output only module and it gets spawned from carradio directly, so it doesn't have to be a systemd. [Mike has lots of build information on his github site.])https://github.com/mikebrady/shairport-sync)

 ```bash
cd
#clone the repository
git clone https://github.com/mikebrady/shairport-sync.git
cd shairport-sync/

# Get Tools and Libraries
 sudo apt install --no-install-recommends build-essential git autoconf automake libtool     libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev libsoxr-dev   libplist-dev libsodium-dev libavutil-dev libavcodec-dev libavformat-dev uuid-dev libgcrypt-dev xxd
 
 #build it
autoreconf -fi
./configure  --with-avahi --with-ssl=openssl --with-metadata --with-pipe 
make 

#copy it to /usr/local/bin/
sudo cp shairport-sync /usr/local/bin/
 ```

In addition I customize shairpoint-sync for my device

`sudo nano  /usr/local/etc/shairport-sync.conf`

```
general =
{
name = "Vinnie's Jeep";
ignore_volume_control = "yes";
volume_max_db = -3.00;  
}

metadata =
{
include_cover_art = "no"; 
}
```

##### The CarRadio App

All this is why we came here.

 ```bash
cd
#clone the repository
git clone https://github.com/vinthewrench/carradio.git
cd carradio

#build it
mkdir build
cd build
cmake ..
make

# test it
bin/carradio
 ```

make it a system service for auto run on boot

copy this into  /etc/init.d/carradio

 ```bash
#!/bin/sh
### BEGIN INIT INFO

# Provides:carradio
# Required-Start:$local_fs $network $remote_fs $syslog  +shairport-sync
# Required-Stop:$local_fs $network $remote_fs $syslog
# Default-Start:2 3 4 5
# Default-Stop:0 1 6
# Short-Description: carradio
# Description: car radio auto start after boot
### END INIT INFO

case "$1" in
    start)
        echo "Starting carradio"
        cd /home/vinthewrench/carradio/build/bin
        sudo nohup /home/vinthewrench/carradio/build/bin/carradio &
         ;;
    stop)
        echo "Stopping carradio"
         sudo killall carradio
         ;;
    *)
        echo "Usage: service carradio start|stop"
        exit 1
        ;;
esac
exit 0
 ```

 ```bash
sudo chmod +x /etc/init.d/carradio
sudo update-rc.d carradio defaults

# to remove it
#sudo update-rc.d  -f carradio remove
 ```
`sudo service carradio start # start the service`
`sudo service carradio stop # stop the service`
`sudo service carradio restart # restart the service`
`sudo service carradio status # check service status`

#### Setup for Wi-Fi server

A number of packages to enable the Pi to work as a WiFi base station are needed:

```bash
sudo apt-get install hostapd isc-dhcp-server
```

Disable both of these services from starting at boot time (this is because we will launch them sequentially later on):

```
sudo systemctl unmask hostapd
sudo systemctl disable hostapd
sudo systemctl disable isc-dhcp-server
```

#### Configure HostAPD

Configure `hostapd` by creating `/etc/hostapd/hostapd.conf` with the following contents which will set up an open network with the name Jeep. You might wish to change the name:

```
# Thanks to https://wiki.gentoo.org/wiki/Hostapd#802.11b.2Fg.2Fn_triple_AP

# The interface used by the AP
interface=wlan0

# This is the name of the network -- yours may be different
ssid=BMW

# "g" simply means 2.4GHz band
hw_mode=g

# Channel to use
channel=11

# Limit the frequencies used to those allowed in the country
ieee80211d=1

# The country code
country_code=IE

# Enable 802.11n support
ieee80211n=1

# QoS support, also required for full speed on 802.11n/ac/ax
wmm_enabled=1
```

Note that, since the car network is isolated from the Internet, you don't really need to secure it with a password.  -- is this true?

#### Configure DHCP server

First, replace the contents of `/etc/dhcp/dhcpd.conf` with this:

```
subnet 10.0.10.0 netmask 255.255.255.0 {
     range 10.0.10.5 10.0.10.150;
     #option routers <the-IP-address-of-your-gateway-or-router>;
     #option broadcast-address <the-broadcast-IP-address-for-your-network>;
}
```

Second, modify the `INTERFACESv4` entry at the end of the file `/etc/default/isc-dhcp-server` to look as follows:

```
INTERFACESv4="wlan0"
INTERFACESv6=""
```

### Set up the Startup Sequence

Configure the startup sequence by adding commands to `/etc/rc.local` to start `hostapd` and the `dhcp` server and then to start `shairport-sync` automatically after startup. Its contents should look like this:

```
#!/bin/sh -e
#
# rc.local
#
# This script is executed at the end of each multiuser  # By default this script does nothing.

/sbin/iw dev wlan0 set power_save off
/usr/sbin/hostapd -B -P /run/hostapd.pid /etc/hostapd/hostapd.conf
/sbin/ip addr add 10.0.10.1/24 dev wlan0
/bin/sleep 1
/bin/systemctl start isc-dhcp-server
exit 0
```

### Final Steps

Up to now, if you reboot the Pi, it will reconnect to your WiFi network, ignoring the instructions and settings you have given it to act as a base station. That is because the `wlan0` interface is still under the control of the `dhcpcd` service. So, the final step is to instruct the `dhcpcd` service not to manage `wlan0`. To do this, edit `/etc/dhcpcd.conf` and insert the following line at the start:

```
denyinterfaces wlan0
```

From this point on, at least on the Raspberry Pi, if you reboot the machine, it will not reconnect to your network. Instead, it will act as the WiFi base station you have configured with `hostapd` and `isc-dhcp-server`.

### Optimise startup time – Raspberry Pi Specific

This is applicable to a Raspberry Pi only. Some of it may be applicable to other systems, but it has not been tested on them.

There are quite a few services that are not necessary for this setup. Disabling them can improve startup time. Running these commands disables them:

```
sudo systemctl disable systemd-timesyncd.service
sudo systemctl disable keyboard-setup.service
sudo systemctl disable triggerhappy.service
sudo systemctl disable dphys-swapfile.service
#sudo systemctl disable networking.service
```

### To make it real

```
#sudo systemctl disable dhcpcd.service
#sudo systemctl disable wpa_supplicant.service

#edit /etc/dhcpcd.conf and uncomment 
# denyinterfaces wlan0
```

### To restore Network

```
#sudo systemctl enable dhcpcd.service
#sudo systemctl enable wpa_supplicant.service

#edit /etc/dhcpcd.conf and comment 
# denyinterfaces wlan0
```

