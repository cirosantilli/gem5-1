/*
    pybind11/cast.h: Partial template specializations to cast between
    C++ and Python types

    Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "pytypes.h"
#include "detail/typeid.h"
#include "detail/descr.h"
#include "detail/internals.h"
#include <array>
#include <limits>
#include <tuple>
#include <type_traits>

#include "cast.h"

#ifdef PYBIND11_HAS_STRING_VIEW
#include <string_view>
#endif

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)
NAMESPACE_BEGIN(detail)

    loader_life_support::loader_life_support() {
        get_internals().loader_patient_stack.push_back(nullptr);
    }

    /// ... and destroyed after it returns
    loader_life_support::~loader_life_support() {
        auto &stack = get_internals().loader_patient_stack;
        if (stack.empty())
            pybind11_fail("loader_life_support: internal error");

        auto ptr = stack.back();
        stack.pop_back();
        Py_CLEAR(ptr);

        // A heuristic to reduce the stack's capacity (e.g. after long recursive calls)
        if (stack.capacity() > 16 && stack.size() != 0 && stack.capacity() / stack.size() > 2)
            stack.shrink_to_fit();
    }

    /// This can only be used inside a pybind11-bound function, either by `argument_loader`
    /// at argument preparation time or by `py::cast()` at execution time.
    PYBIND11_NOINLINE void loader_life_support::add_patient(handle h) {
        auto &stack = get_internals().loader_patient_stack;
        if (stack.empty())
            throw cast_error("When called outside a bound function, py::cast() cannot "
                             "do Python -> C++ conversions which require the creation "
                             "of temporary values");

        auto &list_ptr = stack.back();
        if (list_ptr == nullptr) {
            list_ptr = PyList_New(1);
            if (!list_ptr)
                pybind11_fail("loader_life_support: error allocating list");
            PyList_SET_ITEM(list_ptr, 0, h.inc_ref().ptr());
        } else {
            auto result = PyList_Append(list_ptr, h.ptr());
            if (result == -1)
                pybind11_fail("loader_life_support: error adding patient");
        }
    }

// Gets the cache entry for the given type, creating it if necessary.  The return value is the pair
// returned by emplace, i.e. an iterator for the entry and a bool set to `true` if the entry was
// just created.
std::pair<decltype(internals::registered_types_py)::iterator, bool> all_type_info_get_cache(PyTypeObject *type);

// Populates a just-created cache entry.
PYBIND11_NOINLINE void all_type_info_populate(PyTypeObject *t, std::vector<type_info *> &bases) {
    std::vector<PyTypeObject *> check;
    for (handle parent : reinterpret_borrow<tuple>(t->tp_bases))
        check.push_back((PyTypeObject *) parent.ptr());

    auto const &type_dict = get_internals().registered_types_py;
    for (size_t i = 0; i < check.size(); i++) {
        auto type = check[i];
        // Ignore Python2 old-style class super type:
        if (!PyType_Check((PyObject *) type)) continue;

        // Check `type` in the current set of registered python types:
        auto it = type_dict.find(type);
        if (it != type_dict.end()) {
            // We found a cache entry for it, so it's either pybind-registered or has pre-computed
            // pybind bases, but we have to make sure we haven't already seen the type(s) before: we
            // want to follow Python/virtual C++ rules that there should only be one instance of a
            // common base.
            for (auto *tinfo : it->second) {
                // NB: Could use a second set here, rather than doing a linear search, but since
                // having a large number of immediate pybind11-registered types seems fairly
                // unlikely, that probably isn't worthwhile.
                bool found = false;
                for (auto *known : bases) {
                    if (known == tinfo) { found = true; break; }
                }
                if (!found) bases.push_back(tinfo);
            }
        }
        else if (type->tp_bases) {
            // It's some python type, so keep follow its bases classes to look for one or more
            // registered types
            if (i + 1 == check.size()) {
                // When we're at the end, we can pop off the current element to avoid growing
                // `check` when adding just one base (which is typical--i.e. when there is no
                // multiple inheritance)
                check.pop_back();
                i--;
            }
            for (handle parent : reinterpret_borrow<tuple>(type->tp_bases))
                check.push_back((PyTypeObject *) parent.ptr());
        }
    }
}

/**
 * Extracts vector of type_info pointers of pybind-registered roots of the given Python type.  Will
 * be just 1 pybind type for the Python type of a pybind-registered class, or for any Python-side
 * derived class that uses single inheritance.  Will contain as many types as required for a Python
 * class that uses multiple inheritance to inherit (directly or indirectly) from multiple
 * pybind-registered classes.  Will be empty if neither the type nor any base classes are
 * pybind-registered.
 *
 * The value is cached for the lifetime of the Python type.
 */
