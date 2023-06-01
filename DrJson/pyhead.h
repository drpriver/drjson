//
// Copyright © 2021-2023, David Priver
//
#ifndef PYHEAD_H
#define PYHEAD_H
#define PY_SSIZE_T_CLEAN

// Python's pytime.h triggers a visibility warning (at least on windows). We really don't care.
#ifdef __clang___
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvisibility"
#endif

#define PY_LIMITED_API

#if defined(_WIN32) && defined(_DEBUG)
// Windows release of python only ships with release lib, but the _DEBUG macro
// enables a linker comment to link against the debug lib, which will fail at link time.
// The offending header is "pyconfig.h" in the python include directory.
// So undef it.
#undef _DEBUG
#include <Python.h>
#define _DEBUG

#else
// The proper way to include this on macos when compiling against a framework
// is to do <Python/Python.h>, but cmake and meson don't use the framework
// argument, they instead set include paths. Very annoying.
#include <Python.h>
#endif

#ifdef __clang___
#pragma clang diagnostic pop
#endif


#if PY_MAJOR_VERSION < 3
#error "Only python3 or better is supported"
#endif
#if PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION < 6
#error "Only python 3.6 or better is supported"
#endif

#if PY_MINOR_VERSION <= 7
// Python 3.7 has a bug in PyStructSequence_NewType, so just use
// a namedtuple instead.
//   https://bugs.python.org/issue28709
static inline
PyTypeObject*
my_PyStructSequence_NewType(PyStructSequence_Desc* desc){
    PyObject* collections = NULL;
    PyObject* namedtup = NULL;
    PyObject* fieldnames = NULL;
    PyObject* args = NULL;
    PyObject* result = NULL;
    collections = PyImport_ImportModule("collections");
    if(!collections) return NULL;
     namedtup = PyObject_GetAttrString(collections, "namedtuple");
    if(!namedtup) goto fail;
    fieldnames = PyList_New(0);
    if(!fieldnames) goto fail;
    for(PyStructSequence_Field* field = desc->fields;field->doc; field++){
        PyObject* fieldname = PyUnicode_FromString(field->name);
        if(!fieldname) goto fail;
        int err = PyList_Append(fieldnames, fieldname);
        Py_XDECREF(fieldname);
        if(err) goto fail;
    }
    args = Py_BuildValue("(sO)", desc->name, fieldnames);
    if(!args) goto fail;
    result = PyObject_CallObject(namedtup, args);
    if(!result) goto fail;
    assert(PyType_Check(result));

    Py_XDECREF(collections);
    Py_XDECREF(namedtup);
    Py_XDECREF(fieldnames);
    Py_XDECREF(args);
    return (PyTypeObject*)result;

fail:
    Py_XDECREF(collections);
    Py_XDECREF(namedtup);
    Py_XDECREF(fieldnames);
    Py_XDECREF(args);
    return NULL;
}
#else
static inline
PyTypeObject*
my_PyStructSequence_NewType(PyStructSequence_Desc *desc){
    return PyStructSequence_NewType(desc);
}
#endif

// inspect.py doesn't support native functions having type annotations in the
// docstring yet.
// If it encounters annotations it just gives you the horrible (...) signature.
// If I feel like it, I'll submit a patch allowing annotations as it is trivial.
#if 0
#define PYSIG(typed, untyped) typed
#else
#define PYSIG(typed, untyped) untyped
#endif


#if PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION < 10
// Shim for older pythons.
static inline
int
PyModule_AddObjectRef(PyObject* mod, const char* name, PyObject* value){
    int result = PyModule_AddObject(mod, name, value);
    if(result == 0){ // 0 is success, so above call stole our ref
        Py_INCREF(value);
    }
    return result;
}
static inline
PyObject*
Py_XNewRef(PyObject* o){
    if(!o) return NULL;
    Py_INCREF(o);
    return o;
}
static inline
PyObject*
Py_NewRef(PyObject* o){
    return Py_XNewRef(o);
}
#endif
#if PY_MAJOR_VERSION == 3 && PY_MINOR_VERSION < 9
// shim
static inline
PyObject*
PyObject_CallOneArg(PyObject* callable, PyObject* arg){
    PyObject* tup = PyTuple_Pack(1, arg); // new ref
    if(!tup) return NULL;
    PyObject* result = PyObject_CallObject(callable, tup);
    Py_DECREF(tup);
    return result;
}

static inline
int
Py_IS_TYPE(const PyObject* o, const PyTypeObject* type){
    return o->ob_type == type;
}

#endif

#endif
