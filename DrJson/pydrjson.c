#include "pyhead.h"
#include <structmember.h>
#define DRJSON_API static inline
#define DRJSON_NO_STDIO
#define DRJSON_NO_IO
#include "drjson.h"
#define DRJ_DONT_FREE_CTX 1
#include "drjson.c"
#include "long_string.h"

static PyModuleDef drjson;
static PyMethodDef drjson_methods[];

static PyObject* _Nullable pydrj_parse(PyObject*, PyObject*, PyObject*);
static PyObject* _Nullable pydrj_load(PyObject*, PyObject*, PyObject*);

static PyTypeObject DrjPyCtxType;
static PyMethodDef DrjPyCtx_methods[];

static inline
StringView
pystring_borrow_stringview(PyObject* pyobj){
    const char* text;
    Py_ssize_t length;
    text = PyUnicode_AsUTF8AndSize(pyobj, &length);
    // unhandled_error_condition(!text);
    return (StringView){.text=text, .length=length};
}

typedef struct DrjPyCtx DrjPyCtx;
struct DrjPyCtx {
    PyObject_HEAD
    DrJsonContext ctx;
};
static PyObject* _Nullable DrjPyCtx_new(PyTypeObject* type, PyObject* args, PyObject* kwargs);
static void DrjPyCtx_dealloc(PyObject* o);
static PyObject*_Nullable DrjPyCtx_mem(PyObject* o);
static PyObject* _Nullable DrjPyCtx_parse(PyObject* s, PyObject* args, PyObject* kwargs);
static PyObject* _Nullable DrjPyCtx_load(PyObject* s, PyObject* args, PyObject* kwargs);
static PyObject* _Nullable DrjPyCtx_make_value(PyObject* s, PyObject* arg);


typedef struct DrjValue DrjValue;
struct DrjValue {
    PyObject_HEAD
    DrjPyCtx* ctx;
    DrJsonValue value;
};
static PyTypeObject DrjValType;
static PyMethodDef DrjVal_methods[];
static PyMemberDef DrjVal_members[];
static PyGetSetDef DrjVal_getset[];
static PySequenceMethods DrjVal_sequence_methods;
static PyMappingMethods DrjVal_mapping_methods;
static void DrjVal_dealloc(PyObject* o);
static PyObject*_Nullable DrjVal_py(PyObject*s);
static PyObject*_Nullable DrjVal_clear(PyObject*s);
static PyObject*_Nullable DrjVal_pop(PyObject*s);
static PyObject*_Nullable DrjVal_append(PyObject*s, PyObject* arg);
static PyObject*_Nullable DrjVal_insert(PyObject*s, PyObject* arg, PyObject* kwargs);
static PyObject*_Nullable DrjVal_dump(PyObject*s, PyObject*, PyObject*);
static PyObject*_Nullable DrjVal_keys(PyObject*s);
static PyObject*_Nullable DrjVal_values(PyObject*s);
static PyObject*_Nullable DrjVal_items(PyObject*s);
static PyObject*_Nullable DrjVal_mem(PyObject*s);
static PyObject*_Nullable DrjVal_query(PyObject*s, PyObject* args, PyObject* kwargs);
static Py_ssize_t DrjVal_len(PyObject*s);
static PyObject*_Nullable DrjVal_subscript(PyObject* s, PyObject* k);
static int DrjVal_ass_subscript(PyObject* s, PyObject*, PyObject*);
static PyObject*_Nullable DrjVal_seqitem(PyObject* s, Py_ssize_t idx);
static Py_hash_t DrjVal_hash(PyObject* s);
static PyObject* DrjVal_richcmp(PyObject *s, PyObject *other, int op);
static PyObject *_Nullable DrjVal_get_kind(PyObject *s, void *_Nullable p);
static PyObject*_Nullable DrjVal_repr(PyObject*s);

static inline
DrjValue*_Nullable
make_drjval(DrjPyCtx* ctx, DrJsonValue);

// helpers
static inline
DrJsonValue
python_to_drj(DrJsonContext* ctx, PyObject* arg, int depth);

static inline
PyObject* _Nullable
drj_to_python(DrJsonContext* ctx, DrJsonValue v);

static inline
PyObject*_Nullable
exception_from_error(DrJsonValue v);

static
PyObject* py_drjson_kinds[] = {
    [DRJSON_ERROR] = NULL,
    [DRJSON_NUMBER] = NULL,
    [DRJSON_INTEGER] = NULL,
    [DRJSON_UINTEGER] = NULL,
    [DRJSON_STRING] = NULL,
    [DRJSON_ARRAY] = NULL,
    [DRJSON_OBJECT] = NULL,
    [DRJSON_NULL] = NULL,
    [DRJSON_BOOL] = NULL,
    [DRJSON_ARRAY_VIEW] = NULL,
    [DRJSON_OBJECT_KEYS] = NULL,
    [DRJSON_OBJECT_VALUES] = NULL,
    [DRJSON_OBJECT_ITEMS] = NULL,
};


