/* $Id$ */
/*
 * Python binding for the packet.c module.
 */
#include <Python.h>

#include "packet.c"

void gpsd_report(int errlevel UNUSED, const char *fmt, ... )
/* stub logger -- we should allow redirecting this */
{
    va_list ap;

    va_start(ap, fmt);
    (void)vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static PyObject *ErrorObject;

typedef struct {
	PyObject_HEAD
	struct gps_packet_t getter;
} GetterObject;

static PyTypeObject Getter_Type;

#define GetterObject_Check(v)	((v)->ob_type == &Getter_Type)

static GetterObject *
newGetterObject(PyObject *arg)
{
    GetterObject *self;
    self = PyObject_New(GetterObject, &Getter_Type);
    if (self == NULL)
	return NULL;
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

    if (!PyArg_ParseTuple(args, "i", &fd))
        return NULL;

    type = packet_get(fd, &self->getter);

    return Py_BuildValue("(i, s)", type, self->getter.outbuffer);
}

static PyObject *
Getter_reset(GetterObject *self)
{
    packet_reset(&self->getter);
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
        0,                      /*tp_doc*/
        0,                      /*tp_traverse*/
        0,                      /*tp_clear*/
        0,                      /*tp_richcompare*/
        0,                      /*tp_weaklistoffset*/
        0,                      /*tp_iter*/
        0,                      /*tp_iternext*/
        0,         		/*tp_methods*/
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

/* List of functions defined in the module */

static PyMethodDef gpspacket_methods[] = {
    {"new",		gpspacket_new,		METH_VARARGS,
     PyDoc_STR("new() -> new packet-getter object")},
    {NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(module_doc,
"Python binding of the libgpsd module for recognizing GPS packets.");

PyMODINIT_FUNC
initgpspacket(void)
{
    PyObject *m;

    /* Finalize the type object including setting type of the new type
     * object; doing it here is required for portability to Windows 
     * without requiring C++. */
    if (PyType_Ready(&Getter_Type) < 0)
	return;

    /* Create the module and add the functions */
    m = Py_InitModule3("gpspacket", gpspacket_methods, module_doc);

    /* Add some symbolic constants to the module */
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
