This folder is for creating a desktop environment for the TI-99/4A.

The target system is a 32k machine with keyboard interface. We want to support plug-in drivers at a fixed address for joystick or mouse, but consider that after the basics are working. All keys should be hotkeys, we don't want a simple cursor-movement from the keyboard. (Yes for the plugin drivers).

GEM is the basic concept to emulate. By default we will open with a cartridge device only. A 'scan' option in the menu will enumerate all devices by scanning CRU, and create device entries for them. The user should be able to remove or change the icon for the device, and we should have basic icons for cartridge, disk, ramdisk and hard drive.

We don't need to be overly comprehensive, due to memory constraints. We want to be able to open a device and view its directory listing, and select a file from that listing. Once selected, it should be possible to "run" the file. For the first pass, we'll only run PROGRAM files but we should make the code extendable as there are other types. We should also be able to copy or move the file to another device, or view the file onscreen. The file viewer should operate on records for lines, but PROGRAM images do not require viewing.

You will need to understand reading a disk directory on the TI and opening files in per-sector access mode (so as not to require the file types as with OPEN). In addition, special handling for the cartridge is needed to make it appear like a disk device with multiple programs on it. 

Hotkeys should let you jump to a device on the desktop, open it, arrow through the files in it, select it, deselect it, and close the window. Another hotkey for the global menu. If you use a hotkey to select a file, a popup menu should ask what operation to try with it.

Otherwise take inspiration from Gem, particularly on the AtariST, for look and feel.

Write the code for GCC, noting the TI limits of 16 bit ints and avoid 8 bit chars where possible as they are less stable. Define the memory map. For program loading, leave a little room for me to inject the loader, I have a tight loader that runs from scratchpad so it won't conflict.
