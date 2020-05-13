/*
    pybind11/pytypes.h: Convenience wrapper classes for basic Python types

    Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "pybind11.h"
#include "buffer_info.h"
#include <utility>
#include <type_traits>

#include "pytypes.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)

NAMESPACE_BEGIN(detail)


NAMESPACE_END(detail)

/** \rst
    Holds a reference to a Python object (no reference counting)

    The `handle` class is a thin wrapper around an arbitrary Python object (i.e. a
    ``PyObject *`` in Python's C API). It does not perform any automatic reference
    counting and merely provides a basic C++ interface to various Python API functions.

    .. seealso::
        The `object` class inherits from `handle` and adds automatic reference
        counting features.
\endrst */
    /// The default constructor creates a handle with a ``nullptr``-valued pointer
    /// Creates a ``handle`` from the given raw Python object pointer
    handle::handle(PyObject *ptr) : m_ptr(ptr) { } // Allow implicit conversion from PyObject*

    /// Return the underlying ``PyObject *`` pointer
    PyObject *handle::ptr() const { return m_ptr; }
    PyObject *&handle::ptr() { return m_ptr; }

    /** \rst
        Manually increase the reference count of the Python object. Usually, it is
        preferable to use the `object` class which derives from `handle` and calls
        this function automatically. Returns a reference to itself.
    \endrst */
    const handle& handle::inc_ref() const & { Py_XINCREF(m_ptr); return *this; }

    /** \rst
        Manually decrease the reference count of the Python object. Usually, it is
        preferable to use the `object` class which derives from `handle` and calls
        this function automatically. Returns a reference to itself.
    \endrst */
    const handle& handle::dec_ref() const & { Py_XDECREF(m_ptr); return *this; }

    /** \rst
        Attempt to cast the Python object into the given C++ type. A `cast_error`
        will be throw upon failure.
    \endrst */
    /// Return ``true`` when the `handle` wraps a valid Python object
    handle::operator bool() const { return m_ptr != nullptr; }
    /** \rst
        Deprecated: Check that the underlying pointers are the same.
        Equivalent to ``obj1 is obj2`` in Python.
    \endrst */
    PYBIND11_DEPRECATED("Use obj1.is(obj2) instead")
    bool handle::operator==(const handle &h) const { return m_ptr == h.m_ptr; }
    PYBIND11_DEPRECATED("Use !obj1.is(obj2) instead")
    bool handle::operator!=(const handle &h) const { return m_ptr != h.m_ptr; }
    PYBIND11_DEPRECATED("Use handle::operator bool() instead")
    bool handle::check() const { return m_ptr != nullptr; }

/** \rst
    Holds a reference to a Python object (with reference counting)

    Like `handle`, the `object` class is a thin wrapper around an arbitrary Python
    object (i.e. a ``PyObject *`` in Python's C API). In contrast to `handle`, it
    optionally increases the object's reference count upon construction, and it
    *always* decreases the reference count when the `object` instance goes out of
    scope and is destructed. When using `object` instances consistently, it is much
    easier to get reference counting right at the first attempt.
\endrst */
    PYBIND11_DEPRECATED("Use reinterpret_borrow<object>() or reinterpret_steal<object>()")
    object::object(handle h, bool is_borrowed) : handle(h) { if (is_borrowed) inc_ref(); }
    /// Copy constructor; always increases the reference count
    object::object(const object &o) : handle(o) { inc_ref(); }
    /// Move constructor; steals the object from ``other`` and preserves its reference count
    object::object(object &&other) noexcept { m_ptr = other.m_ptr; other.m_ptr = nullptr; }
    /// Destructor; automatically calls `handle::dec_ref()`
    object::~object() { dec_ref(); }

    /** \rst
        Resets the internal pointer to ``nullptr`` without without decreasing the
        object's reference count. The function returns a raw handle to the original
        Python object.
    \endrst */
    handle object::release() {
      PyObject *tmp = m_ptr;
      m_ptr = nullptr;
      return handle(tmp);
    }

    object& object::operator=(const object &other) {
        other.inc_ref();
        dec_ref();
        m_ptr = other.m_ptr;
        return *this;
    }

    object& object::operator=(object &&other) noexcept {
        if (this != &other) {
            handle temp(m_ptr);
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            temp.dec_ref();
        }
        return *this;
    }

    object::object(handle h, borrowed_t) : handle(h) { inc_ref(); }
    object::object(handle h, stolen_t) : handle(h) { }

