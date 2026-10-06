# Wine driver for Hantek USB oscilloscope

Tested with Hantek 6254BD.

While I was working on getting this ready for wine 11, I found out, that it was **no longer necessary.**
wine-11.0 supports it **out of the box** (plus the winetricks below, plus udev rule below).

## Install original Hantek software

- Download the latest version

E.g. from http://www.hantek.com/download

- Unpack and install it into wine

```
$ cd /tmp
$ unrar Hantek-6000_Ver2.2.7_D20220325.rar
$ cd Hantek-6000_Ver2.2.7_D20220325
$ wine Setup.EXE
```

- Enable winetricks, because the Hantek software requires a DLL that does not come with wine.

Details can be found here
https://gitlab.winehq.org/wine/wine/-/wikis/Winetricks
and here
https://github.com/Winetricks/winetricks

The latest release is available at
https://raw.githubusercontent.com/Winetricks/winetricks/master/src/winetricks.
Right-click on that link and use 'Save As' to save a fresh copy into a directory of your choice, that is mentioned in your $PATH. Set execute rights for it.

- Add required DLL

Carefully watch for warnings and install additional packages, that winetricks may request, e.g.:
*warning: Cannot find cabextract.  Please install it (e.g. 'sudo apt install cabextract' or 'sudo yum install cabextract').*
Then repeat.

```
$ winetricks mfc42
```

## Wine driver installation

I haven't been able to find a way to build an "out-of-tree" driver so you need to download full sources for your wine version

- check your wine version:
```
$ wine --version
wine-6.0.3 (Ubuntu 6.0.3~repack-1)
or
wine-11.0
```
- clone corresponding wine version, apply patch and install driver:
```
git clone --branch wine-11.0 --depth=1 https://github.com/wine-mirror/wine

cd wine

git clone https://github.com/danielkucera/hantek-wine-driver dlls/hantek.sys

patch -p1 < dlls/hantek.sys/0001-add-hantek.sys.patch

cd dlls/hantek.sys/

./build.sh
```

- Connect your USB oscilloscope

- Check access rights
Find your device. With a Hantek 6254BD connected, you would see something like that:
```
$ lsusb
...
Bus 002 Device 026: ID 04b5:6cde ROHM LSI Systems USA, LLC DSO Device
...
```

Use the bus and device IDs, that you saw above, in the following command:
```
$ ls -l /dev/bus/usb/002/026
crw-rw-r-- 1 root root 189, 153  6. Okt 12:27 /dev/bus/usb/002/026

```
In case of missing rights, like in the above example:

- Create a udev rule, that sets access rights upon plugging the device in

Verify, that you are a member of group plugdev or choose another group below.
```
$ id
```

```
$ echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="04b5", ATTRS{idProduct}=="6cde", GROUP="plugdev", MODE="0664"' \
    | sudo tee /etc/udev/rules.d/60-hantek.rules
```

Unplug your oscilloscope, then plug it in again. Verify access rights.
Now you should see something like this. Note, that the device ID probably has changed.
```
$ lsusb
...
Bus 002 Device 028: ID 04b5:6cde ROHM LSI Systems USA, LLC DSO Device
...
$ ls -l /dev/bus/usb/002/028
crw-rw-r-- 1 root plugdev 189, 155  6. Okt 12:51 /dev/bus/usb/002/028
```

- Try to run `wine Scope.exe`
```
wine start 'C:\Program Files (x86)\Hantek6000\Scope.exe'
```

## Debugging

- To see debug info:
```
WINEDEBUG=trace+hantek  wine start 'C:\Program Files (x86)\Hantek6000\Scope.exe'
```

- For even more info (warnings) and testing:
```
wineserver -k
WINEDEBUG=+hantek       wine start 'C:\Program Files (x86)\Hantek6000\Scope.exe'
```

