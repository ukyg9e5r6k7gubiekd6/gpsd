#include <Python.h>
#include "structmember.h"

typedef struct {
    PyObject_HEAD
    int online;		/* this should really be onstrained to be a boolean */
    PyObject *online_timestamp;
    PyObject *first;
    PyObject *last;
    int number;
} gpsd;

static int
gpsd_traverse(gpsd *self, visitproc visit, void *arg)
{
    if (self->first && visit(self->online_timestamp, arg) < 0)
        return -1;
    if (self->first && visit(self->first, arg) < 0)
        return -1;
    if (self->last && visit(self->last, arg) < 0)
        return -1;

    return 0;
}

static int 
gpsd_clear(gpsd *self)
{
    Py_XDECREF(self->online_timestamp);
    self->online_timestamp = NULL;
    Py_XDECREF(self->first);
    self->first = NULL;
    Py_XDECREF(self->last);
    self->last = NULL;

    return 0;
}

static void
gpsd_dealloc(gpsd* self)
{
    gpsd_clear(self);
    self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
gpsd_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    gpsd *self;

    self = (gpsd *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->online = 0;
	// FIXME: must instantiate a new timestamp object here 

        self->first = PyString_FromString("");
        if (self->first == NULL) {
            Py_DECREF(self);
            return NULL;
          }
        
        self->last = PyString_FromString("");
        if (self->last == NULL) {
            Py_DECREF(self);
            return NULL;
          }

        self->number = 0;
    }

    return (PyObject *)self;
}

static int
gpsd_init(gpsd *self, PyObject *args, PyObject *kwds)
{
    PyObject *first=NULL, *last=NULL;

    static char *kwlist[] = {
	"online",
	"first",
	"last",
	"number",
	NULL};

    if (! PyArg_ParseTupleAndKeywords(args, kwds, "|iOOi", kwlist, 
				      &self->online,
                                      &first, &last, 
                                      &self->number))
        return -1; 

    if (first) {
        Py_XDECREF(self->first);
        Py_INCREF(first);
        self->first = first;
    }

    if (last) {
        Py_XDECREF(self->last);
        Py_INCREF(last);
        self->last = last;
    }

    return 0;
}


static PyMemberDef gpsd_members[] = {
    {"first", T_OBJECT_EX, offsetof(gpsd, first), 0,
     "first name"},
    {"last", T_OBJECT_EX, offsetof(gpsd, last), 0,
     "last name"},
    {"number", T_INT, offsetof(gpsd, number), 0,
     "gpsd number"},
    {NULL}  /* Sentinel */
};

static PyObject *
gpsd_name(gpsd* self)
{
    static PyObject *format = NULL;
    PyObject *args, *result;

    if (format == NULL) {
        format = PyString_FromString("%s %s");
        if (format == NULL)
            return NULL;
    }

    if (self->first == NULL) {
        PyErr_SetString(PyExc_AttributeError, "first");
        return NULL;
    }

    if (self->last == NULL) {
        PyErr_SetString(PyExc_AttributeError, "last");
        return NULL;
    }

    args = Py_BuildValue("OO", self->first, self->last);
    if (args == NULL)
        return NULL;

    result = PyString_Format(format, args);
    Py_DECREF(args);
    
    return result;
}

static PyMethodDef gpsd_methods[] = {
    {"name", (PyCFunction)gpsd_name, METH_NOARGS,
     "Return the name, combining the first and last name"
    },
    {NULL}  /* Sentinel */
};

static PyTypeObject gpsdType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "gpsd.gpsd",             /*tp_name*/
    sizeof(gpsd),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)gpsd_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, /*tp_flags*/
    "gpsd objects",           /* tp_doc */
    (traverseproc)gpsd_traverse,   /* tp_traverse */
    (inquiry)gpsd_clear,           /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    gpsd_methods,             /* tp_methods */
    gpsd_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)gpsd_init,      /* tp_init */
    0,                         /* tp_alloc */
    gpsd_new,                 /* tp_new */
};

static PyMethodDef module_methods[] = {
    {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initgpsd(void) 
{
    PyObject* m;

    if (PyType_Ready(&gpsdType) < 0)
        return;

    m = Py_InitModule3("gpsd", module_methods,
                       "Example module that creates an extension type.");

    if (m == NULL)
      return;

    Py_INCREF(&gpsdType);
    PyModule_AddObject(m, "gpsd", (PyObject *)&gpsdType);
}
