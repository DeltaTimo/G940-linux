# G940-linux
Improvements to Linux kernel support for
[Logitech Flight System G940](https://support.logitech.com/en_gb/product/flight-system-g940)

This device has partial Linux support since 2010, but with bugs including some axes conflated/missing.

I have made both some relatively uncontroversial improvements,
[submitted upstream](https://patchwork.kernel.org/project/linux-input/list/?series=90297)
(fix axis/button mappings, support LEDs, remove hard autocenter) and some more experimental changes which are only here, to
support additional force feedback effects by including
[ff-memless-next from Michal Malý](https://patchwork.kernel.org/project/linux-input/list/?q=ff-memless-next&archive=both),
a sort of emulation layer for periodic and rumble effects which was submitted upstream circa 2014, never merged, but it still
works great for this.

The repository contains a replacement for the **hid_logitech** module called **hid_logitech_next** and a support module
called **ff_memless_next**.

## Support

Module is tested with kernel version `5.4.0`. Other kernels *might* work, but this is not tested. In any case, use of this module is at **your own risk**.

Module may be installed manually or by creating a DKMS package (for Ubuntu, Debian etc.).

## Build and install kernel module without packackage

This installation procedure must be run whenever the kernel has been upgraded.

*Preconditions: The repository has been cloned from GitHub, and current directory is the top level source directory.*

1.  Kernel headers for running kernel must be present in `/lib/modules/(VERSION)/`
2.  Compile and install module: `cd drivers/ && ./local_make.sh modules && sudo ./local_make.sh modules_install`
3.  Blacklist the old **hid_logitech** module: `sudo echo "blacklist hid_logitech" > /etc/modprobe.d/blacklist-hid-logitech.conf`
4.  Unload old module if loaded: `sudo rmmod hid_logitech`

## Build .deb DKMS package (for Debian, Ubuntu etc.)

The DKMS package will ensure that a new module is automatically built whenever the kernel is upgraded.

*Preconditions: The repository has been cloned from GitHub, and current directory is the top level source directory.*

1.  Install kernel headers package (`linux-headers-(VERSION)`)
2.  Install dependencies: `sudo apt install dkms debhelper dpkg-dev`
3.  Build package: `dpkg-buildpackage`
4.  Install package: `sudo dpkg -i ../g940-dkms*.deb`

NOTE : Other packages might be required for building, install those who are missing if step 3 fails.

## Check that correct module is loaded

1.  (re)connect device
2.  Run and verify that **hid_logitech_next** exists, but not **hid_logitech**: `lsmod | grep hid`
3.  If the old module is still loaded, perform a restart.