/// Fetch and hold an error which was already set in Python.  An instance of this is typically
/// thrown to propagate python-side errors back through C++ which can either be caught manually or
/// else falls back to the function dispatcher (which then raises the captured error back to
/// python).
    /// Constructs a new exception from the current Python error indicator, if any.  The current
    /// Python error indicator will be cleared.
    error_already_set::error_already_set() : std::runtime_error(detail::error_string()) {
        PyErr_Fetch(&m_type.ptr(), &m_value.ptr(), &m_trace.ptr());
    }

    /// Give the currently-held error back to Python, if any.  If there is currently a Python error
    /// already set it is cleared first.  After this call, the current object no longer stores the
    /// error variables (but the `.what()` string is still available).
    void error_already_set::restore() { PyErr_Restore(m_type.release().ptr(), m_value.release().ptr(), m_trace.release().ptr()); }

    // Does nothing; provided for backwards compatibility.
    PYBIND11_DEPRECATED("Use of error_already_set.clear() is deprecated")
    void error_already_set::clear() {}

    /// Check if the currently trapped error type matches the given Python exception class (or a
    /// subclass thereof).  May also be passed a tuple to search for any exception class matches in
    /// the given tuple.
    bool error_already_set::matches(handle exc) const { return PyErr_GivenExceptionMatches(m_type.ptr(), exc.ptr()); }

    const object& error_already_set::type() const { return m_type; }
    const object& error_already_set::value() const { return m_value; }
    const object& error_already_set::trace() const { return m_trace; }


/** \defgroup python_builtins _
    Unless stated otherwise, the following C++ functions behave the same
    as their Python counterparts.
 */

/** \ingroup python_builtins
    \rst
    Return true if ``obj`` is an instance of ``T``. Type ``T`` must be a subclass of
    `object` or a class which was exposed to Python as ``py::class_<T>``.
\endrst */



/// \ingroup python_builtins
/// Return true if ``obj`` is an instance of the ``type``.
bool isinstance(handle obj, handle type) {
    const auto result = PyObject_IsInstance(obj.ptr(), type.ptr());
    if (result == -1)
        throw error_already_set();
    return result != 0;
}

/// \addtogroup python_builtins
/// @{
bool hasattr(handle obj, handle name) {
    return PyObject_HasAttr(obj.ptr(), name.ptr()) == 1;
}

bool hasattr(handle obj, const char *name) {
    return PyObject_HasAttrString(obj.ptr(), name) == 1;
}

void delattr(handle obj, handle name) {
    if (PyObject_DelAttr(obj.ptr(), name.ptr()) != 0) { throw error_already_set(); }
}

void delattr(handle obj, const char *name) {
    if (PyObject_DelAttrString(obj.ptr(), name) != 0) { throw error_already_set(); }
}

object getattr(handle obj, handle name) {
    PyObject *result = PyObject_GetAttr(obj.ptr(), name.ptr());
    if (!result) { throw error_already_set(); }
    return reinterpret_steal<object>(result);
}

object getattr(handle obj, const char *name) {
    PyObject *result = PyObject_GetAttrString(obj.ptr(), name);
    if (!result) { throw error_already_set(); }
    return reinterpret_steal<object>(result);
}

object getattr(handle obj, handle name, handle default_) {
    if (PyObject *result = PyObject_GetAttr(obj.ptr(), name.ptr())) {
        return reinterpret_steal<object>(result);
    } else {
        PyErr_Clear();
        return reinterpret_borrow<object>(default_);
    }
}

object getattr(handle obj, const char *name, handle default_) {
    if (PyObject *result = PyObject_GetAttrString(obj.ptr(), name)) {
        return reinterpret_steal<object>(result);
    } else {
        PyErr_Clear();
        return reinterpret_borrow<object>(default_);
    }
}

void setattr(handle obj, handle name, handle value) {
    if (PyObject_SetAttr(obj.ptr(), name.ptr(), value.ptr()) != 0) { throw error_already_set(); }
}

void setattr(handle obj, const char *name, handle value) {
    if (PyObject_SetAttrString(obj.ptr(), name, value.ptr()) != 0) { throw error_already_set(); }
}

ssize_t hash(handle obj) {
    auto h = PyObject_Hash(obj.ptr());
    if (h == -1) { throw error_already_set(); }
    return h;
}

