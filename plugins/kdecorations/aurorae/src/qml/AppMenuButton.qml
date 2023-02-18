/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
import QtQuick 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kwin.decoration 0.1

DecorationButton {
    id: appMenuButton
    buttonType: DecorationOptions.DecorationButtonApplicationMenu
    visible: decoration.client.hasApplicationMenu
    PlasmaCore.IconItem {
        usesPlasmaTheme: false
        source: decoration.client.icon
        anchors.fill: parent
    }

}