PyMODINIT_FUNC _Nullable
PyInit_drjson(void){
    PyObject* mod = PyModule_Create(&drjson);
    PyObject* enum_mod = NULL;
    PyObject* int_enum = NULL;
    PyObject* enum_kwargs = NULL;
    PyObject* enum_args = NULL;
    PyObject* enum_name = NULL;
    PyObject* enum_values = NULL;
    PyObject* kind_enum = NULL;
    PyObject* modname = NULL;
    PyObject* ctx_type = NULL;
    PyObject* val_type = NULL;
    PyObject* version = NULL;
    if(!mod) goto fail;
    modname = PyModule_GetNameObject(mod);
    if(!modname) goto fail;
    PyModule_AddStringConstant(mod, "__version__", DRJSON_VERSION);
    enum_kwargs = PyDict_New();
    if(!enum_kwargs) goto fail;
    if(PyDict_SetItemString(enum_kwargs, "module", modname) < 0)
        goto fail;

    enum_mod = PyImport_ImportModule("enum");
    if(!enum_mod) goto fail;
    int_enum = PyObject_GetAttrString(enum_mod, "IntEnum");
    if(!int_enum) goto fail;
    enum_values = PyDict_New();
    if(!enum_values) goto fail;
    enum_name = PyUnicode_FromString("Kind");
    if(!enum_name) goto fail;
    #define ADD_VALUE(x) do { \
        PyObject* v = PyLong_FromLong(DRJSON_##x); \
        if(!v) goto fail; \
        if(PyDict_SetItemString(enum_values, #x, v) < 0){ \
            Py_DECREF(v); \
            goto fail; \
        } \
        Py_DECREF(v); \
    }while(0)
    ADD_VALUE(ERROR);
    ADD_VALUE(NUMBER);
    ADD_VALUE(INTEGER);
    ADD_VALUE(UINTEGER);
    ADD_VALUE(STRING);
    ADD_VALUE(ARRAY);
    ADD_VALUE(OBJECT);
    ADD_VALUE(NULL);
    ADD_VALUE(BOOL);
    ADD_VALUE(ARRAY_VIEW);
    ADD_VALUE(OBJECT_KEYS);
    ADD_VALUE(OBJECT_VALUES);
    ADD_VALUE(OBJECT_ITEMS);
    #undef ADD_VALUE
    enum_args = PyTuple_Pack(2, enum_name, enum_values);
    if(!enum_args) goto fail;
    kind_enum = PyObject_Call(int_enum, enum_args, enum_kwargs);
    if(!kind_enum) goto fail;
    {
        PyObject* doc = PyUnicode_FromString("The kind of a drjson value");
        if(!doc) goto fail;
        if(PyObject_SetAttrString(int_enum, "__doc__", doc) < 0){
            Py_DECREF(doc);
            goto fail;
        }
        Py_DECREF(doc);
    }
    if(PyModule_AddObjectRef(mod, "Kind", kind_enum) < 0)
        goto fail;

    #define ADD_MOD_ENUM(x) do { \
        if(py_drjson_kinds[DRJSON_##x]){ \
            Py_DECREF(py_drjson_kinds[DRJSON_##x]); \
            py_drjson_kinds[DRJSON_##x] = NULL; \
        } \
        PyObject* e = PyObject_GetAttrString(kind_enum, #x); \
        if(!e) goto fail; \
        if(PyModule_AddObjectRef(mod, #x, e) < 0){ \
            Py_DECREF(e); \
            goto fail; \
        } \
        py_drjson_kinds[DRJSON_##x] = e; \
    }while(0)
    ADD_MOD_ENUM(ERROR);
    ADD_MOD_ENUM(NUMBER);
    ADD_MOD_ENUM(INTEGER);
    ADD_MOD_ENUM(UINTEGER);
    ADD_MOD_ENUM(STRING);
    ADD_MOD_ENUM(ARRAY);
    ADD_MOD_ENUM(OBJECT);
    ADD_MOD_ENUM(NULL);
    ADD_MOD_ENUM(BOOL);
    ADD_MOD_ENUM(ARRAY_VIEW);
    ADD_MOD_ENUM(OBJECT_KEYS);
    ADD_MOD_ENUM(OBJECT_VALUES);
    ADD_MOD_ENUM(OBJECT_ITEMS);

    if(PyType_Ready(&DrjPyCtxType) < 0)
        goto fail;
    Py_INCREF(&DrjPyCtxType);
    ctx_type = (PyObject*)&DrjPyCtxType;
    if(PyModule_AddObjectRef(mod, "Ctx", ctx_type) < 0) goto fail;

    if(PyType_Ready(&DrjValType) < 0)
        goto fail;
    Py_INCREF(&DrjValType);
    val_type = (PyObject*)&DrjValType;
    if(PyModule_AddObjectRef(mod, "Value", val_type) < 0) goto fail;

    version = Py_BuildValue("(iii)", DRJSON_VERSION_MAJOR, DRJSON_VERSION_MINOR, DRJSON_VERSION_MICRO);
    if(!version) goto fail;
    if(PyModule_AddObjectRef(mod, "version", version) < 0) goto fail;

    if(0){
        fail:
        Py_XDECREF(mod);
        mod = NULL;
    }
    Py_XDECREF(enum_mod);
    Py_XDECREF(int_enum);
    Py_XDECREF(enum_kwargs);
    Py_XDECREF(enum_args);
    Py_XDECREF(enum_name);
    Py_XDECREF(enum_values);
    Py_XDECREF(kind_enum);
    Py_XDECREF(modname);
    Py_XDECREF(ctx_type);
    Py_XDECREF(val_type);
    Py_XDECREF(version);
    return mod;
}

static
PyModuleDef drjson = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name="drjson",
    .m_doc= ""
        ,
    .m_size=-1,
    .m_methods=drjson_methods,
    .m_slots=NULL,
    .m_traverse=NULL,
    .m_clear=NULL,
    .m_free=NULL,
};

static
PyMethodDef drjson_methods[] = {
    {
        .ml_name = "parse",
        .ml_meth = (PyCFunction)pydrj_parse,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "parse(text, braceless=False)\n"
            "--\n"
            "\n"
            "Convenience function, implicitly creates a context and calls\n"
            "`parse` on it with the given arguments.\n"
    },
    {
        .ml_name = "loads",
        .ml_meth = (PyCFunction)pydrj_parse,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "loads(text, braceless=False)\n"
            "--\n"
            "\n"
            "Convenience function, implicitly creates a context and calls\n"
            "`parse` on it with the given arguments.\n"
    },
    {
        .ml_name = "load",
        .ml_meth = (PyCFunction)pydrj_load,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "load(file, braceless=False)\n"
            "--\n"
            "\n"
            "Convenience function, calls .read() on the file,\n"
            "implicitly creates a context and calls `parse` on it with\n"
            "the given arguments.\n"
    },
    {NULL, NULL, 0, NULL}
};

static
PyTypeObject DrjPyCtxType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "drjson.Ctx",
    .tp_doc = "A json context",
    .tp_basicsize = sizeof(DrjPyCtx),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = DrjPyCtx_new,
    .tp_methods = DrjPyCtx_methods,
    .tp_dealloc = DrjPyCtx_dealloc,
};

static PyMethodDef DrjPyCtx_methods[] = {
    {
        .ml_name = "parse",
        .ml_meth = (PyCFunction)DrjPyCtx_parse,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "parse(self, text, braceless=False)\n"
            "--\n"
            "\n"
            "Parse a json string.\n",
    },
    {
        .ml_name = "loads",
        .ml_meth = (PyCFunction)DrjPyCtx_parse,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "loads(self, text, braceless=False)\n"
            "--\n"
            "\n"
            "Parse a json string.\n",
    },
    {
        .ml_name = "load",
        .ml_meth = (PyCFunction)DrjPyCtx_load,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "load(self, file, braceless=False)\n"
            "--\n"
            "\n"
            "Calls .read() on the file, then parses as a json string.\n",
    },
    {
        .ml_name = "make",
        .ml_meth = (PyCFunction)DrjPyCtx_make_value,
        .ml_flags = METH_O,
        .ml_doc = "make(self, value)\n"
            "--\n"
            "\n"
            "Converts (recursively) a basic python type to a json value.\n",
    },
    {
        .ml_name = "mem",
        .ml_meth = (PyCFunction)DrjPyCtx_mem,
        .ml_flags = METH_NOARGS,
        .ml_doc = "mem(self)\n"
            "--\n"
            "\n"
            "Returns the memory usage of the ctx.\n",
    },
    {NULL, NULL, 0, NULL}
};

static
PyObject* _Nullable
DrjPyCtx_new(PyTypeObject* type, PyObject* args, PyObject* kwargs){
    const char* const keywords[] = {NULL};
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, ":Context", (char**)keywords))
        return NULL;
    DrjPyCtx* self = (DrjPyCtx*)type->tp_alloc(type, 0);
    if(!self) return NULL;
    self->ctx = (DrJsonContext){
        .allocator = drjson_stdc_allocator(),
    };
    return (PyObject*)self;
}

static
void
DrjPyCtx_dealloc(PyObject* o){
    DrjPyCtx* self = (DrjPyCtx*)o;
    drjson_ctx_free_all(&self->ctx);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static
PyObject* _Nullable
DrjPyCtx_mem(PyObject* s){
    DrjPyCtx* self = (DrjPyCtx*)s;
    Py_ssize_t atom_size = drj_atom_table_size_for(self->ctx.atoms.capacity);
    Py_ssize_t object_array = sizeof(DrJsonObject)*self->ctx.objects.capacity;
    DrJsonObject* odata = self->ctx.objects.data;
    Py_ssize_t objects = 0;
    Py_ssize_t obj_slop = 0;
    for(size_t i = 0; i < self->ctx.objects.count; i++){
        DrJsonObject* object = &odata[i];
        objects += drjson_size_for_object_of_length(object->capacity);
        obj_slop += drjson_size_for_object_of_length(object->capacity) - drjson_size_for_object_of_length(object->count);
    }
    Py_ssize_t array_array = sizeof(DrJsonArray)*self->ctx.arrays.capacity;
    Py_ssize_t arrays = 0;
    Py_ssize_t arr_slop = 0;
    DrJsonArray* adata = self->ctx.arrays.data;
    for(size_t i = 0; i < self->ctx.arrays.count; i++){
        DrJsonArray* array = &adata[i];
        arrays += array->capacity * sizeof *array->array_items;
        arr_slop += (array->capacity * sizeof *array->array_items) - (array->count * sizeof *array->array_items);
    }
    Py_ssize_t usage = atom_size + object_array + objects + array_array + arrays;
    return Py_BuildValue("nnnnnnnn", usage, object_array, objects, obj_slop, array_array, arrays, arr_slop, atom_size);
    return PyLong_FromSsize_t(usage);
}

static
PyObject* _Nullable
DrjPyCtx_parse(PyObject* s, PyObject* args, PyObject* kwargs){
    DrjPyCtx* self = (DrjPyCtx*)s;
    int braceless = 0;
    PyObject* txt = NULL;
    const char* const keywords[] = {"text", "braceless", NULL};
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|p:parse", (char**)keywords, &PyUnicode_Type, &txt, &braceless))
        return NULL;
    StringView sv = pystring_borrow_stringview(txt);
    unsigned flags = braceless?DRJSON_PARSE_FLAG_BRACELESS_OBJECT: DRJSON_PARSE_FLAG_NONE;
    DrJsonValue v = drjson_parse_string(&self->ctx, sv.text, sv.length, flags);
    if(v.kind == DRJSON_ERROR){
        if(PyErr_Occurred())
            return NULL;
        return exception_from_error(v);
    }
    return (PyObject*)make_drjval(self, v);
}

static
PyObject* _Nullable
DrjPyCtx_load(PyObject* s, PyObject* args, PyObject* kwargs){
    DrjPyCtx* self = (DrjPyCtx*)s;
    int braceless = 0;
    PyObject* file_arg = NULL;
    const char* const keywords[] = {"file", "braceless", NULL};
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p:load", (char**)keywords, &file_arg, &braceless))
        return NULL;
    PyObject* txt;
    {
        PyObject* file;
        if(PyUnicode_Check(file_arg)){
            file = PyFile_FromUnicode(file_arg, "rb");
            if(!file) return NULL;
        }
        else {
            file = file_arg;
        }
        txt = PyObject_CallMethod(file, "read", NULL);
        if(file != file_arg) Py_DECREF(file);
        if(!txt) return NULL;
    }
    StringView sv;
    if(PyUnicode_Check(txt)){
        sv = pystring_borrow_stringview(txt);
    }
    else if(PyBytes_Check(txt)){
        char* buffer; Py_ssize_t length;
        int err = PyBytes_AsStringAndSize(txt, &buffer, &length);
        if(err) {
            Py_DECREF(txt);
            return NULL;
        }
        sv.text = buffer;
        sv.length = length;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "read() returned neither bytes nor a string");
        Py_DECREF(txt);
        return NULL;
    }
    unsigned flags = DRJSON_PARSE_FLAG_NONE;
    if(braceless) flags |= DRJSON_PARSE_FLAG_BRACELESS_OBJECT;
    DrJsonValue v = drjson_parse_string(&self->ctx, sv.text, sv.length, flags);
    Py_DECREF(txt);
    if(v.kind == DRJSON_ERROR){
        if(PyErr_Occurred())
            return NULL;
        return exception_from_error(v);
    }
    return (PyObject*)make_drjval(self, v);
}

static inline
DrJsonValue
python_to_drj(DrJsonContext* ctx, PyObject* arg, int depth){
    if(depth > 100)
        return drjson_make_error(DRJSON_ERROR_TOO_DEEP, "TOO DEEP");
    depth++;
    if(Py_TYPE(arg) == &DrjValType){
        DrjValue* v = (DrjValue*)arg;
        if(&v->ctx->ctx == ctx){
            return v->value;
        }
        switch(v->value.kind){
            case DRJSON_ERROR:
                break;
            case DRJSON_NUMBER:
            case DRJSON_INTEGER:
            case DRJSON_UINTEGER:
                return v->value;
            case DRJSON_STRING:
                break;
            case DRJSON_ARRAY:
            case DRJSON_OBJECT:
                break;
            case DRJSON_NULL:
            case DRJSON_BOOL:
                return v->value;
            case DRJSON_ARRAY_VIEW:
            case DRJSON_OBJECT_KEYS:
            case DRJSON_OBJECT_VALUES:
            case DRJSON_OBJECT_ITEMS:
                break;
            default:
                break;
        }
        return drjson_make_null();
    }
    if(arg == Py_None){
        DrJsonValue val = drjson_make_null();
        return val;
    }
    if(arg == Py_True)
        return drjson_make_bool(1);
    if(arg == Py_False)
        return drjson_make_bool(0);
    if(PyUnicode_Check(arg)){
        StringView sv = pystring_borrow_stringview(arg);
        DrJsonValue val = drjson_make_string(ctx, sv.text, sv.length);
        return val;
    }
    if(PyLong_Check(arg)){
        _Static_assert(sizeof(long long) == sizeof(int64_t), "");
        int overflow = 0;
        int64_t i = PyLong_AsLongLongAndOverflow(arg, &overflow);
        if(i == -1){
            if(PyErr_Occurred())
                return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "integer doesn't fit in i64");
            if(overflow < 0)
                return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "integer doesn't fit in i64");
            if(overflow == 1){
                PyErr_Clear();
                _Static_assert(sizeof(unsigned long long) == sizeof(uint64_t), "");
                uint64_t u = PyLong_AsUnsignedLongLong(arg);
                if(u == (uint64_t)-1){
                    if(PyErr_Occurred()){
                        return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "integer doesn't fit in u64");
                    }
                }
                DrJsonValue val = drjson_make_uint(u);
                return val;
            }
        }
        DrJsonValue val = drjson_make_int(i);
        return val;
    }
    if(PyFloat_Check(arg)){
        double d = PyFloat_AsDouble(arg);
        DrJsonValue val = drjson_make_number(d);
        return val;
    }
    if(PyDict_Check(arg)){
        DrJsonValue val = drjson_make_object(ctx);
        PyObject* key = NULL;
        PyObject* value = NULL;
        for(Py_ssize_t pos = 0;PyDict_Next(arg, &pos, &key, &value);){
            if(!PyUnicode_Check(key)){
                return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "only string keys of dicts supported");
            }
            StringView k = pystring_borrow_stringview(key);
            DrJsonValue v = python_to_drj(ctx, value, depth);
            if(v.kind == DRJSON_ERROR) return v;
            if(drjson_object_set_item_copy_key(ctx, val, k.text, k.length, v))
                return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to set object item");
        }
        return val;
    }
    if(PySequence_Check(arg)){
        PyObject* seq = PySequence_Fast(arg, "wat");
        if(!seq){
            return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "Not a fast sequence");
        }
        Py_ssize_t len = PySequence_Fast_GET_SIZE(seq);
        DrJsonValue val = drjson_make_array(ctx);
        if(val.kind == DRJSON_ERROR){
            Py_DECREF(seq);
            return val;
        }
        for(Py_ssize_t i = 0; i < len; i++){
            PyObject* it = PySequence_Fast_GET_ITEM(seq, i);
            DrJsonValue v = python_to_drj(ctx, it, depth);
            if(v.kind == DRJSON_ERROR){
                Py_DECREF(seq);
                return v;
            }
            if(drjson_array_push_item(ctx, val, v)){
                Py_DECREF(seq);
                return drjson_make_error(DRJSON_ERROR_ALLOC_FAILURE, "Failed to push to an array");
            }
        }
        Py_DECREF(seq);
        return val;
    }
    if(PyObject_HasAttrString(arg, "__dict__")){
        PyObject* odict = PyObject_GetAttrString(arg, "__dict__");
        return python_to_drj(ctx, odict, depth);
    }
    // return drjson_make_null();
    return drjson_make_error(DRJSON_ERROR_INVALID_VALUE, "UNHANDLED TYPE CONVERSION");
}