const std::vector<detail::type_info *> &all_type_info(PyTypeObject *type) {
    auto ins = all_type_info_get_cache(type);
    if (ins.second)
        // New cache entry: populate it
        all_type_info_populate(type, ins.first->second);

    return ins.first->second;
}

/**
 * Gets a single pybind11 type info for a python type.  Returns nullptr if neither the type nor any
 * ancestors are pybind11-registered.  Throws an exception if there are multiple bases--use
 * `all_type_info` instead if you want to support multiple bases.
 */
PYBIND11_NOINLINE detail::type_info* get_type_info(PyTypeObject *type) {
    auto &bases = all_type_info(type);
    if (bases.size() == 0)
        return nullptr;
    if (bases.size() > 1)
        pybind11_fail("pybind11::detail::get_type_info: type has multiple pybind11-registered bases");
    return bases.front();
}

detail::type_info *get_local_type_info(const std::type_index &tp) {
    auto &locals = registered_local_types_cpp();
    auto it = locals.find(tp);
    if (it != locals.end())
        return it->second;
    return nullptr;
}

detail::type_info *get_global_type_info(const std::type_index &tp) {
    auto &types = get_internals().registered_types_cpp;
    auto it = types.find(tp);
    if (it != types.end())
        return it->second;
    return nullptr;
}

/// Return the type info for a given C++ type; on lookup failure can either throw or return nullptr.
PYBIND11_NOINLINE detail::type_info *get_type_info(const std::type_index &tp,
                                                          bool throw_if_missing) {
    if (auto ltype = get_local_type_info(tp))
        return ltype;
    if (auto gtype = get_global_type_info(tp))
        return gtype;

    if (throw_if_missing) {
        std::string tname = tp.name();
        detail::clean_type_id(tname);
        pybind11_fail("pybind11::detail::get_type_info: unable to find type info for \"" + tname + "\"");
    }
    return nullptr;
}

PYBIND11_NOINLINE handle get_type_handle(const std::type_info &tp, bool throw_if_missing) {
    detail::type_info *type_info = get_type_info(tp, throw_if_missing);
    return handle(type_info ? ((PyObject *) type_info->type) : nullptr);
}

// Main constructor for a found value/holder:
value_and_holder::value_and_holder(instance *i, const detail::type_info *type, size_t vpos, size_t index) :
    inst{i}, index{index}, type{type},
    vh{inst->simple_layout ? inst->simple_value_holder : &inst->nonsimple.values_and_holders[vpos]}
{}

// Default constructor (used to signal a value-and-holder not found by get_value_and_holder())
value_and_holder::value_and_holder() {}

// Used for past-the-end iterator
value_and_holder::value_and_holder(size_t index) : index{index} {}

// True if this `value_and_holder` has a non-null value pointer
value_and_holder::operator bool() const { return value_ptr(); }

