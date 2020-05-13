/*
    pybind11/embed.h: Support for embedding the interpreter

    Copyright (c) 2017 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "pybind11.h"
#include "eval.h"

#include "embed.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)

/** \rst
    Initialize the Python interpreter. No other pybind11 or CPython API functions can be
    called before this is done; with the exception of `PYBIND11_EMBEDDED_MODULE`. The
    optional parameter can be used to skip the registration of signal handlers (see the
    `Python documentation`_ for details). Calling this function again after the interpreter
    has already been initialized is a fatal error.

    If initializing the Python interpreter fails, then the program is terminated.  (This
    is controlled by the CPython runtime and is an exception to pybind11's normal behavior
    of throwing exceptions on errors.)

    .. _Python documentation: https://docs.python.org/3/c-api/init.html#c.Py_InitializeEx
 \endrst */
void initialize_interpreter(bool init_signal_handlers) {
    if (Py_IsInitialized())
        pybind11_fail("The interpreter is already running");

    Py_InitializeEx(init_signal_handlers ? 1 : 0);

    // Make .py files in the working directory available by default
    module::import("sys").attr("path").cast<list>().append(".");
}

/** \rst
    Shut down the Python interpreter. No pybind11 or CPython API functions can be called
    after this. In addition, pybind11 objects must not outlive the interpreter:

    .. code-block:: cpp

        { // BAD
            py::initialize_interpreter();
            auto hello = py::str("Hello, World!");
            py::finalize_interpreter();
        } // <-- BOOM, hello's destructor is called after interpreter shutdown

        { // GOOD
            py::initialize_interpreter();
            { // scoped
                auto hello = py::str("Hello, World!");
            } // <-- OK, hello is cleaned up properly
            py::finalize_interpreter();
        }

        { // BETTER
            py::scoped_interpreter guard{};
            auto hello = py::str("Hello, World!");
        }

    .. warning::

        The interpreter can be restarted by calling `initialize_interpreter` again.
        Modules created using pybind11 can be safely re-initialized. However, Python
        itself cannot completely unload binary extension modules and there are several
        caveats with regard to interpreter restarting. All the details can be found
        in the CPython documentation. In short, not all interpreter memory may be
        freed, either due to reference cycles or user-created global data.

 \endrst */
void finalize_interpreter() {
    handle builtins(PyEval_GetBuiltins());
    const char *id = PYBIND11_INTERNALS_ID;

    // Get the internals pointer (without creating it if it doesn't exist).  It's possible for the
    // internals to be created during Py_Finalize() (e.g. if a py::capsule calls `get_internals()`
    // during destruction), so we get the pointer-pointer here and check it after Py_Finalize().
    detail::internals **internals_ptr_ptr = detail::get_internals_pp();
    // It could also be stashed in builtins, so look there too:
    if (builtins.contains(id) && isinstance<capsule>(builtins[id]))
        internals_ptr_ptr = capsule(builtins[id]);

    Py_Finalize();

    if (internals_ptr_ptr) {
        delete *internals_ptr_ptr;
        *internals_ptr_ptr = nullptr;
    }
}

/** \rst
    Scope guard version of `initialize_interpreter` and `finalize_interpreter`.
    This a move-only guard and only a single instance can exist.

    .. code-block:: cpp

        #include <pybind11/embed.h>

        int main() {
            py::scoped_interpreter guard{};
            py::print(Hello, World!);
        } // <-- interpreter shutdown
 \endrst */
    scoped_interpreter::scoped_interpreter(bool init_signal_handlers) {
        initialize_interpreter(init_signal_handlers);
    }

    scoped_interpreter::scoped_interpreter(scoped_interpreter &&other) noexcept { other.is_valid = false; }

    scoped_interpreter::~scoped_interpreter() {
        if (is_valid)
            finalize_interpreter();
    }

NAMESPACE_END(PYBIND11_NAMESPACE)
