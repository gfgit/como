/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::render
{

render::platform* singleton_interface::platform{nullptr};

}