/// @} python_builtins

NAMESPACE_BEGIN(detail)
handle get_function(handle value) {
    if (value) {
#if PY_MAJOR_VERSION >= 3
        if (PyInstanceMethod_Check(value.ptr()))
            value = PyInstanceMethod_GET_FUNCTION(value.ptr());
        else
#endif
        if (PyMethod_Check(value.ptr()))
            value = PyMethod_GET_FUNCTION(value.ptr());
    }
    return value;
}

// Match a PyObject*, which we want to convert directly to handle via its converting constructor
handle object_or_cast(PyObject *ptr) { return ptr; }

NAMESPACE_BEGIN(accessor_policies)
    object obj_attr::get(handle obj, handle key) { return getattr(obj, key); }
    void obj_attr::set(handle obj, handle key, handle val) { setattr(obj, key, val); }

    object str_attr::get(handle obj, const char *key) { return getattr(obj, key); }
    void str_attr::set(handle obj, const char *key, handle val) { setattr(obj, key, val); }

    object generic_item::get(handle obj, handle key) {
        PyObject *result = PyObject_GetItem(obj.ptr(), key.ptr());
        if (!result) { throw error_already_set(); }
        return reinterpret_steal<object>(result);
    }
    void generic_item::set(handle obj, handle key, handle val) {
        if (PyObject_SetItem(obj.ptr(), key.ptr(), val.ptr()) != 0) { throw error_already_set(); }
    }

    object sequence_item::get(handle obj, size_t index) {
        PyObject *result = PySequence_GetItem(obj.ptr(), static_cast<ssize_t>(index));
        if (!result) { throw error_already_set(); }
        return reinterpret_steal<object>(result);
    }

    void sequence_item::set(handle obj, size_t index, handle val) {
        // PySequence_SetItem does not steal a reference to 'val'
        if (PySequence_SetItem(obj.ptr(), static_cast<ssize_t>(index), val.ptr()) != 0) {
            throw error_already_set();
        }
    }

    object list_item::get(handle obj, size_t index) {
        PyObject *result = PyList_GetItem(obj.ptr(), static_cast<ssize_t>(index));
        if (!result) { throw error_already_set(); }
        return reinterpret_borrow<object>(result);
    }

    void list_item::set(handle obj, size_t index, handle val) {
        // PyList_SetItem steals a reference to 'val'
        if (PyList_SetItem(obj.ptr(), static_cast<ssize_t>(index), val.inc_ref().ptr()) != 0) {
            throw error_already_set();
        }
    }

    object tuple_item::get(handle obj, size_t index) {
        PyObject *result = PyTuple_GetItem(obj.ptr(), static_cast<ssize_t>(index));
        if (!result) { throw error_already_set(); }
        return reinterpret_borrow<object>(result);
    }

    void tuple_item::set(handle obj, size_t index, handle val) {
        // PyTuple_SetItem steals a reference to 'val'
        if (PyTuple_SetItem(obj.ptr(), static_cast<ssize_t>(index), val.inc_ref().ptr()) != 0) {
            throw error_already_set();
        }
    }
NAMESPACE_END(accessor_policies)

NAMESPACE_BEGIN(iterator_policies)

    sequence_fast_readonly::sequence_fast_readonly(handle obj, ssize_t n) : ptr(PySequence_Fast_ITEMS(obj.ptr()) + n) { }

    sequence_fast_readonly::reference sequence_fast_readonly::dereference() const { return *ptr; }
    void sequence_fast_readonly::increment() { ++ptr; }
    void sequence_fast_readonly::decrement() { --ptr; }
    void sequence_fast_readonly::advance(ssize_t n) { ptr += n; }
    bool sequence_fast_readonly::equal(const sequence_fast_readonly &b) const { return ptr == b.ptr; }
    ssize_t sequence_fast_readonly::distance_to(const sequence_fast_readonly &b) const { return ptr - b.ptr; }

    sequence_slow_readwrite::sequence_slow_readwrite(handle obj, ssize_t index) : obj(obj), index(index) { }
    sequence_slow_readwrite::reference sequence_slow_readwrite::dereference() const { return {obj, static_cast<size_t>(index)}; }
    void sequence_slow_readwrite::increment() { ++index; }
    void sequence_slow_readwrite::decrement() { --index; }
    void sequence_slow_readwrite::advance(ssize_t n) { index += n; }
    bool sequence_slow_readwrite::equal(const sequence_slow_readwrite &b) const { return index == b.index; }
    ssize_t sequence_slow_readwrite::distance_to(const sequence_slow_readwrite &b) const { return index - b.index; }

    dict_readonly::dict_readonly(handle obj, ssize_t pos) : obj(obj), pos(pos) { increment(); }
    dict_readonly::reference dict_readonly::dereference() const { return {key, value}; }
    void dict_readonly::increment() { if (!PyDict_Next(obj.ptr(), &pos, &key, &value)) { pos = -1; } }
    bool dict_readonly::equal(const dict_readonly &b) const { return pos == b.pos; }

