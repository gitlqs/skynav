#!/usr/bin/env python

import sys

import roslib

pkg = 'skynav_gui'
roslib.load_manifest(pkg)

from rqt_gui.main import Main

main = Main()
sys.exit(main.main(sys.argv, standalone=pkg))