static
PyObject* _Nullable
DrjPyCtx_make_value(PyObject* s, PyObject* arg){
    DrjPyCtx* self = (DrjPyCtx*)s;
    DrJsonValue val = python_to_drj(&self->ctx, arg, 0);
    if(val.kind == DRJSON_ERROR)
        return exception_from_error(val);
    return (PyObject*)make_drjval(self, val);
}

static PyTypeObject DrjValType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "drjson.Value",
    .tp_doc = "A json value",
    .tp_basicsize = sizeof(DrjValue),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = NULL,
    .tp_getset = DrjVal_getset,
    .tp_members = DrjVal_members,
    .tp_methods = DrjVal_methods,
    .tp_dealloc = DrjVal_dealloc,
    .tp_repr = DrjVal_repr,
    .tp_as_sequence = &DrjVal_sequence_methods,
    .tp_as_mapping = &DrjVal_mapping_methods,
    .tp_hash = DrjVal_hash,
    .tp_richcompare = DrjVal_richcmp,
};

static PyMethodDef DrjVal_methods[] = {
    {
        .ml_name = "py",
        .ml_meth = (PyCFunction)DrjVal_py,
        .ml_flags = METH_NOARGS,
        .ml_doc = "py(self)\n"
            "--\n"
            "\n"
            "Converts the value to python native types.\n",
    },
    {
        .ml_name = "query",
        .ml_meth = (PyCFunction)DrjVal_query,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "query(self, query, type=None)\n"
            "--\n"
            "\n"
            "Executes the given query into the object",
    },
    {
        .ml_name = "clear",
        .ml_meth = (PyCFunction)DrjVal_clear,
        .ml_flags = METH_NOARGS,
        .ml_doc = "clear(self)\n"
            "--\n"
            "\n"
            "Empties the array or object\n",
    },
    {
        .ml_name = "append",
        .ml_meth = (PyCFunction)DrjVal_append,
        .ml_flags = METH_O,
        .ml_doc = "append(self, item)\n"
            "--\n"
            "\n"
            "Whatcha think this does.\n",
    },
    {
        .ml_name = "pop",
        .ml_meth = (PyCFunction)DrjVal_pop,
        .ml_flags = METH_NOARGS,
        .ml_doc = "pop(self)\n"
            "--\n"
            "\n"
            "Whatcha think this does.\n",
    },
    {
        .ml_name = "insert",
        .ml_meth = (PyCFunction)DrjVal_insert,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "insert(self, whence, item)\n"
            "--\n"
            "\n"
            "Whatcha think this does.\n",
    },
    {
        .ml_name = "dump",
        .ml_meth = (PyCFunction)DrjVal_dump,
        .ml_flags = METH_VARARGS|METH_KEYWORDS,
        .ml_doc = "dump(self, writer=None, pretty=False, newline=False)\n"
            "--\n"
            "\n"
            "Serializes to a json string.\n"
            "The writer should be a callable that takes a string or have a method called `write` that takes a string.\n",
    },
    {
        .ml_name = "keys",
        .ml_meth = (PyCFunction)DrjVal_keys,
        .ml_flags = METH_NOARGS,
        .ml_doc = "keys(self)\n"
            "--\n"
            "\n"
            "Returns a keys view into this object.\n"
    },
    {
        .ml_name = "items",
        .ml_meth = (PyCFunction)DrjVal_items,
        .ml_flags = METH_NOARGS,
        .ml_doc = "keys(self)\n"
            "--\n"
            "\n"
            "Returns a items view into this object.\n"
    },
    {
        .ml_name = "values",
        .ml_meth = (PyCFunction)DrjVal_values,
        .ml_flags = METH_NOARGS,
        .ml_doc = "keys(self)\n"
            "--\n"
            "\n"
            "Returns a values view into this object.\n"
    },
    {
        .ml_name = "mem",
        .ml_meth = (PyCFunction)DrjVal_mem,
        .ml_flags = METH_NOARGS,
        .ml_doc = "mem(self)\n"
            "--\n"
            "\n"
            "Returns the memory usage of this object (not recursive).\n"
    },
    {},
};
static PyMemberDef DrjVal_members[] = {
    {"ctx", T_OBJECT, offsetof(DrjValue, ctx), READONLY, "The Context that this value is from."},
    {0}  // Sentinel
};

