/*
    pybind11/detail/internals.h: Internal data structure and related functions

    Copyright (c) 2017 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "internals.h"
#include "../pytypes.h"
#include "../cast.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)
NAMESPACE_BEGIN(detail)

#if defined(__GLIBCXX__)
bool same_type(const std::type_info &lhs, const std::type_info &rhs) { return lhs == rhs; }
#else
bool same_type(const std::type_info &lhs, const std::type_info &rhs) {
    return lhs.name() == rhs.name() || std::strcmp(lhs.name(), rhs.name()) == 0;
}

size_t type_hash::operator()(const std::type_index &t) const {
    size_t hash = 5381;
    const char *ptr = t.name();
    while (auto c = static_cast<unsigned char>(*ptr++))
        hash = (hash * 33) ^ c;
    return hash;
}

bool type_equal_to:;operator()(const std::type_index &lhs, const std::type_index &rhs) const {
    return lhs.name() == rhs.name() || std::strcmp(lhs.name(), rhs.name()) == 0;
}

#endif

size_t overload_hash::operator()(const std::pair<const PyObject *, const char *>& v) const {
    size_t value = std::hash<const void *>()(v.first);
    value ^= std::hash<const void *>()(v.second)  + 0x9e3779b9 + (value<<6) + (value>>2);
    return value;
}

/// Each module locally stores a pointer to the `internals` data. The data
/// itself is shared among modules with the same `PYBIND11_INTERNALS_ID`.
internals **&get_internals_pp() {
    static internals **internals_pp = nullptr;
    return internals_pp;
}

void translate_exception(std::exception_ptr p) {
    try {
        if (p) std::rethrow_exception(p);
    } catch (error_already_set &e)           { e.restore();                                    return;
    } catch (const builtin_exception &e)     { e.set_error();                                  return;
    } catch (const std::bad_alloc &e)        { PyErr_SetString(PyExc_MemoryError,   e.what()); return;
    } catch (const std::domain_error &e)     { PyErr_SetString(PyExc_ValueError,    e.what()); return;
    } catch (const std::invalid_argument &e) { PyErr_SetString(PyExc_ValueError,    e.what()); return;
    } catch (const std::length_error &e)     { PyErr_SetString(PyExc_ValueError,    e.what()); return;
    } catch (const std::out_of_range &e)     { PyErr_SetString(PyExc_IndexError,    e.what()); return;
    } catch (const std::range_error &e)      { PyErr_SetString(PyExc_ValueError,    e.what()); return;
    } catch (const std::exception &e)        { PyErr_SetString(PyExc_RuntimeError,  e.what()); return;
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Caught an unknown exception!");
        return;
    }
}

/// Return a reference to the current `internals` data
PYBIND11_NOINLINE internals &get_internals() {
    auto **&internals_pp = get_internals_pp();
    if (internals_pp && *internals_pp)
        return **internals_pp;

    // Ensure that the GIL is held since we will need to make Python calls.
    // Cannot use py::gil_scoped_acquire here since that constructor calls get_internals.
    struct gil_scoped_acquire_local {
        gil_scoped_acquire_local() : state (PyGILState_Ensure()) {}
        ~gil_scoped_acquire_local() { PyGILState_Release(state); }
        const PyGILState_STATE state;
    } gil;

    constexpr auto *id = PYBIND11_INTERNALS_ID;
    auto builtins = handle(PyEval_GetBuiltins());
    if (builtins.contains(id) && isinstance<capsule>(builtins[id])) {
        internals_pp = static_cast<internals **>(capsule(builtins[id]));

        // We loaded builtins through python's builtins, which means that our `error_already_set`
        // and `builtin_exception` may be different local classes than the ones set up in the
        // initial exception translator, below, so add another for our local exception classes.
        //
        // libstdc++ doesn't require this (types there are identified only by name)
#if !defined(__GLIBCXX__)
        (*internals_pp)->registered_exception_translators.push_front(&translate_local_exception);
#endif
    } else {
        if (!internals_pp) internals_pp = new internals*();
        auto *&internals_ptr = *internals_pp;
        internals_ptr = new internals();
#if defined(WITH_THREAD)
        PyEval_InitThreads();
        PyThreadState *tstate = PyThreadState_Get();
        #if PY_VERSION_HEX >= 0x03070000
            internals_ptr->tstate = PyThread_tss_alloc();
            if (!internals_ptr->tstate || PyThread_tss_create(internals_ptr->tstate))
                pybind11_fail("get_internals: could not successfully initialize the TSS key!");
            PyThread_tss_set(internals_ptr->tstate, tstate);
        #else
            internals_ptr->tstate = PyThread_create_key();
            if (internals_ptr->tstate == -1)
                pybind11_fail("get_internals: could not successfully initialize the TLS key!");
            PyThread_set_key_value(internals_ptr->tstate, tstate);
        #endif
        internals_ptr->istate = tstate->interp;
#endif
        builtins[id] = capsule(internals_pp);
        internals_ptr->registered_exception_translators.push_front(&translate_exception);
        internals_ptr->static_property_type = make_static_property_type();
        internals_ptr->default_metaclass = make_default_metaclass();
        internals_ptr->instance_base = make_object_base_type(internals_ptr->default_metaclass);
    }
    return **internals_pp;
}

/// Works like `internals.registered_types_cpp`, but for module-local registered types:
type_map<type_info *> &registered_local_types_cpp() {
    static type_map<type_info *> locals{};
    return locals;
}

NAMESPACE_END(detail)

/// Returns a named pointer that is shared among all extension modules (using the same
/// pybind11 version) running in the current interpreter. Names starting with underscores
/// are reserved for internal usage. Returns `nullptr` if no matching entry was found.
PYBIND11_NOINLINE void *get_shared_data(const std::string &name) {
    auto &internals = detail::get_internals();
    auto it = internals.shared_data.find(name);
    return it != internals.shared_data.end() ? it->second : nullptr;
}

/// Set the shared data that can be later recovered by `get_shared_data()`.
PYBIND11_NOINLINE void *set_shared_data(const std::string &name, void *data) {
    detail::get_internals().shared_data[name] = data;
    return data;
}

NAMESPACE_END(PYBIND11_NAMESPACE)
