/* $Id$ */
/*
 * Python binding for the packet.c module.
 */
#include <Python.h>

#include <stdio.h>
#include "gpsd_config.h"
#include "gpsd.h"

static PyObject *ErrorObject = NULL;

static PyObject *report_callback = NULL;

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
{
    char buf[BUFSIZ];
    PyObject *args, *result;
    va_list ap;

    if (!report_callback)   /* no callback defined, exit early */
	return;	
    
    if (!PyCallable_Check(report_callback)) {
	PyErr_SetString(ErrorObject, "Cannot call Python callback function");
	return;
    }

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    args = Py_BuildValue("(s)", buf);
    if (!args)
	return;

    result = PyObject_Call(report_callback, args, NULL);
    Py_DECREF(args);
    if (!result)
	return;
}

static PyTypeObject Getter_Type;

typedef struct {
	PyObject_HEAD
	struct gps_packet_t getter;
} GetterObject;

#define GetterObject_Check(v)	((v)->ob_type == &Getter_Type)

static GetterObject *
newGetterObject(PyObject *arg)
{
    GetterObject *self;
    self = PyObject_New(GetterObject, &Getter_Type);
    if (self == NULL)
	return NULL;
    memset(&self->getter, 0, sizeof(struct gps_packet_t));
    packet_reset(&self->getter);
    return self;
}

/* Getter methods */

static int
Getter_init(GetterObject *self)
{
    packet_reset(&self->getter);
    return 0;
}
static PyObject *
Getter_get(GetterObject *self, PyObject *args)
{
    int fd;
    ssize_t type;

    if (!PyArg_ParseTuple(args, "i;missing or invalid file descriptor argument to gpspacket.get", &fd))
        return NULL;

    type = packet_get(fd, &self->getter);
    if (PyErr_Occurred())
	return NULL;

    return Py_BuildValue("(i, s)", type, self->getter.outbuffer);
}

static PyObject *
Getter_reset(GetterObject *self)
{
    packet_reset(&self->getter);
    if (PyErr_Occurred())
	return NULL;
    return 0;
}

static void
Getter_dealloc(GetterObject *self)
{
    PyObject_Del(self);
}

static PyMethodDef Getter_methods[] = {
    {"get",	(PyCFunction)Getter_get,	METH_VARARGS,
    		PyDoc_STR("Get a packet from a file descriptor.")},
    {"reset",	(PyCFunction)Getter_reset,	METH_NOARGS,
    		PyDoc_STR("Reset the packet getter to ground state.")},
    {NULL,		NULL}		/* sentinel */
};

static PyObject *
Getter_getattr(GetterObject *self, char *name)
{
    return Py_FindMethod(Getter_methods, (PyObject *)self, name);
}

PyDoc_STRVAR(Getter__doc__,
"GPS packet getter object\n\
\n\
Fetch a single packet from file descriptor");

static PyTypeObject Getter_Type = {
	/* The ob_type field must be initialized in the module init function
	 * to be portable to Windows without using C++. */
	PyObject_HEAD_INIT(NULL)
	0,			/*ob_size*/
	"gpspacket.getter",	/*tp_name*/
	sizeof(GetterObject),	/*tp_basicsize*/
	0,			/*tp_itemsize*/
	/* methods */
	(destructor)Getter_dealloc, /*tp_dealloc*/
	0,			/*tp_print*/
	(getattrfunc)Getter_getattr,			/*tp_getattr*/
	0,			/*tp_setattr*/
	0,			/*tp_compare*/
	0,			/*tp_repr*/
	0,			/*tp_as_number*/
	0,			/*tp_as_sequence*/
	0,			/*tp_as_mapping*/
	0,			/*tp_hash*/
        0,                      /*tp_call*/
        0,                      /*tp_str*/
        0,                      /*tp_getattro*/
        0,                      /*tp_setattro*/
        0,                      /*tp_as_buffer*/
        Py_TPFLAGS_DEFAULT,     /*tp_flags*/
        Getter__doc__,          /*tp_doc*/
        0,                      /*tp_traverse*/
        0,                      /*tp_clear*/
        0,                      /*tp_richcompare*/
        0,                      /*tp_weaklistoffset*/
        0,                      /*tp_iter*/
        0,                      /*tp_iternext*/
        Getter_methods,		/*tp_methods*/
        0,                      /*tp_members*/
        0,                      /*tp_getset*/
        0,                      /*tp_base*/
        0,                      /*tp_dict*/
        0,                      /*tp_descr_get*/
        0,                      /*tp_descr_set*/
        0,                      /*tp_dictoffset*/
        (initproc)Getter_init,	/*tp_init*/
        0,                      /*tp_alloc*/
        0,                      /*tp_new*/
        0,                      /*tp_free*/
        0,                      /*tp_is_gc*/
};

