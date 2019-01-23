#!/usr/bin/env python3

""" Example of resolving a service with a known name """

import logging
import sys

from zeroconf import Zeroconf

TYPE = '_WPPS._tcp.local.'
NAME = f'WPP.{TYPE}'

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    if len(sys.argv) > 1:
        assert sys.argv[1:] == ['--debug']
        logging.getLogger('zeroconf').setLevel(logging.DEBUG)

    zeroconf = Zeroconf()

    try:
        print(zeroconf.get_service_info(TYPE, NAME))
    finally:
        zeroconf.close()