bool value_and_holder::holder_constructed() const {
    return inst->simple_layout
        ? inst->simple_holder_constructed
        : inst->nonsimple.status[index] & instance::status_holder_constructed;
}
void value_and_holder::set_holder_constructed(bool v) {
    if (inst->simple_layout)
        inst->simple_holder_constructed = v;
    else if (v)
        inst->nonsimple.status[index] |= instance::status_holder_constructed;
    else
        inst->nonsimple.status[index] &= (uint8_t) ~instance::status_holder_constructed;
}
bool value_and_holder::instance_registered() const {
    return inst->simple_layout
        ? inst->simple_instance_registered
        : inst->nonsimple.status[index] & instance::status_instance_registered;
}
void value_and_holder::set_instance_registered(bool v) {
    if (inst->simple_layout)
        inst->simple_instance_registered = v;
    else if (v)
        inst->nonsimple.status[index] |= instance::status_instance_registered;
    else
        inst->nonsimple.status[index] &= (uint8_t) ~instance::status_instance_registered;
}

// Container for accessing and iterating over an instance's values/holders
values_and_holders::values_and_holders(instance *inst) : inst{inst}, tinfo(all_type_info(Py_TYPE(inst))) {}

        values_and_holders::iterator::iterator(instance *inst, const type_vec *tinfo)
            : inst{inst}, types{tinfo},
            curr(inst /* instance */,
                 types->empty() ? nullptr : (*types)[0] /* type info */,
                 0, /* vpos: (non-simple types only): the first vptr comes first */
                 0 /* index */)
        {}
        // Past-the-end iterator:
        values_and_holders::iterator::iterator(size_t end) : curr(end) {}

        bool values_and_holders::iterator::operator==(const iterator &other) { return curr.index == other.curr.index; }
        bool values_and_holders::iterator::operator!=(const iterator &other) { return curr.index != other.curr.index; }
        values_and_holders::iterator &values_and_holders::iterator::operator++() {
            if (!inst->simple_layout)
                curr.vh += 1 + (*types)[curr.index]->holder_size_in_ptrs;
            ++curr.index;
            curr.type = curr.index < types->size() ? (*types)[curr.index] : nullptr;
            return *this;
        }
        value_and_holder &values_and_holders::iterator::operator*() { return curr; }
        value_and_holder *values_and_holders::iterator::operator->() { return &curr; }

    values_and_holders::iterator values_and_holders::begin() { return iterator(inst, &tinfo); }
    values_and_holders::iterator values_and_holders::end() { return iterator(tinfo.size()); }

    values_and_holders::iterator values_and_holders::find(const type_info *find_type) {
        auto it = begin(), endit = end();
        while (it != endit && it->type != find_type) ++it;
        return it;
    }

    size_t values_and_holders::size() { return tinfo.size(); }

/**
 * Extracts C++ value and holder pointer references from an instance (which may contain multiple
 * values/holders for python-side multiple inheritance) that match the given type.  Throws an error
 * if the given type (or ValueType, if omitted) is not a pybind11 base of the given instance.  If
 * `find_type` is omitted (or explicitly specified as nullptr) the first value/holder are returned,
 * regardless of type (and the resulting .type will be nullptr).
 *
 * The returned object should be short-lived: in particular, it must not outlive the called-upon
 * instance.
 */
PYBIND11_NOINLINE value_and_holder instance::get_value_and_holder(const type_info *find_type /*= nullptr default in common.h*/, bool throw_if_missing /*= true in common.h*/) {
    // Optimize common case:
    if (!find_type || Py_TYPE(this) == find_type->type)
        return value_and_holder(this, find_type, 0, 0);

    detail::values_and_holders vhs(this);
    auto it = vhs.find(find_type);
    if (it != vhs.end())
        return *it;

    if (!throw_if_missing)
        return value_and_holder();

#if defined(NDEBUG)
    pybind11_fail("pybind11::detail::instance::get_value_and_holder: "
            "type is not a pybind11 base of the given instance "
            "(compile in debug mode for type details)");
#else
    pybind11_fail("pybind11::detail::instance::get_value_and_holder: `" +
            std::string(find_type->type->tp_name) + "' is not a pybind11 base of the given `" +
            std::string(Py_TYPE(this)->tp_name) + "' instance");
#endif
}