NAMESPACE_END(iterator_policies)

bool PyIterable_Check(PyObject *obj) {
    PyObject *iter = PyObject_GetIter(obj);
    if (iter) {
        Py_DECREF(iter);
        return true;
    } else {
        PyErr_Clear();
        return false;
    }
}

bool PyNone_Check(PyObject *o) { return o == Py_None; }
#if PY_MAJOR_VERSION >= 3
bool PyEllipsis_Check(PyObject *o) { return o == Py_Ellipsis; }
#endif

bool PyUnicode_Check_Permissive(PyObject *o) { return PyUnicode_Check(o) || PYBIND11_BYTES_CHECK(o); }

bool PyStaticMethod_Check(PyObject *o) { return o->ob_type == &PyStaticMethod_Type; }

    kwargs_proxy::kwargs_proxy(handle h) : handle(h) { }

    args_proxy::args_proxy(handle h) : handle(h) { }
    kwargs_proxy args_proxy::operator*() const { return kwargs_proxy(*this); }

NAMESPACE_END(detail)

/// \addtogroup pytypes
/// @{

/** \rst
    Wraps a Python iterator so that it can also be used as a C++ input iterator

    Caveat: copying an iterator does not (and cannot) clone the internal
    state of the Python iterable. This also applies to the post-increment
    operator. This iterator should only be used to retrieve the current
    value using ``operator*()``.
\endrst */

iterator& iterator::operator++() {
    advance();
    return *this;
}

iterator iterator::operator++(int) {
    auto rv = *this;
    advance();
    return rv;
}

iterator::reference iterator::operator*() const {
    if (m_ptr && !value.ptr()) {
        auto& self = const_cast<iterator &>(*this);
        self.advance();
    }
    return value;
}

iterator::pointer iterator::operator->() const { operator*(); return &value; }

/** \rst
        The value which marks the end of the iteration. ``it == iterator::sentinel()``
        is equivalent to catching ``StopIteration`` in Python.

        .. code-block:: cpp

            void foo(py::iterator it) {
                while (it != py::iterator::sentinel()) {
                // use `*it`
                ++it;
                }
            }
\endrst */
iterator iterator::sentinel() { return {}; }

bool operator==(const iterator &a, const iterator &b) { return a->ptr() == b->ptr(); }
bool operator!=(const iterator &a, const iterator &b) { return a->ptr() != b->ptr(); }

void iterator::advance() {
    value = reinterpret_steal<object>(PyIter_Next(m_ptr));
    if (PyErr_Occurred()) { throw error_already_set(); }
}

str::str(const char *c, size_t n)
    : object(PyUnicode_FromStringAndSize(c, (ssize_t) n), stolen_t{}) {
    if (!m_ptr) pybind11_fail("Could not allocate string object!");
}

// 'explicit' is explicitly omitted from the following constructors to allow implicit conversion to py::str from C++ string-like objects
str::str(const char *c)
    : object(PyUnicode_FromString(c), stolen_t{}) {
    if (!m_ptr) pybind11_fail("Could not allocate string object!");
}

str::str(const std::string &s) : str(s.data(), s.size()) { }

/** \rst
    Return a string representation of the object. This is analogous to
    the ``str()`` function in Python.
\endrst */
str::str(handle h) : object(raw_str(h.ptr()), stolen_t{}) { }

str::operator std::string() const {
    object temp = *this;
    if (PyUnicode_Check(m_ptr)) {
        temp = reinterpret_steal<object>(PyUnicode_AsUTF8String(m_ptr));
        if (!temp)
            pybind11_fail("Unable to extract string contents! (encoding issue)");
    }
    char *buffer;
    ssize_t length;
    if (PYBIND11_BYTES_AS_STRING_AND_SIZE(temp.ptr(), &buffer, &length))
        pybind11_fail("Unable to extract string contents! (invalid type)");
    return std::string(buffer, (size_t) length);
}

