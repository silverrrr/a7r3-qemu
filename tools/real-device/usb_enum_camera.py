#!/usr/bin/env python3
r"""Enumerate the Sony camera's USB descriptors (VID 0x054C).

Prints every configuration/interface/endpoint so we can see whether the camera
exposes more than the MTP interface in the current USB mode.

usage: python usb_enum_camera.py
"""

import sys

import usb.core


def main():
    devs = list(usb.core.find(find_all=True, idVendor=0x054C) or [])
    print('Sony devices: %d' % len(devs))
    for d in devs:
        try:
            print('\n%04x:%04x bus=%s addr=%s %s'
                  % (d.idVendor, d.idProduct, d.bus, d.address,
                     (d.product or '').strip()))
        except Exception as exc:
            print('  descriptor read failed: %r' % (exc,))
            continue
        try:
            for cfg in d:
                print('  cfg %s' % cfg.bConfigurationValue)
                for intf in cfg:
                    print('    intf %d alt=%d class=%02x sub=%02x proto=%02x'
                          % (intf.bInterfaceNumber, intf.bAlternateSetting,
                             intf.bInterfaceClass, intf.bInterfaceSubClass,
                             intf.bInterfaceProtocol))
                    for ep in intf:
                        print('      ep 0x%02x attr=%02x maxpkt=%d'
                              % (ep.bEndpointAddress, ep.bmAttributes,
                                 ep.wMaxPacketSize))
        except Exception as exc:
            print('  config walk failed: %r' % (exc,))
    return 0


if __name__ == '__main__':
    sys.exit(main())