/* Function of no arguments returning new Getter object */

static PyObject *
gpspacket_new(PyObject *self, PyObject *args)
{
    GetterObject *rv;

    if (!PyArg_ParseTuple(args, ":new"))
	return NULL;
    rv = newGetterObject(args);
    if (rv == NULL)
	return NULL;
    return (PyObject *)rv;
}

PyDoc_STRVAR(register_report__doc__,
"register_report(callback)\n\
\n\
callback must be a callable object expecting a string as parameter.");

static PyObject *
register_report(GetterObject *self, PyObject *args)
{
    PyObject *callback = NULL;

    if (!PyArg_ParseTuple(args, "O:register_report", &callback))
	return NULL;

    if (!PyCallable_Check(callback)) {
	PyErr_SetString(PyExc_TypeError, "First argument must be callable");
	return NULL;
    }

    if (report_callback) {
	Py_DECREF(report_callback);
	report_callback = NULL;
    }

    report_callback = callback;
    Py_INCREF(report_callback);

    Py_INCREF(Py_None);
    return Py_None;
}


/* List of functions defined in the module */

static PyMethodDef gpspacket_methods[] = {
    {"new",		gpspacket_new,		METH_VARARGS,
     PyDoc_STR("new() -> new packet-getter object")},
    {"register_report", (PyCFunction)register_report, METH_VARARGS,
			register_report__doc__},
    {NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(module_doc,
"Python binding of the libgpsd module for recognizing GPS packets.");

PyMODINIT_FUNC
initgpspacket(void)
{
    PyObject *m;

    if (PyType_Ready(&Getter_Type) < 0)
	return;

    /* Create the module and add the functions */
    m = Py_InitModule3("gpspacket", gpspacket_methods, module_doc);

    if (ErrorObject == NULL) {
	ErrorObject = PyErr_NewException("gpspacket.error", NULL, NULL);
	if (ErrorObject == NULL)
	    return;
    }
    Py_INCREF(ErrorObject);
    PyModule_AddObject(m, "error", ErrorObject);

    PyModule_AddIntConstant(m, "BAD_PACKET", BAD_PACKET);
    PyModule_AddIntConstant(m, "COMMENT_PACKET", COMMENT_PACKET);
    PyModule_AddIntConstant(m, "NMEA_PACKET", NMEA_PACKET);
    PyModule_AddIntConstant(m, "SIRF_PACKET", SIRF_PACKET);
    PyModule_AddIntConstant(m, "ZODIAC_PACKET", ZODIAC_PACKET);
    PyModule_AddIntConstant(m, "TSIP_PACKET", TSIP_PACKET);
    PyModule_AddIntConstant(m, "EVERMORE_PACKET", EVERMORE_PACKET);
    PyModule_AddIntConstant(m, "ITALK_PACKET", ITALK_PACKET);
    PyModule_AddIntConstant(m, "RTCM_PACKET", RTCM_PACKET);
    PyModule_AddIntConstant(m, "GARMIN_PACKET", GARMIN_PACKET);
}