PyObject *str::raw_str(PyObject *op) {
    PyObject *str_value = PyObject_Str(op);
#if PY_MAJOR_VERSION < 3
    if (!str_value) throw error_already_set();
    PyObject *unicode = PyUnicode_FromEncodedObject(str_value, "utf-8", nullptr);
    Py_XDECREF(str_value); str_value = unicode;
#endif
    return str_value;
}
/// @} pytypes

namespace literals {
/** \rst
    String literal version of `str`
 \endrst */
str operator"" _s(const char *s, size_t size) { return {s, size}; }
}

/// \addtogroup pytypes
/// @{
    // Allow implicit conversion:
    bytes::bytes(const char *c)
        : object(PYBIND11_BYTES_FROM_STRING(c), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate bytes object!");
    }

    bytes::bytes(const char *c, size_t n)
        : object(PYBIND11_BYTES_FROM_STRING_AND_SIZE(c, (ssize_t) n), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate bytes object!");
    }

    // Allow implicit conversion:
    bytes::bytes(const std::string &s) : bytes(s.data(), s.size()) { }

    bytes::operator std::string() const {
        char *buffer;
        ssize_t length;
        if (PYBIND11_BYTES_AS_STRING_AND_SIZE(m_ptr, &buffer, &length))
            pybind11_fail("Unable to extract bytes contents!");
        return std::string(buffer, (size_t) length);
    }

bytes::bytes(const pybind11::str &s) {
    object temp = s;
    if (PyUnicode_Check(s.ptr())) {
        temp = reinterpret_steal<object>(PyUnicode_AsUTF8String(s.ptr()));
        if (!temp)
            pybind11_fail("Unable to extract string contents! (encoding issue)");
    }
    char *buffer;
    ssize_t length;
    if (PYBIND11_BYTES_AS_STRING_AND_SIZE(temp.ptr(), &buffer, &length))
        pybind11_fail("Unable to extract string contents! (invalid type)");
    auto obj = reinterpret_steal<object>(PYBIND11_BYTES_FROM_STRING_AND_SIZE(buffer, length));
    if (!obj)
        pybind11_fail("Could not allocate bytes object!");
    m_ptr = obj.release().ptr();
}

str::str(const bytes& b) {
    char *buffer;
    ssize_t length;
    if (PYBIND11_BYTES_AS_STRING_AND_SIZE(b.ptr(), &buffer, &length))
        pybind11_fail("Unable to extract bytes contents!");
    auto obj = reinterpret_steal<object>(PyUnicode_FromStringAndSize(buffer, (ssize_t) length));
    if (!obj)
        pybind11_fail("Could not allocate string object!");
    m_ptr = obj.release().ptr();
}

    none::none() : object(Py_None, borrowed_t{}) { }

