/*
    pybind11/exec.h: Support for evaluating Python expressions and statements
    from strings and files

    Copyright (c) 2016 Klemens Morgenstern <klemens.morgenstern@ed-chemnitz.de> and
                       Wenzel Jakob <wenzel.jakob@epfl.ch>

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE file.
*/

#include "pybind11.h"

#include "eval.h"

NAMESPACE_BEGIN(PYBIND11_NAMESPACE)

void exec(str expr, object global, object local) {
    eval<eval_statements>(expr, global, local);
}

NAMESPACE_END(PYBIND11_NAMESPACE)
