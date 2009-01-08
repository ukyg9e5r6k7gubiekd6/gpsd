/* $Id$ */
/*
 * Python binding for selected libgps library functions
 */
#include <Python.h>

#include <stdio.h>
#include "gps.h"

static PyObject *
gpslib_wgs84_separation(PyObject *self, PyObject *args)
{
    const double lat, lon;
    double sep;

    if (!PyArg_ParseTuple(args, "dd", &lat, &lon))
	return NULL;
    sep = wgs84_separation(lat, lon);
    return Py_BuildValue("d", sep);
}

/* List of functions defined in the module */

static PyMethodDef gpslib_methods[] = {
    {"wgs84_separation",	gpslib_wgs84_separation,	METH_VARARGS,
     PyDoc_STR("Return WGS84 geidetic separation in meters.")},
    {NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(module_doc,
"Python wrapper for selected libgps library routines.\n\
");

PyMODINIT_FUNC
initgpslib(void)
{
    Py_InitModule3("gpslib", gpslib_methods, module_doc);
}