#if PY_MAJOR_VERSION >= 3
ellipsis::ellipsis() : object(Py_Ellipsis, borrowed_t{}) { }
#endif

    bool_::bool_() : object(Py_False, borrowed_t{}) { }
    // Allow implicit conversion from and to `bool`:
    bool_::bool_(bool value) : object(value ? Py_True : Py_False, borrowed_t{}) { }
    bool_::operator bool() const { return m_ptr && PyLong_AsLong(m_ptr) != 0; }

    /// Return the truth value of an object -- always returns a new reference
    PyObject *bool_::raw_bool(PyObject *op) {
        const auto value = PyObject_IsTrue(op);
        if (value == -1) return nullptr;
        return handle(value ? Py_True : Py_False).inc_ref().ptr();
    }

    int_::int_() : object(PyLong_FromLong(0), stolen_t{}) { }

    float_::float_(float value) : object(PyFloat_FromDouble((double) value), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate float object!");
    }
    float_::float_(double value) : object(PyFloat_FromDouble((double) value), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate float object!");
    }
    float_::operator float() const { return (float) PyFloat_AsDouble(m_ptr); }
    float_::operator double() const { return (double) PyFloat_AsDouble(m_ptr); }

    weakref::weakref(handle obj, handle callback)
        : object(PyWeakref_NewRef(obj.ptr(), callback.ptr()), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate weak reference!");
    }

    slice::slice(ssize_t start_, ssize_t stop_, ssize_t step_) {
        int_ start(start_), stop(stop_), step(step_);
        m_ptr = PySlice_New(start.ptr(), stop.ptr(), step.ptr());
        if (!m_ptr) pybind11_fail("Could not allocate slice object!");
    }
    bool slice::compute(size_t length, size_t *start, size_t *stop, size_t *step,
                 size_t *slicelength) const {
        return PySlice_GetIndicesEx((PYBIND11_SLICE_OBJECT *) m_ptr,
                                    (ssize_t) length, (ssize_t *) start,
                                    (ssize_t *) stop, (ssize_t *) step,
                                    (ssize_t *) slicelength) == 0;
    }
    bool slice::compute(ssize_t length, ssize_t *start, ssize_t *stop, ssize_t *step,
      ssize_t *slicelength) const {
      return PySlice_GetIndicesEx((PYBIND11_SLICE_OBJECT *) m_ptr,
          length, start,
          stop, step,
          slicelength) == 0;
    }

    capsule::capsule(PyObject *ptr, bool is_borrowed) : object(is_borrowed ? object(ptr, borrowed_t{}) : object(ptr, stolen_t{})) { }

    capsule::capsule(const void *value, const char *name, void (*destructor)(PyObject *))
        : object(PyCapsule_New(const_cast<void *>(value), name, destructor), stolen_t{}) {
        if (!m_ptr)
            pybind11_fail("Could not allocate capsule object!");
    }

    PYBIND11_DEPRECATED("Please pass a destructor that takes a void pointer as input")
    capsule::capsule(const void *value, void (*destruct)(PyObject *))
        : object(PyCapsule_New(const_cast<void*>(value), nullptr, destruct), stolen_t{}) {
        if (!m_ptr)
            pybind11_fail("Could not allocate capsule object!");
    }

    capsule::capsule(const void *value, void (*destructor)(void *)) {
        m_ptr = PyCapsule_New(const_cast<void *>(value), nullptr, [](PyObject *o) {
            auto destructor = reinterpret_cast<void (*)(void *)>(PyCapsule_GetContext(o));
            void *ptr = PyCapsule_GetPointer(o, nullptr);
            destructor(ptr);
        });

        if (!m_ptr)
            pybind11_fail("Could not allocate capsule object!");

        if (PyCapsule_SetContext(m_ptr, (void *) destructor) != 0)
            pybind11_fail("Could not set capsule context!");
    }

    capsule::capsule(void (*destructor)()) {
        m_ptr = PyCapsule_New(reinterpret_cast<void *>(destructor), nullptr, [](PyObject *o) {
            auto destructor = reinterpret_cast<void (*)()>(PyCapsule_GetPointer(o, nullptr));
            destructor();
        });

        if (!m_ptr)
            pybind11_fail("Could not allocate capsule object!");
    }

    const char *capsule::name() const { return PyCapsule_GetName(m_ptr); }

    tuple::tuple(size_t size) : object(PyTuple_New((ssize_t) size), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate tuple object!");
    }
    size_t tuple::size() const { return (size_t) PyTuple_Size(m_ptr); }
    bool tuple::empty() const { return size() == 0; }
    detail::tuple_accessor tuple::operator[](size_t index) const { return {*this, index}; }
    detail::item_accessor tuple::operator[](handle h) const { return object::operator[](h); }
    detail::tuple_iterator tuple::begin() const { return {*this, 0}; }
    detail::tuple_iterator tuple::end() const { return {*this, PyTuple_GET_SIZE(m_ptr)}; }

    dict::dict() : object(PyDict_New(), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate dict object!");
    }

    size_t dict::size() const { return (size_t) PyDict_Size(m_ptr); }
    bool dict::empty() const { return size() == 0; }
    detail::dict_iterator dict::begin() const { return {*this, 0}; }
    detail::dict_iterator dict::end() const { return {}; }
    void dict::clear() const { PyDict_Clear(ptr()); }

    PyObject *dict::raw_dict(PyObject *op) {
        if (PyDict_Check(op))
            return handle(op).inc_ref().ptr();
        return PyObject_CallFunctionObjArgs((PyObject *) &PyDict_Type, op, nullptr);
    }

    size_t sequence::size() const { return (size_t) PySequence_Size(m_ptr); }
    bool sequence::empty() const { return size() == 0; }
    detail::sequence_accessor sequence::operator[](size_t index) const { return {*this, index}; }
    detail::item_accessor sequence::operator[](handle h) const { return object::operator[](h); }
    detail::sequence_iterator sequence::begin() const { return {*this, 0}; }
    detail::sequence_iterator sequence::end() const { return {*this, PySequence_Size(m_ptr)}; }

    list::list(size_t size) : object(PyList_New((ssize_t) size), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate list object!");
    }
    size_t list::size() const { return (size_t) PyList_Size(m_ptr); }
    bool list::empty() const { return size() == 0; }
    detail::list_accessor list::operator[](size_t index) const { return {*this, index}; }
    detail::item_accessor list::operator[](handle h) const { return object::operator[](h); }
    detail::list_iterator list::begin() const { return {*this, 0}; }
    detail::list_iterator list::end() const { return {*this, PyList_GET_SIZE(m_ptr)}; }

    set::set() : object(PySet_New(nullptr), stolen_t{}) {
        if (!m_ptr) pybind11_fail("Could not allocate set object!");
    }
    size_t set::size() const { return (size_t) PySet_Size(m_ptr); }
    bool set::empty() const { return size() == 0; }
    void set::clear() const { PySet_Clear(m_ptr); }

    handle function::cpp_function() const {
        handle fun = detail::get_function(m_ptr);
        if (fun && PyCFunction_Check(fun.ptr()))
            return fun;
        return handle();
    }
    bool function::is_cpp_function() const { return (bool) cpp_function(); }


    buffer_info buffer::request(bool writable) const {
        int flags = PyBUF_STRIDES | PyBUF_FORMAT;
        if (writable) flags |= PyBUF_WRITABLE;
        Py_buffer *view = new Py_buffer();
        if (PyObject_GetBuffer(m_ptr, view, flags) != 0) {
            delete view;
            throw error_already_set();
        }
        return buffer_info(view);
    }

    memoryview::memoryview(const buffer_info& info) {
        Py_buffer buf { };
        // Py_buffer uses signed sizes, strides and shape!..
        std::vector<Py_ssize_t> py_strides { };
        std::vector<Py_ssize_t> py_shape { };
        buf.buf = info.ptr;
        buf.itemsize = info.itemsize;
        buf.format = const_cast<char *>(info.format.c_str());
        buf.ndim = (int) info.ndim;
        buf.len = info.size;
        py_strides.clear();
        py_shape.clear();
        for (size_t i = 0; i < (size_t) info.ndim; ++i) {
            py_strides.push_back(info.strides[i]);
            py_shape.push_back(info.shape[i]);
        }
        buf.strides = py_strides.data();
        buf.shape = py_shape.data();
        buf.suboffsets = nullptr;
        buf.readonly = false;
        buf.internal = nullptr;

        m_ptr = PyMemoryView_FromBuffer(&buf);
        if (!m_ptr)
            pybind11_fail("Unable to create memoryview from buffer descriptor");
    }

