#!/usr/bin/env python
from __future__ import print_function

import sys

import cairo

import gi
try:
    gi.require_version('Gtk', '3.0')
except (AttributeError, ValueError):
    # Explain the reason for the exception, then reraise
    print('*** Need PyGObject V3 or later, and Gtk3 ***', file=sys.stderr)
    raise
from gi.repository import GObject    # pylint: disable=wrong-import-position
# Skip the imports that require X11
### from gi.repository import Gtk    # pylint: disable=wrong-import-position
### from gi.repository import Gdk    # pylint: disable=wrong-import-position
### from gi.repository import Pango  # pylint: disable=wrong-import-position
