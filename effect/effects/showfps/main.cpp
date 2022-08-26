/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "showfps.h"

#include <kwineffects/effect_plugin_factory.h>

namespace KWin
{

KWIN_EFFECT_FACTORY(ShowFpsEffect, "metadata.json.stripped")

} // namespace KWin

#include "main.moc"