static
void
DrjVal_dealloc(PyObject* o){
    DrjValue* self = (DrjValue*)o;
    Py_CLEAR(self->ctx);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static inline
DrjValue*_Nullable
make_drjval(DrjPyCtx* ctx, DrJsonValue v){
    DrjValue* o = PyObject_New(DrjValue, &DrjValType);
    if(o){
        Py_INCREF(ctx);
        o->ctx = ctx;
        o->value = v;
    }
    return o;
}

static
PyObject*_Nullable
DrjVal_repr(PyObject*s){
    DrjValue* self = (DrjValue*)s;
    // char buff[512];
    static char buff[512*1024];
    size_t printed = 0;
    int used = snprintf(buff, sizeof buff, "Value<%s, ", DrJsonKindNames[self->value.kind]);
    if(drjson_print_value_mem(&self->ctx->ctx, buff+used, (sizeof buff)-used, self->value, 0, 0, &printed)){
        memcpy(buff+used, "...", 3);
        printed = 3;
    }
    printed += used;
    if(printed < sizeof buff)
        buff[printed++] = '>';
    return PyUnicode_FromStringAndSize(buff, printed);
}

static
PyObject*_Nullable
DrjVal_py(PyObject*s){
    DrjValue* self = (DrjValue*)s;
    return drj_to_python(&self->ctx->ctx, self->value);
}

static
PyObject*_Nullable
DrjVal_clear(PyObject*s){
    DrjValue* self = (DrjValue*)s;
    int err = drjson_clear(&self->ctx->ctx, self->value);
    if(err){
        PyErr_SetString(PyExc_TypeError, "Can't clear this type");
        return NULL;
    }
    Py_RETURN_NONE;
}

static
PyObject*_Nullable
DrjVal_pop(PyObject*s){
    DrjValue* self = (DrjValue*)s;
    DrJsonValue v = drjson_array_pop_item(&self->ctx->ctx, self->value);
    if(v.kind == DRJSON_ERROR)
        return exception_from_error(v);
    return (PyObject*)make_drjval(self->ctx, v);
}

static
PyObject*_Nullable
DrjVal_append(PyObject* s, PyObject* arg){
    DrjValue* self = (DrjValue*)s;
    DrJsonValue v = python_to_drj(&self->ctx->ctx, arg, 0);
    if(v.kind == DRJSON_ERROR)
        return exception_from_error(v);
    int err = drjson_array_push_item(&self->ctx->ctx, self->value, v);
    if(err){
        PyErr_SetString(PyExc_TypeError, "Couldn't append to this type");
        return NULL;
    }
    Py_RETURN_NONE;
}

static
PyObject*_Nullable
DrjVal_insert(PyObject*s, PyObject* args, PyObject* kwargs){
    DrjValue* self = (DrjValue*)s;
    const char* keywords[] = {
        "whence", "item",
        NULL,
    };
    Py_ssize_t whence;
    PyObject* item;
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "nO:insert", (char**)keywords, &whence, &item))
        return NULL;
    DrJsonValue val = python_to_drj(&self->ctx->ctx, item, 0);
    if(val.kind == DRJSON_ERROR)
        return exception_from_error(val);
    int err = drjson_array_insert_item(&self->ctx->ctx, self->value, whence, val);
    if(err){
        PyErr_SetString(PyExc_TypeError, "Couldn't insert to this type");
        return NULL;
    }
    Py_RETURN_NONE;
}