PYBIND11_NOINLINE void instance::allocate_layout() {
    auto &tinfo = all_type_info(Py_TYPE(this));

    const size_t n_types = tinfo.size();

    if (n_types == 0)
        pybind11_fail("instance allocation failed: new instance has no pybind11-registered base types");

    simple_layout =
        n_types == 1 && tinfo.front()->holder_size_in_ptrs <= instance_simple_holder_in_ptrs();

    // Simple path: no python-side multiple inheritance, and a small-enough holder
    if (simple_layout) {
        simple_value_holder[0] = nullptr;
        simple_holder_constructed = false;
        simple_instance_registered = false;
    }
    else { // multiple base types or a too-large holder
        // Allocate space to hold: [v1*][h1][v2*][h2]...[bb...] where [vN*] is a value pointer,
        // [hN] is the (uninitialized) holder instance for value N, and [bb...] is a set of bool
        // values that tracks whether each associated holder has been initialized.  Each [block] is
        // padded, if necessary, to an integer multiple of sizeof(void *).
        size_t space = 0;
        for (auto t : tinfo) {
            space += 1; // value pointer
            space += t->holder_size_in_ptrs; // holder instance
        }
        size_t flags_at = space;
        space += size_in_ptrs(n_types); // status bytes (holder_constructed and instance_registered)

        // Allocate space for flags, values, and holders, and initialize it to 0 (flags and values,
        // in particular, need to be 0).  Use Python's memory allocation functions: in Python 3.6
        // they default to using pymalloc, which is designed to be efficient for small allocations
        // like the one we're doing here; in earlier versions (and for larger allocations) they are
        // just wrappers around malloc.
#if PY_VERSION_HEX >= 0x03050000
        nonsimple.values_and_holders = (void **) PyMem_Calloc(space, sizeof(void *));
        if (!nonsimple.values_and_holders) throw std::bad_alloc();
#else
        nonsimple.values_and_holders = (void **) PyMem_New(void *, space);
        if (!nonsimple.values_and_holders) throw std::bad_alloc();
        std::memset(nonsimple.values_and_holders, 0, space * sizeof(void *));
#endif
        nonsimple.status = reinterpret_cast<uint8_t *>(&nonsimple.values_and_holders[flags_at]);
    }
    owned = true;
}

PYBIND11_NOINLINE void instance::deallocate_layout() {
    if (!simple_layout)
        PyMem_Free(nonsimple.values_and_holders);
}

PYBIND11_NOINLINE bool isinstance_generic(handle obj, const std::type_info &tp) {
    handle type = detail::get_type_handle(tp, false);
    if (!type)
        return false;
    return isinstance(obj, type);
}

PYBIND11_NOINLINE std::string error_string() {
    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown internal error occurred");
        return "Unknown internal error occurred";
    }

    error_scope scope; // Preserve error state

    std::string errorString;
    if (scope.type) {
        errorString += handle(scope.type).attr("__name__").cast<std::string>();
        errorString += ": ";
    }
    if (scope.value)
        errorString += (std::string) str(scope.value);

    PyErr_NormalizeException(&scope.type, &scope.value, &scope.trace);

#if PY_MAJOR_VERSION >= 3
    if (scope.trace != nullptr)
        PyException_SetTraceback(scope.value, scope.trace);
#endif

#if !defined(PYPY_VERSION)
    if (scope.trace) {
        PyTracebackObject *trace = (PyTracebackObject *) scope.trace;

        /* Get the deepest trace possible */
        while (trace->tb_next)
            trace = trace->tb_next;

        PyFrameObject *frame = trace->tb_frame;
        errorString += "\n\nAt:\n";
        while (frame) {
            int lineno = PyFrame_GetLineNumber(frame);
            errorString +=
                "  " + handle(frame->f_code->co_filename).cast<std::string>() +
                "(" + std::to_string(lineno) + "): " +
                handle(frame->f_code->co_name).cast<std::string>() + "\n";
            frame = frame->f_back;
        }
    }
