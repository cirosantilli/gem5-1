/*
    pybind11/options.h: global settings that are configurable at runtime.

    Copyright (c) 2016 Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "detail/common.h"

#include "options.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)


    // Default RAII constructor, which leaves settings as they currently are.
    options::options() : previous_state(global_state()) {}

    // Destructor, which restores settings that were in effect before.
    options::~options() {
        global_state() = previous_state;
    }

    // Setter methods (affect the global state):

    options& options::disable_user_defined_docstrings() & { global_state().show_user_defined_docstrings = false; return *this; }

    options& options::enable_user_defined_docstrings() & { global_state().show_user_defined_docstrings = true; return *this; }

    options& options::disable_function_signatures() & { global_state().show_function_signatures = false; return *this; }

    options& options::enable_function_signatures() & { global_state().show_function_signatures = true; return *this; }

    // Getter methods (return the global state):

    bool options::show_user_defined_docstrings() { return global_state().show_user_defined_docstrings; }

    bool options::show_function_signatures() { return global_state().show_function_signatures; }

    options::state &options::global_state() {
        static state instance;
        return instance;
    }

NAMESPACE_END(PYBIND11_NAMESPACE)