static
int
pywrite(void*_Null_unspecified ud, const void* mem, size_t len){
    PyObject* p = ud;
    PyObject* err = PyObject_CallOneArg(p, PyUnicode_FromStringAndSize(mem, len));
    Py_XDECREF(err);
    return err?0:1;
}


struct MemBuff {
    void* p;
    size_t cap;
    size_t used;
};
static
int
memwrite(void*_Null_unspecified ud, const void* mem, size_t len){
    struct MemBuff* mb = ud;
    if(len + mb->used >= mb->cap){
        size_t new_cap = mb->cap?mb->cap*2:128;
        while(new_cap < len)
            new_cap *= 2;
        void* p = realloc(mb->p, new_cap);
        if(!p){
            free(mb->p);
            return 1;
        }
        mb->p = p;
        mb->cap = new_cap;
    }
    memcpy(mb->used+(char*)mb->p, mem, len);
    mb->used += len;
    return 0;
}


static
PyObject*_Nullable
DrjVal_dump(PyObject* s, PyObject* args, PyObject* kwargs){
    DrjValue* self = (DrjValue*)s;
    PyObject* pywriter = NULL;
    PyObject* meth = NULL;
    int pretty = 0;
    int newline = 0;
    char* keywords[] = {"writer", "pretty", "newline", NULL};
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "|Opp:dump", (char**)keywords, &pywriter, &pretty, &newline))
        return NULL;
    if(pywriter == Py_None)
        pywriter = NULL;
    DrJsonTextWriter writer;
    struct MemBuff mb = {0};
    if(!pywriter){
        writer.up = &mb;
        writer.write = memwrite;
    }
    else{
        if(PyObject_HasAttrString(pywriter, "write")){
            meth = PyObject_GetAttrString(pywriter, "write");
        }
        writer.up = meth?meth:pywriter;
        writer.write = pywrite;
    }
    unsigned flags = 0;
    if(pretty) flags |= DRJSON_PRETTY_PRINT;
    if(newline) flags |= DRJSON_APPEND_NEWLINE;
    int err = drjson_print_value(&self->ctx->ctx, &writer, self->value, 0, flags);
    Py_XDECREF(meth);
    if(err){
        if(!PyErr_Occurred())
            PyErr_SetString(PyExc_Exception, "Error while dumping");
        return NULL;
    }
    if(!pywriter){
        PyObject* result = PyUnicode_FromStringAndSize(mb.p, mb.used);
        free(mb.p);
        return result;
    }
    Py_RETURN_NONE;

}