#endif

    return errorString;
}

PYBIND11_NOINLINE handle get_object_handle(const void *ptr, const detail::type_info *type ) {
    auto &instances = get_internals().registered_instances;
    auto range = instances.equal_range(ptr);
    for (auto it = range.first; it != range.second; ++it) {
        for (auto vh : values_and_holders(it->second)) {
            if (vh.type == type)
                return handle((PyObject *) it->second);
        }
    }
    return handle();
}

PyThreadState *get_thread_state_unchecked() {
#if defined(PYPY_VERSION)
    return PyThreadState_GET();
#elif PY_VERSION_HEX < 0x03000000
    return _PyThreadState_Current;
#elif PY_VERSION_HEX < 0x03050000
    return (PyThreadState*) _Py_atomic_load_relaxed(&_PyThreadState_Current);
#elif PY_VERSION_HEX < 0x03050200
    return (PyThreadState*) _PyThreadState_Current.value;
#else
    return _PyThreadState_UncheckedGet();
#endif
}

// Forward declarations
void keep_alive_impl(handle nurse, handle patient);
PyObject *make_new_instance(PyTypeObject *type);

    PYBIND11_NOINLINE type_caster_generic::type_caster_generic(const std::type_info &type_info)
        : typeinfo(get_type_info(type_info)), cpptype(&type_info) { }

    type_caster_generic::type_caster_generic(const type_info *typeinfo)
        : typeinfo(typeinfo), cpptype(typeinfo ? typeinfo->cpptype : nullptr) { }

    bool type_caster_generic::load(handle src, bool convert) {
        return load_impl<type_caster_generic>(src, convert);
    }

    PYBIND11_NOINLINE handle type_caster_generic::cast(const void *_src, return_value_policy policy, handle parent,
                                         const detail::type_info *tinfo,
                                         void *(*copy_constructor)(const void *),
                                         void *(*move_constructor)(const void *),
                                         const void *existing_holder) {
        if (!tinfo) // no type info: error will be set already
            return handle();

        void *src = const_cast<void *>(_src);
        if (src == nullptr)
            return none().release();

        auto it_instances = get_internals().registered_instances.equal_range(src);
        for (auto it_i = it_instances.first; it_i != it_instances.second; ++it_i) {
            for (auto instance_type : detail::all_type_info(Py_TYPE(it_i->second))) {
                if (instance_type && same_type(*instance_type->cpptype, *tinfo->cpptype))
                    return handle((PyObject *) it_i->second).inc_ref();
            }
        }

        auto inst = reinterpret_steal<object>(make_new_instance(tinfo->type));
        auto wrapper = reinterpret_cast<instance *>(inst.ptr());
        wrapper->owned = false;
        void *&valueptr = values_and_holders(wrapper).begin()->value_ptr();

        switch (policy) {
            case return_value_policy::automatic:
            case return_value_policy::take_ownership:
                valueptr = src;
                wrapper->owned = true;
                break;

            case return_value_policy::automatic_reference:
            case return_value_policy::reference:
                valueptr = src;
                wrapper->owned = false;
                break;

            case return_value_policy::copy:
                if (copy_constructor)
                    valueptr = copy_constructor(src);
                else
                    throw cast_error("return_value_policy = copy, but the "
                                     "object is non-copyable!");
                wrapper->owned = true;
                break;

            case return_value_policy::move:
                if (move_constructor)
                    valueptr = move_constructor(src);
                else if (copy_constructor)
                    valueptr = copy_constructor(src);
                else
                    throw cast_error("return_value_policy = move, but the "
                                     "object is neither movable nor copyable!");
                wrapper->owned = true;
                break;

            case return_value_policy::reference_internal:
                valueptr = src;
                wrapper->owned = false;
                keep_alive_impl(inst, parent);
                break;

            default:
                throw cast_error("unhandled return_value_policy: should not happen!");
        }

        tinfo->init_instance(wrapper, existing_holder);

        return inst.release();
    }

    // Base methods for generic caster; there are overridden in copyable_holder_caster
    void type_caster_generic::load_value(value_and_holder &&v_h) {
        auto *&vptr = v_h.value_ptr();
        // Lazy allocation for unallocated values:
        if (vptr == nullptr) {
            auto *type = v_h.type ? v_h.type : typeinfo;
            if (type->operator_new) {
                vptr = type->operator_new(type->type_size);
            } else {
                #if defined(PYBIND11_CPP17)
                    if (type->type_align > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
                        vptr = ::operator new(type->type_size,
                                              (std::align_val_t) type->type_align);
                    else
                #endif
                vptr = ::operator new(type->type_size);
            }
        }
        value = vptr;
    }
    bool type_caster_generic::try_implicit_casts(handle src, bool convert) {
        for (auto &cast : typeinfo->implicit_casts) {
            type_caster_generic sub_caster(*cast.first);
            if (sub_caster.load(src, convert)) {
                value = cast.second(sub_caster.value);
                return true;
            }
        }
        return false;
    }
    bool type_caster_generic::try_direct_conversions(handle src) {
        for (auto &converter : *typeinfo->direct_conversions) {
            if (converter(src.ptr(), value))
                return true;
        }
        return false;
    }
    void type_caster_generic::check_holder_compat() {}

    PYBIND11_NOINLINE void *type_caster_generic::local_load(PyObject *src, const type_info *ti) {
        auto caster = type_caster_generic(ti);
        if (caster.load(src, false))
            return caster.value;
        return nullptr;
    }

    /// Try to load with foreign typeinfo, if available. Used when there is no
    /// native typeinfo, or when the native one wasn't able to produce a value.
    PYBIND11_NOINLINE bool type_caster_generic::try_load_foreign_module_local(handle src) {
        constexpr auto *local_key = PYBIND11_MODULE_LOCAL_ID;
        const auto pytype = src.get_type();
        if (!hasattr(pytype, local_key))
            return false;

        type_info *foreign_typeinfo = reinterpret_borrow<capsule>(getattr(pytype, local_key));
        // Only consider this foreign loader if actually foreign and is a loader of the correct cpp type
        if (foreign_typeinfo->module_local_load == &local_load
            || (cpptype && !same_type(*cpptype, *foreign_typeinfo->cpptype)))
            return false;

        if (auto result = foreign_typeinfo->module_local_load(src.ptr(), foreign_typeinfo)) {
            value = result;
            return true;
        }
        return false;
    }


    // Called to do type lookup and wrap the pointer and type in a pair when a dynamic_cast
    // isn't needed or can't be used.  If the type is unknown, sets the error and returns a pair
    // with .second = nullptr.  (p.first = nullptr is not an error: it becomes None).
    PYBIND11_NOINLINE std::pair<const void *, const type_info *> type_caster_generic::src_and_type(
            const void *src, const std::type_info &cast_type, const std::type_info *rtti_type = nullptr) {
        if (auto *tpi = get_type_info(cast_type))
            return {src, const_cast<const type_info *>(tpi)};

        // Not found, set error:
        std::string tname = rtti_type ? rtti_type->name() : cast_type.name();
        detail::clean_type_id(tname);
        std::string msg = "Unregistered type : " + tname;
        PyErr_SetString(PyExc_TypeError, msg.c_str());
        return {nullptr, nullptr};
    }

NAMESPACE_END(detail)

NAMESPACE_END(PYBIND11_NAMESPACE)
