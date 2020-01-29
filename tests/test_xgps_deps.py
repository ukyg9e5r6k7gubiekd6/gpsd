#!/usr/bin/env python
"""Test imports needed by X11-based tools."""

from __future__ import print_function

import os
import sys

# Keep Gtk from trying to launch X11.
# Pylint seems to defeat this hack, but we can live with that.
os.environ['DISPLAY'] = ''

# pylint: disable=unused-import,wrong-import-position

import cairo

import gi
try:
    gi.require_version('Gtk', '3.0')
except (AttributeError, ValueError):
    # Explain the reason for the exception, then reraise
    print('*** Need PyGObject V3 or later, and Gtk3 ***', file=sys.stderr)
    raise
from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import Pango