static
PyObject*_Nullable
DrjVal_keys(PyObject* s){
    DrjValue* self = (DrjValue*)s;
    DrJsonValue v = drjson_object_keys(self->value);
    if(v.kind == DRJSON_ERROR)
        return exception_from_error(v);
    return (PyObject*)make_drjval(self->ctx, v);
}

static
PyObject*_Nullable
DrjVal_items(PyObject* s){
    DrjValue* self = (DrjValue*)s;
    DrJsonValue v = drjson_object_items(self->value);
    if(v.kind == DRJSON_ERROR)
        return exception_from_error(v);
    return (PyObject*)make_drjval(self->ctx, v);
}

static
PyObject*_Nullable
DrjVal_values(PyObject* s){
    DrjValue* self = (DrjValue*)s;
    DrJsonValue v = drjson_object_values(self->value);
    if(v.kind == DRJSON_ERROR)
        return exception_from_error(v);
    return (PyObject*)make_drjval(self->ctx, v);
}

static
PyObject*_Nullable
DrjVal_mem(PyObject* s){
    DrjValue* self = (DrjValue*)s;
    Py_ssize_t usage = 0;
    switch(self->value.kind){
        default: break;
        case DRJSON_ARRAY:{
            const DrJsonArray* adata = self->ctx->ctx.arrays.data;
            const DrJsonArray* array = &adata[self->value.array_idx];
            usage = array->capacity * sizeof *array->array_items;
        }break;
        case DRJSON_OBJECT:{
            DrJsonObject* odata = self->ctx->ctx.objects.data;
            DrJsonObject* object = &odata[self->value.object_idx];
            usage = drjson_size_for_object_of_length(object->capacity);
        }break;
    }
    return PyLong_FromSsize_t(usage);
}