/// @} pytypes

/// \addtogroup python_builtins
/// @{
size_t len(handle h) {
    ssize_t result = PyObject_Length(h.ptr());
    if (result < 0)
        pybind11_fail("Unable to compute length of object");
    return (size_t) result;
}

size_t len_hint(handle h) {
#if PY_VERSION_HEX >= 0x03040000
    ssize_t result = PyObject_LengthHint(h.ptr(), 0);
#else
    ssize_t result = PyObject_Length(h.ptr());
#endif
    if (result < 0) {
        // Sometimes a length can't be determined at all (eg generators)
        // In which case simply return 0
        PyErr_Clear();
        return 0;
    }
    return (size_t) result;
}

str repr(handle h) {
    PyObject *str_value = PyObject_Repr(h.ptr());
    if (!str_value) throw error_already_set();
#if PY_MAJOR_VERSION < 3
    PyObject *unicode = PyUnicode_FromEncodedObject(str_value, "utf-8", nullptr);
    Py_XDECREF(str_value); str_value = unicode;
    if (!str_value) throw error_already_set();
#endif
    return reinterpret_steal<str>(str_value);
}

iterator iter(handle obj) {
    PyObject *result = PyObject_GetIter(obj.ptr());
    if (!result) { throw error_already_set(); }
    return reinterpret_steal<iterator>(result);
}
/// @} python_builtins

NAMESPACE_END(PYBIND11_NAMESPACE)
