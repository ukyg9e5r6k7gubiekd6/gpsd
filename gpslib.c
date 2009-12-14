/* $Id$ */
/*
 * Python binding for selected libgps library functions
 */
#include <Python.h>

#include <stdio.h>
#include "gps.h"
#include "gpsdclient.h"

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

static PyObject *
gpslib_deg_to_str(PyObject *self, PyObject *args)
{
    int fmt;
    double degrees;

    if (!PyArg_ParseTuple(args, "id", &fmt, &degrees))
	return NULL;
    return Py_BuildValue("s", deg_to_str((enum deg_str_type)fmt, degrees));
}

/* List of functions defined in the module */

static PyMethodDef gpslib_methods[] = {
    {"wgs84_separation",	gpslib_wgs84_separation,	METH_VARARGS,
     PyDoc_STR("Return WGS84 geodetic separation in meters.")},
    {"deg_to_str",      	gpslib_deg_to_str,      	METH_VARARGS,
     PyDoc_STR("String-format a latitude/longitude.")},
    {NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(module_doc,
"Python wrapper for selected libgps library routines.\n\
");

PyMODINIT_FUNC
initgpslib(void)
{
    PyObject *m;

    m = Py_InitModule3("gpslib", gpslib_methods, module_doc);

    PyModule_AddIntConstant(m, "deg_dd", deg_dd);
    PyModule_AddIntConstant(m, "deg_ddmm", deg_ddmm);
    PyModule_AddIntConstant(m, "deg_ddmmss", deg_ddmmss);
}

