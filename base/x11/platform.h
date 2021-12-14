/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "base/platform.h"

#include <memory>
#include <vector>

namespace KWin::base::x11
{

class platform : public base::platform
{
public:
    std::vector<std::unique_ptr<output>> outputs;

    std::vector<base::output*> get_outputs() const override
    {
        std::vector<base::output*> vec;
        for (auto&& output : outputs) {
            vec.push_back(output.get());
        }
        return vec;
    }
};

}
