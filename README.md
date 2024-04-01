
# DireWolf-L

### Decoded Information from Radio Emissions for Windows Or Linux Fans (Lightweight)

DireWolf is a modern software replacement for the old 1980's style TNC built with special hardware.

In the early days of Amateur Packet Radio, it was necessary to use an expensive "Terminal Node Controller" (TNC) with specialized hardware.  Those days are gone.  You can now get better results at lower cost by connecting your radio to the "soundcard" interface of a computer and using software to decode the signals.

Dire Wolf includes [FX.25](https://en.wikipedia.org/wiki/FX.25_Forward_Error_Correction) which adds Forward Error Correction (FEC) in a way that is completely compatible with existing systems.  If both ends are capable of FX.25, your information will continue to get through under conditions where regular AX.25 is completely useless. This was originally developed for satellites and is now seeing widespread use on HF.

It can also be used as a virtual TNC for other applications such as [APRSIS32](http://aprsisce.wikidot.com/), [Xastir](http://xastir.org/index.php/Main_Page), [APRS-TW](http://aprstw.blandranch.net/), [YAAC](http://www.ka2ddo.org/ka2ddo/YAAC.html), [PinPoint APRS](http://www.pinpointaprs.com/), [UI-View32](http://www.ui-view.net/),[UISS](http://users.belgacom.net/hamradio/uiss.htm), [Linux AX25](http://www.linux-ax25.org/wiki/Main_Page), [SARTrack](http://www.sartrack.co.nz/index.html), [Winlink Express (formerly known as RMS Express, formerly known as Winlink 2000 or WL2K)](http://www.winlink.org/RMSExpress), [BPQ32](http://www.cantab.net/users/john.wiseman/Documents/BPQ32.html), [Outpost PM](http://www.outpostpm.org/), [Ham Radio of Things](https://github.com/wb2osz/hrot), [Packet Compressed Sensing Imaging (PCSI)](https://maqifrnswa.github.io/PCSI/), and many others.
 

### DireWolf-L includes: 

- **KISS Interface (TCP/IP, serial port, Bluetooth)**

    DireWolf can be used as a virtual TNC for applications such as [APRSIS32](http://aprsisce.wikidot.com/), [Xastir](http://xastir.org/index.php/Main_Page), [APRS-TW](http://aprstw.blandranch.net/), [YAAC](http://www.ka2ddo.org/ka2ddo/YAAC.html), [PinPoint APRS](http://www.pinpointaprs.com/), [UI-View32](http://www.ui-view.net/),[UISS](http://users.belgacom.net/hamradio/uiss.htm), [Linux AX25](http://www.linux-ax25.org/wiki/Main_Page), [SARTrack](http://www.sartrack.co.nz/index.html), [Winlink Express (formerly known as RMS Express, formerly known as Winlink 2000 or WL2K)](http://www.winlink.org/RMSExpress), [BPQ32](http://www.cantab.net/users/john.wiseman/Documents/BPQ32.html), [Outpost PM](http://www.outpostpm.org/), [Ham Radio of Things](https://github.com/wb2osz/hrot), [Packet Compressed Sensing Imaging (PCSI)](https://maqifrnswa.github.io/PCSI/), and many others.


### Other features:

- **Modems:**

    300 bps AFSK for HF

    1200 bps AFSK most common for VHF/UHF

- **Compatible with Software Defined Radios such as gqrx, rtl_fm, and SDR#.**

- **Concurrent operation with up to 3 soundcards and 6 radios.**

### Portable & Open Source: 

- **Runs on Windows, Linux (PC/laptop, Raspberry Pi, etc.), Mac OSX.**


## Installation 

### Windows 

Build from source.  For more details see the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).


### Linux 

First you will need to install some software development packages using different commands depending on your flavor of Linux.
In most cases, the first few  will already be there and the package installer will tell you that installation is not necessary.

On Debian / Ubuntu / Raspbian / Raspberry Pi OS:

    sudo apt-get install git
    sudo apt-get install gcc
    sudo apt-get install g++
    sudo apt-get install make
    sudo apt-get install cmake
    sudo apt-get install libasound2-dev
    sudo apt-get install libudev-dev

Or on Red Hat / Fedora / CentOS:

    sudo yum install git
    sudo yum install gcc
    sudo yum install gcc-c++
    sudo yum install make
    sudo yum install alsa-lib-devel
    sudo yum install libudev-devel

CentOS 6 & 7 currently have cmake 2.8 but we need 3.1 or later.
First you need to enable the EPEL repository.  Add a symlink if you don't already have the older version and want to type cmake rather than cmake3.

    sudo yum install epel-release
	sudo rpm -e cmake
	sudo yum install cmake3
	sudo ln -s /usr/bin/cmake3 /usr/bin/cmake

Then on any flavor of Linux:

	cd ~
	git clone https://www.github.com/wb2osz/direwolf
	cd direwolf
	mkdir build && cd build
	cmake ..
	make -j4
	sudo make install
	make install-conf

This gives you the latest development version.  Leave out the "git checkout dev" to get the most recent stable release.

For more details see the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).  Special considerations for the Raspberry Pi are found in **Raspberry-Pi-APRS.pdf**

### Macintosh OS X 

Read the **User Guide** in the [**doc** directory](https://github.com/wb2osz/direwolf/tree/master/doc).   It is more complicated than Linux.

If you have problems,  post them to the [Dire Wolf packet TNC](https://groups.io/g/direwolf) discussion group.

## Join the conversation
 
[Dire Wolf Software TNC](https://groups.io/g/direwolf) 

The github "issues" section is for reporting software defects and enhancement requests.  It is NOT a place to ask questions or have general discussions.  Please use one of the locations above.