static
PyObject*_Nullable
DrjVal_query(PyObject*s, PyObject* args, PyObject* kwargs){
    DrjValue* self = (DrjValue*)s;
    const char* txt;
    Py_ssize_t len;
    int type = -1;
    const char* keywords[] = {
        "query", "type",
        NULL,
    };
    if(!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|i:query", (char**)keywords, &txt, &len, &type))
        return NULL;
    DrJsonValue val;
    if(type > DRJSON_ERROR && type <= DRJSON_OBJECT_ITEMS)
        val = drjson_checked_query(&self->ctx->ctx, self->value, type, txt, len);
    else
        val = drjson_query(&self->ctx->ctx, self->value, txt, len);
    if(val.kind == DRJSON_ERROR)
        return exception_from_error(val);
    return (PyObject*)make_drjval(self->ctx, val);
}

static
PyObject *_Nullable
DrjVal_get_kind(PyObject *s, void *_Nullable p){
    DrjValue* self = (DrjValue*)s;
    (void)p;
    PyObject* v = py_drjson_kinds[self->value.kind];
    Py_INCREF(v);
    return v;
}

static PyGetSetDef DrjVal_getset[] = {
    {"kind", DrjVal_get_kind, NULL, "The kind of the value.", NULL},
    {0} // Sentinel
};

static PySequenceMethods DrjVal_sequence_methods = {
    .sq_item = DrjVal_seqitem,
    .sq_length = DrjVal_len,

};

static
Py_ssize_t
DrjVal_len(PyObject*s){
    DrjValue* self = (DrjValue*)s;
    int64_t len = drjson_len(&self->ctx->ctx, self->value);
    if(len < 0)
        PyErr_SetString(PyExc_TypeError, "Length not supported for this type");
    return len;
}

static
PyObject*_Nullable
DrjVal_subscript(PyObject* s, PyObject* k){
    DrjValue* self = (DrjValue*)s;
    if(PyLong_Check(k)){
        Py_ssize_t idx = PyLong_AsSsize_t(k);
        if(self->value.kind == DRJSON_OBJECT){
            // return a tuple of k, v
            DrJsonValue v = drjson_object_items(self->value);
            DrJsonValue key = drjson_get_by_index(&self->ctx->ctx, v, idx*2);
            if(key.kind == DRJSON_ERROR)
                return exception_from_error(key);
            DrJsonValue value = drjson_get_by_index(&self->ctx->ctx, v, idx*2+1);
            if(value.kind == DRJSON_ERROR)
                return exception_from_error(value);
            PyObject* o1 = (PyObject*)make_drjval(self->ctx, key);
            if(!o1) return NULL;
            PyObject* o2 = (PyObject*)make_drjval(self->ctx, value);
            if(!o2){
                Py_XDECREF(o1);
                return NULL;
            }
            return PyTuple_Pack(2, o1, o2);
        }
        DrJsonValue val = drjson_get_by_index(&self->ctx->ctx, self->value, idx);
        if(val.kind == DRJSON_ERROR)
            return exception_from_error(val);
        return (PyObject*)make_drjval(self->ctx, val);
    }
    else if(PyUnicode_Check(k)){
        StringView sv = pystring_borrow_stringview(k);
        DrJsonValue val = drjson_object_get_item(&self->ctx->ctx, self->value, sv.text, sv.length);
        if(val.kind == DRJSON_ERROR)
            return exception_from_error(val);
        return (PyObject*)make_drjval(self->ctx, val);
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Unsupported index type");
        return NULL;
    }
}

static
int
DrjVal_ass_subscript(PyObject* s, PyObject* key, PyObject* v){
    DrjValue* self = (DrjValue*)s;
    // NULL v means del item
    if(!v){
        if(!PyLong_Check(key)){
            PyErr_SetString(PyExc_TypeError, "del with this type unsupported");
            return -1;
        }
        Py_ssize_t idx = PyLong_AsSsize_t(key);
        DrJsonValue err = drjson_array_del_item(&self->ctx->ctx, self->value, idx);
        if(err.kind == DRJSON_ERROR){
            exception_from_error(err);
            return -1;
        }
        return 0;
    }
    if(self->value.kind == DRJSON_ARRAY){
        if(!PyLong_Check(key)){
            PyErr_SetString(PyExc_TypeError, "__setitem__ with this type unsupported");
            return -1;
        }
        DrJsonValue val = python_to_drj(&self->ctx->ctx, v, 0);
        if(val.kind == DRJSON_ERROR){
            exception_from_error(val);
            return -1;
        }
        long long idx = PyLong_AsLongLong(key);
        int err = drjson_array_set_by_index(&self->ctx->ctx, self->value, idx, val);
        if(err){
            PyErr_SetString(PyExc_Exception, "error when setting (oom?)");
            return -1;
        }
        return 0;
    }
    else if(self->value.kind == DRJSON_OBJECT){
        if(!PyUnicode_Check(key)){
            PyErr_SetString(PyExc_TypeError, "__setitem__ with this type unsupported");
            return -1;
        }
        DrJsonValue val = python_to_drj(&self->ctx->ctx, v, 0);
        if(val.kind == DRJSON_ERROR){
            exception_from_error(val);
            return -1;
        }
        StringView sv = pystring_borrow_stringview(key);
        int err = drjson_object_set_item_copy_key(&self->ctx->ctx, self->value, sv.text, sv.length, val);
        if(err){
            PyErr_SetString(PyExc_Exception, "error when setting (oom?)");
            return -1;
        }
        return 0;
    }
    else{
        PyErr_SetString(PyExc_TypeError, "__setitem__ with this type unsupported");
        return -1;
    }
}

static
PyObject*_Nullable
DrjVal_seqitem(PyObject* s, Py_ssize_t idx){
    // fprintf(stderr, "%s %zd\n", __func__, idx);
    DrjValue* self = (DrjValue*)s;
    if(self->value.kind == DRJSON_OBJECT){
        // return a tuple of k, v
        DrJsonValue v = drjson_object_items(self->value);
        DrJsonValue key = drjson_get_by_index(&self->ctx->ctx, v, idx*2);
        if(key.kind == DRJSON_ERROR)
            return NULL;
            // return exception_from_error(key);
        DrJsonValue value = drjson_get_by_index(&self->ctx->ctx, v, idx*2+1);
        if(value.kind == DRJSON_ERROR)
            return NULL;
            // return exception_from_error(value);
        PyObject* o1 = (PyObject*)make_drjval(self->ctx, key);
        if(!o1) return NULL;
        PyObject* o2 = (PyObject*)make_drjval(self->ctx, value);
        if(!o2){
            Py_XDECREF(o1);
            return NULL;
        }
        return PyTuple_Pack(2, o1, o2);
    }
    DrJsonValue val = drjson_get_by_index(&self->ctx->ctx, self->value, idx);
    if(val.kind == DRJSON_ERROR)
        return NULL;
        // return exception_from_error(val);
    return (PyObject*)make_drjval(self->ctx, val);

}

static
Py_hash_t
DrjVal_hash(PyObject* s){
    DrjValue* self = (DrjValue*)s;
    switch(self->value.kind){
        case DRJSON_NUMBER:
            return (Py_hash_t)self->value.number;
        case DRJSON_INTEGER:
            if(self->value.integer == -1) return -2;
            return (Py_hash_t)self->value.integer;
        case DRJSON_UINTEGER:
            if((Py_hash_t)self->value.uinteger == -1) return (Py_hash_t)-2;
            return (Py_hash_t)self->value.uinteger;
        case DRJSON_NULL:
            return 0;
        case DRJSON_BOOL:
            return self->value.boolean?1:2;
        case DRJSON_STRING:{
            DrJsonAtom atom = self->value.atom;
            return atom.bits >> 32u;
        }
        default:
            return PyObject_HashNotImplemented(s);
    }
}

static
PyObject*
DrjVal_richcmp(PyObject *s, PyObject *o, int op){
    if(Py_TYPE(o) != &DrjValType)
        Py_RETURN_NOTIMPLEMENTED;
    DrjValue* self = (DrjValue*)s;
    DrjValue* other = (DrjValue*)o;
    if(op == Py_EQ){
        return Py_NewRef(drjson_eq(self->value, other->value)?Py_True:Py_False);
    }
    Py_RETURN_NOTIMPLEMENTED;
}

static PyMappingMethods DrjVal_mapping_methods = {
    .mp_length = DrjVal_len,
    .mp_subscript = DrjVal_subscript,
    .mp_ass_subscript = DrjVal_ass_subscript,
};

static inline
PyObject*_Nullable
exception_from_error(DrJsonValue v){
    if(PyErr_Occurred()) return NULL;
    // TODO: correct error type based on error type
    PyErr_SetString(PyExc_Exception, v.err_mess);
    return NULL;
}

static inline
PyObject* _Nullable
drj_to_python(DrJsonContext* ctx, DrJsonValue v){
    switch(v.kind){
        case DRJSON_ERROR:
            return exception_from_error(v);
        case DRJSON_NUMBER:
            return PyFloat_FromDouble(v.number);
        case DRJSON_INTEGER:
            return PyLong_FromLongLong(v.integer);
        case DRJSON_UINTEGER:
            return PyLong_FromUnsignedLongLong(v.uinteger);
        case DRJSON_STRING:{
            const char* string = ""; size_t len = 0;
            int err = drjson_get_str_and_len(ctx, v, &string, &len);
            (void)err;
            return PyUnicode_FromStringAndSize(string, len);
        }break;
        case DRJSON_ARRAY_VIEW:
        case DRJSON_OBJECT_KEYS:
        case DRJSON_OBJECT_VALUES:
        case DRJSON_OBJECT_ITEMS:
        case DRJSON_ARRAY:{
            size_t len = drjson_len(ctx, v);
            PyObject* l = PyList_New(len);
            if(!l) return NULL;
            for(size_t i = 0; i < len; i++){
                DrJsonValue it = drjson_get_by_index(ctx, v, i);
                PyObject* item = drj_to_python(ctx, it);
                if(!item) {
                    Py_DECREF(l);
                    return NULL;
                }
                PyList_SET_ITEM(l, i, item);
            }
            return l;
        }
        case DRJSON_OBJECT:{
            PyObject* d = PyDict_New();
            if(!d) return NULL;
            DrJsonValue items = drjson_object_items(v);
            size_t len = drjson_len(ctx, items);
            for(size_t i = 0; i < len; i+= 2){
                DrJsonValue k = drjson_get_by_index(ctx, items, i);
                DrJsonValue v = drjson_get_by_index(ctx, items, i+1);
                PyObject* key = drj_to_python(ctx, k);
                if(!key){
                    Py_DECREF(d); return NULL;
                }
                PyObject* val = drj_to_python(ctx, v);
                if(!val){
                    Py_DECREF(d); Py_DECREF(key); return NULL;
                }
                PyDict_SetItem(d, key, val);
                Py_DECREF(key);
                Py_DECREF(val);
            }
            return d;
        }
        case DRJSON_NULL:
            Py_RETURN_NONE;
        case DRJSON_BOOL:
            return Py_NewRef(v.boolean?Py_True:Py_False);
    }
    Py_RETURN_NONE;
}

static
PyObject* _Nullable
pydrj_parse(PyObject* mod, PyObject* args, PyObject* kwargs){
    (void)mod;
    PyTypeObject* type = (PyTypeObject*)&DrjPyCtxType;
    DrjPyCtx* self = (DrjPyCtx*)type->tp_alloc(type, 0);
    if(!self) return NULL;
    self->ctx = (DrJsonContext){
        .allocator = drjson_stdc_allocator(),
    };
    PyObject* ret = DrjPyCtx_parse((PyObject*)self, args, kwargs);
    Py_DECREF(self);
    return ret;
}

static
PyObject* _Nullable
pydrj_load(PyObject* mod, PyObject* args, PyObject* kwargs){
    (void)mod;
    PyTypeObject* type = (PyTypeObject*)&DrjPyCtxType;
    DrjPyCtx* self = (DrjPyCtx*)type->tp_alloc(type, 0);
    if(!self) return NULL;
    self->ctx = (DrJsonContext){
        .allocator = drjson_stdc_allocator(),
    };
    PyObject* ret = DrjPyCtx_load((PyObject*)self, args, kwargs);
    Py_DECREF(self);
    return ret;
}
