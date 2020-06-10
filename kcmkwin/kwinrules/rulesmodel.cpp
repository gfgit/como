/*
 * Copyright (c) 2004 Lubos Lunak <l.lunak@kde.org>
 * Copyright (c) 2020 Ismael Asensio <isma.af@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "rulesmodel.h"
#include <rules.h>

#include <QFileInfo>
#include <QIcon>
#include <QQmlEngine>
#include <QTemporaryFile>
#include <QtDBus>

#include <KColorSchemeManager>
#include <KConfig>
#include <KLocalizedString>
#include <KWindowSystem>


namespace KWin
{

RulesModel::RulesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    qmlRegisterUncreatableType<RuleItem>("org.kde.kcms.kwinrules", 1, 0, "RuleItem",
                                         QStringLiteral("Do not create objects of type RuleItem"));
    qmlRegisterUncreatableType<RulesModel>("org.kde.kcms.kwinrules", 1, 0, "RulesModel",
                                                 QStringLiteral("Do not create objects of type RulesModel"));

    qDBusRegisterMetaType<KWin::DBusDesktopDataStruct>();
    qDBusRegisterMetaType<KWin::DBusDesktopDataVector>();

    populateRuleList();
}

RulesModel::~RulesModel()
{
}

QHash< int, QByteArray > RulesModel::roleNames() const
{
    return {
        {KeyRole,            QByteArrayLiteral("key")},
        {NameRole,           QByteArrayLiteral("name")},
        {IconRole,           QByteArrayLiteral("icon")},
        {IconNameRole,       QByteArrayLiteral("iconName")},
        {SectionRole,        QByteArrayLiteral("section")},
        {DescriptionRole,    QByteArrayLiteral("description")},
        {EnabledRole,        QByteArrayLiteral("enabled")},
        {SelectableRole,     QByteArrayLiteral("selectable")},
        {ValueRole,          QByteArrayLiteral("value")},
        {TypeRole,           QByteArrayLiteral("type")},
        {PolicyRole,         QByteArrayLiteral("policy")},
        {PolicyModelRole,    QByteArrayLiteral("policyModel")},
        {OptionsModelRole,   QByteArrayLiteral("options")},
        {SuggestedValueRole, QByteArrayLiteral("suggested")},
    };
}

int RulesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_ruleList.size();
}

QVariant RulesModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return QVariant();
    }

    const RuleItem *rule = m_ruleList.at(index.row());

    switch (role) {
    case KeyRole:
        return rule->key();
    case NameRole:
        return rule->name();
    case IconRole:
        return rule->icon();
    case IconNameRole:
        return rule->iconName();
    case DescriptionRole:
        return rule->description();
    case SectionRole:
        return rule->section();
    case EnabledRole:
        return rule->isEnabled();
    case SelectableRole:
        return !rule->hasFlag(RuleItem::AlwaysEnabled);
    case ValueRole:
        return rule->value();
    case TypeRole:
        return rule->type();
    case PolicyRole:
        return rule->policy();
    case PolicyModelRole:
        return rule->policyModel();
    case OptionsModelRole:
        return rule->options();
    case SuggestedValueRole:
        return rule->suggestedValue();
    }
    return QVariant();
}

bool RulesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return false;
    }

    RuleItem *rule = m_ruleList.at(index.row());

    switch (role) {
    case EnabledRole:
        if (value.toBool() == rule->isEnabled()) {
            return true;
        }
        rule->setEnabled(value.toBool());
        break;
    case ValueRole:
        if (value == rule->value()) {
            return true;
        }
        rule->setValue(value);
        break;
    case PolicyRole:
        if (value.toInt() == rule->policy()) {
            return true;
        }
        rule->setPolicy(value.toInt());
        break;
    case SuggestedValueRole:
        if (value == rule->suggestedValue()) {
            return true;
        }
        rule->setSuggestedValue(value);
        break;
    default:
        return false;
    }

    emit dataChanged(index, index, QVector<int>{role});

    if (rule->hasFlag(RuleItem::AffectsDescription)) {
        emit descriptionChanged();
    }
    if (rule->hasFlag(RuleItem::AffectsWarning)) {
        emit warningMessageChanged();
    }

    return true;
}

RuleItem *RulesModel::addRule(RuleItem *rule)
{
    m_ruleList << rule;
    m_rules.insert(rule->key(), rule);

    return rule;
}

bool RulesModel::hasRule(const QString& key) const
{
    return m_rules.contains(key);
}


RuleItem *RulesModel::ruleItem(const QString& key) const
{
    return m_rules.value(key);
}

QString RulesModel::description() const
{
    const QString desc = m_rules["description"]->value().toString();
    if (!desc.isEmpty()) {
        return desc;
    }
    return defaultDescription();
}

void RulesModel::setDescription(const QString &description)
{
    setData(index(0, 0), description, RulesModel::ValueRole);
}

QString RulesModel::defaultDescription() const
{
    const QString wmclass = m_rules["wmclass"]->value().toString();
    const QString title = m_rules["title"]->isEnabled() ? m_rules["title"]->value().toString() : QString();

    if (!title.isEmpty()) {
        return i18n("Window settings for %1", title);
    }
    if (!wmclass.isEmpty()) {
        return i18n("Settings for %1", wmclass);
    }

    return i18n("New window settings");
}

QString RulesModel::warningMessage() const
{
    if (wmclassWarning()) {
        return i18n("You have specified the window class as unimportant.\n"
                    "This means the settings will possibly apply to windows from all applications."
                    " If you really want to create a generic setting, it is recommended"
                    " you at least limit the window types to avoid special window types.");
    }

    return QString();
}


bool RulesModel::wmclassWarning() const
{
    const bool no_wmclass = !m_rules["wmclass"]->isEnabled()
                                || m_rules["wmclass"]->policy() == Rules::UnimportantMatch;
    const bool alltypes = !m_rules["types"]->isEnabled()
                              || (m_rules["types"]->value() == 0)
                              || (m_rules["types"]->value() == NET::AllTypesMask)
                              || ((m_rules["types"]->value().toInt() | (1 << NET::Override)) == 0x3FF);

    return (no_wmclass && alltypes);
}


void RulesModel::readFromSettings(RuleSettings *settings)
{
    beginResetModel();

    for (RuleItem *rule : qAsConst(m_ruleList)) {
        const KConfigSkeletonItem *configItem = settings->findItem(rule->key());
        const KConfigSkeletonItem *configPolicyItem = settings->findItem(rule->policyKey());

        rule->reset();

        if (!configItem) {
            continue;
        }

        const bool isEnabled = configPolicyItem ? configPolicyItem->property() != Rules::Unused
                                                : !configItem->property().toString().isEmpty();
        rule->setEnabled(isEnabled);

        const QVariant value = configItem->property();
        rule->setValue(value);

        if (configPolicyItem) {
            const int policy = configPolicyItem->property().toInt();
            rule->setPolicy(policy);
        }
    }

    endResetModel();

    emit descriptionChanged();
    emit warningMessageChanged();
}

void RulesModel::writeToSettings(RuleSettings *settings) const
{
    const QString description = m_rules["description"]->value().toString();
    if (description.isEmpty()) {
        m_rules["description"]->setValue(defaultDescription());
    }

    for (const RuleItem *rule : qAsConst(m_ruleList)) {
        KConfigSkeletonItem *configItem = settings->findItem(rule->key());
        KConfigSkeletonItem *configPolicyItem = settings->findItem(rule->policyKey());

        Q_ASSERT (configItem);

        if (rule->isEnabled()) {
            configItem->setProperty(rule->value());
            if (configPolicyItem) {
                configPolicyItem->setProperty(rule->policy());
            }
        } else {
            if (configPolicyItem) {
                configPolicyItem->setProperty(Rules::Unused);
            } else {
                // Rules without policy gets deactivated by an empty string
                configItem->setProperty(QString());
            }
        }
    }
}

void RulesModel::importFromRules(Rules* rules)
{
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        return;
    }
    const auto cfg = KSharedConfig::openConfig(tempFile.fileName(), KConfig::SimpleConfig);
    RuleSettings *settings = new RuleSettings(cfg, QStringLiteral("tempSettings"));

    settings->setDefaults();
    if (rules) {
        rules->write(settings);
    }
    readFromSettings(settings);

    delete(settings);
}

Rules *RulesModel::exportToRules() const
{
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        return nullptr;
    }
    const auto cfg = KSharedConfig::openConfig(tempFile.fileName(), KConfig::SimpleConfig);

    RuleSettings *settings = new RuleSettings(cfg, QStringLiteral("tempSettings"));

    writeToSettings(settings);
    Rules *rules = new Rules(settings);

    delete(settings);
    return rules;
}


void RulesModel::populateRuleList()
{
    qDeleteAll(m_ruleList);
    m_ruleList.clear();

    //Rule description
    auto description = addRule(new RuleItem(QLatin1String("description"),
                                            RulePolicy::NoPolicy, RuleItem::String,
                                            i18n("Description"), i18n("Window matching"),
                                            QIcon::fromTheme("entry-edit")));
    description->setFlag(RuleItem::AlwaysEnabled);
    description->setFlag(RuleItem::AffectsDescription);

    // Window matching
    auto wmclass = addRule(new RuleItem(QLatin1String("wmclass"),
                                        RulePolicy::StringMatch, RuleItem::String,
                                        i18n("Window class (application)"), i18n("Window matching"),
                                        QIcon::fromTheme("window")));
    wmclass->setFlag(RuleItem::AlwaysEnabled);
    wmclass->setFlag(RuleItem::AffectsDescription);
    wmclass->setFlag(RuleItem::AffectsWarning);

    auto wmclasscomplete = addRule(new RuleItem(QLatin1String("wmclasscomplete"),
                                                RulePolicy::NoPolicy, RuleItem::Boolean,
                                                i18n("Match whole window class"), i18n("Window matching"),
                                                QIcon::fromTheme("window")));
    wmclasscomplete->setFlag(RuleItem::AlwaysEnabled);

    auto types = addRule(new RuleItem(QLatin1String("types"),
                                      RulePolicy::NoPolicy, RuleItem::FlagsOption,
                                      i18n("Window types"), i18n("Window matching"),
                                      QIcon::fromTheme("window-duplicate")));
    types->setOptionsData(windowTypesModelData());
    types->setFlag(RuleItem::AlwaysEnabled);
    types->setFlag(RuleItem::AffectsWarning);

    addRule(new RuleItem(QLatin1String("windowrole"),
                         RulePolicy::NoPolicy, RuleItem::String,
                         i18n("Window role"), i18n("Window matching"),
                         QIcon::fromTheme("dialog-object-properties")));

    auto title = addRule(new RuleItem(QLatin1String("title"),
                                      RulePolicy::StringMatch, RuleItem::String,
                                      i18n("Window title"), i18n("Window matching"),
                                      QIcon::fromTheme("edit-comment")));
    title->setFlag(RuleItem::AffectsDescription);

    addRule(new RuleItem(QLatin1String("clientmachine"),
                         RulePolicy::StringMatch, RuleItem::String,
                         i18n("Machine (hostname)"), i18n("Window matching"),
                         QIcon::fromTheme("computer")));

    // Size & Position
    addRule(new RuleItem(QLatin1String("position"),
                         RulePolicy::SetRule, RuleItem::Point,
                         i18n("Position"), i18n("Size & Position"),
                         QIcon::fromTheme("transform-move")));

    addRule(new RuleItem(QLatin1String("size"),
                         RulePolicy::SetRule, RuleItem::Size,
                         i18n("Size"), i18n("Size & Position"),
                         QIcon::fromTheme("image-resize-symbolic")));

    addRule(new RuleItem(QLatin1String("maximizehoriz"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Maximized horizontally"), i18n("Size & Position"),
                         QIcon::fromTheme("resizecol")));

    addRule(new RuleItem(QLatin1String("maximizevert"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Maximized vertically"), i18n("Size & Position"),
                         QIcon::fromTheme("resizerow")));

    auto desktop = addRule(new RuleItem(QLatin1String("desktop"),
                                        RulePolicy::SetRule, RuleItem::Option,
                                        i18n("Virtual Desktop"), i18n("Size & Position"),
                                        QIcon::fromTheme("virtual-desktops")));
    desktop->setOptionsData(virtualDesktopsModelData());

    connect(this, &RulesModel::virtualDesktopsUpdated,
            this, [this] { m_rules["desktop"]->setOptionsData(virtualDesktopsModelData()); });
    updateVirtualDesktops();

#ifdef KWIN_BUILD_ACTIVITIES
    m_activities = new KActivities::Consumer(this);

    auto activity = addRule(new RuleItem(QLatin1String("activity"),
                                         RulePolicy::SetRule, RuleItem::Option,
                                         i18n("Activity"), i18n("Size & Position"),
                                         QIcon::fromTheme("activities")));
    activity->setOptionsData(activitiesModelData());

    // Activites consumer may update the available activities later
    connect(m_activities, &KActivities::Consumer::activitiesChanged,
            this, [this] { m_rules["activity"]->setOptionsData(activitiesModelData()); });
    connect(m_activities, &KActivities::Consumer::serviceStatusChanged,
            this, [this] { m_rules["activity"]->setOptionsData(activitiesModelData()); });

#endif

    addRule(new RuleItem(QLatin1String("screen"),
                         RulePolicy::SetRule, RuleItem::Integer,
                         i18n("Screen"), i18n("Size & Position"),
                         QIcon::fromTheme("osd-shutd-screen")));

    addRule(new RuleItem(QLatin1String("fullscreen"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Fullscreen"), i18n("Size & Position"),
                         QIcon::fromTheme("view-fullscreen")));

    addRule(new RuleItem(QLatin1String("minimize"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Minimized"), i18n("Size & Position"),
                         QIcon::fromTheme("window-minimize")));

    addRule(new RuleItem(QLatin1String("shade"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Shaded"), i18n("Size & Position"),
                         QIcon::fromTheme("window-shade")));

    auto placement = addRule(new RuleItem(QLatin1String("placement"),
                                          RulePolicy::ForceRule, RuleItem::Option,
                                          i18n("Initial placement"), i18n("Size & Position"),
                                          QIcon::fromTheme("region")));
    placement->setOptionsData(placementModelData());

    addRule(new RuleItem(QLatin1String("ignoregeometry"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Ignore requested geometry"), i18n("Size & Position"),
                         QIcon::fromTheme("view-time-schedule-baselined-remove"),
                         i18n("Windows can ask to appear in a certain position.\n"
                              "By default this overrides the placement strategy\n"
                              "what might be nasty if the client abuses the feature\n"
                              "to unconditionally popup in the middle of your screen.")));

    addRule(new RuleItem(QLatin1String("minsize"),
                         RulePolicy::ForceRule, RuleItem::Size,
                         i18n("Minimum Size"), i18n("Size & Position"),
                         QIcon::fromTheme("image-resize-symbolic")));

    addRule(new RuleItem(QLatin1String("maxsize"),
                         RulePolicy::ForceRule, RuleItem::Size,
                         i18n("Maximum Size"), i18n("Size & Position"),
                         QIcon::fromTheme("image-resize-symbolic")));

    addRule(new RuleItem(QLatin1String("strictgeometry"),
                         RulePolicy::ForceRule, RuleItem::Boolean,
                         i18n("Obey geometry restrictions"), i18n("Size & Position"),
                         QIcon::fromTheme("transform-crop-and-resize"),
                         i18n("Eg. terminals or video players can ask to keep a certain aspect ratio\n"
                              "or only grow by values larger than one\n"
                              "(eg. by the dimensions of one character).\n"
                              "This may be pointless and the restriction prevents arbitrary dimensions\n"
                              "like your complete screen area.")));

    // Arrangement & Access
    addRule(new RuleItem(QLatin1String("above"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Keep above"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("window-keep-above")));

    addRule(new RuleItem(QLatin1String("below"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Keep below"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("window-keep-below")));

    addRule(new RuleItem(QLatin1String("skiptaskbar"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Skip taskbar"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("kt-show-statusbar"),
                         i18n("Window shall (not) appear in the taskbar.")));

    addRule(new RuleItem(QLatin1String("skippager"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Skip pager"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("org.kde.plasma.pager"),
                         i18n("Window shall (not) appear in the manager for virtual desktops")));

    addRule(new RuleItem(QLatin1String("skipswitcher"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("Skip switcher"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("preferences-system-windows-effect-flipswitch"),
                         i18n("Window shall (not) appear in the Alt+Tab list")));

    addRule(new RuleItem(QLatin1String("shortcut"),
                         RulePolicy::SetRule, RuleItem::Shortcut,
                         i18n("Shortcut"), i18n("Arrangement & Access"),
                         QIcon::fromTheme("configure-shortcuts")));

    // Appearance & Fixes
    addRule(new RuleItem(QLatin1String("noborder"),
                         RulePolicy::SetRule, RuleItem::Boolean,
                         i18n("No titlebar and frame"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("dialog-cancel")));

    auto decocolor = addRule(new RuleItem(QLatin1String("decocolor"),
                                          RulePolicy::ForceRule, RuleItem::Option,
                                          i18n("Titlebar color scheme"), i18n("Appearance & Fixes"),
                                          QIcon::fromTheme("preferences-desktop-theme")));
    decocolor->setOptionsData(colorSchemesModelData());

    addRule(new RuleItem(QLatin1String("opacityactive"),
                         RulePolicy::ForceRule, RuleItem::Percentage,
                         i18n("Active opacity"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("edit-opacity")));

    addRule(new RuleItem(QLatin1String("opacityinactive"),
                         RulePolicy::ForceRule, RuleItem::Percentage,
                         i18n("Inactive opacity"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("edit-opacity")));

    auto fsplevel = addRule(new RuleItem(QLatin1String("fsplevel"),
                                         RulePolicy::ForceRule, RuleItem::Option,
                                         i18n("Focus stealing prevention"), i18n("Appearance & Fixes"),
                                         QIcon::fromTheme("preferences-system-windows-effect-glide"),
                                         i18n("KWin tries to prevent windows from taking the focus\n"
                                              "(\"activate\") while you're working in another window,\n"
                                              "but this may sometimes fail or superact.\n"
                                              "\"None\" will unconditionally allow this window to get the focus while\n"
                                              "\"Extreme\" will completely prevent it from taking the focus.")));
    fsplevel->setOptionsData(focusModelData());

    auto fpplevel = addRule(new RuleItem(QLatin1String("fpplevel"),
                                         RulePolicy::ForceRule, RuleItem::Option,
                                         i18n("Focus protection"), i18n("Appearance & Fixes"),
                                         QIcon::fromTheme("preferences-system-windows-effect-minimize"),
                                         i18n("This controls the focus protection of the currently active window.\n"
                                              "None will always give the focus away,\n"
                                              "Extreme will keep it.\n"
                                              "Otherwise it's interleaved with the stealing prevention\n"
                                              "assigned to the window that wants the focus.")));
    fpplevel->setOptionsData(focusModelData());

    addRule(new RuleItem(QLatin1String("acceptfocus"),
                         RulePolicy::ForceRule, RuleItem::Boolean,
                         i18n("Accept focus"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("preferences-desktop-cursors"),
                         i18n("Windows may prevent to get the focus (activate) when being clicked.\n"
                              "On the other hand you might wish to prevent a window\n"
                              "from getting focused on a mouse click.")));

    addRule(new RuleItem(QLatin1String("disableglobalshortcuts"),
                         RulePolicy::ForceRule, RuleItem::Boolean,
                         i18n("Ignore global shortcuts"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("input-keyboard-virtual-off"),
                         i18n("When used, a window will receive\n"
                              "all keyboard inputs while it is active, including Alt+Tab etc.\n"
                              "This is especially interesting for emulators or virtual machines.\n"
                              "\n"
                              "Be warned:\n"
                              "you won't be able to Alt+Tab out of the window\n"
                              "nor use any other global shortcut (such as Alt+F2 to show KRunner)\n"
                              "while it's active!")));

    addRule(new RuleItem(QLatin1String("closeable"),
                         RulePolicy::ForceRule, RuleItem::Boolean,
                         i18n("Closeable"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("dialog-close")));

    auto type = addRule(new RuleItem(QLatin1String("type"),
                                     RulePolicy::ForceRule, RuleItem::Option,
                                     i18n("Set window type"), i18n("Appearance & Fixes"),
                                     QIcon::fromTheme("window-duplicate")));
    type->setOptionsData(windowTypesModelData());

    addRule(new RuleItem(QLatin1String("desktopfile"),
                         RulePolicy::SetRule, RuleItem::String,
                         i18n("Desktop file name"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("application-x-desktop")));

    addRule(new RuleItem(QLatin1String("blockcompositing"),
                         RulePolicy::ForceRule, RuleItem::Boolean,
                         i18n("Block compositing"), i18n("Appearance & Fixes"),
                         QIcon::fromTheme("composite-track-on")));
}


const QHash<QString, QString> RulesModel::x11PropertyHash()
{
    static const auto propertyToRule = QHash<QString, QString> {
        /* The original detection dialog allows to choose depending on "Match complete window class":
         *     if Match Complete == false: wmclass = "resourceClass"
         *     if Match Complete == true:  wmclass = "resourceName" + " " + "resourceClass"
         */
        { "resourceName",       "wmclass"       },
        { "caption",            "title"         },
        { "role",               "windowrole"    },
        { "clientMachine",      "clientmachine" },
        { "x11DesktopNumber",   "desktop"       },
        { "maximizeHorizontal", "maximizehoriz" },
        { "maximizeVertical",   "maximizevert"  },
        { "minimized",          "minimize"      },
        { "shaded",             "shade"         },
        { "fullscreen",         "fullscreen"    },
        { "keepAbove",          "above"         },
        { "keepBelow",          "below"         },
        { "noBorder",           "noborder"      },
        { "skipTaskbar",        "skiptaskbar"   },
        { "skipPager",          "skippager"     },
        { "skipSwitcher",       "skipswitcher"  },
        { "type",               "type"          },
        { "desktopFile",        "desktopfile"   }
    };
    return propertyToRule;
};

void RulesModel::setWindowProperties(const QVariantMap &info, bool forceValue)
{
    // Properties that cannot be directly applied via x11PropertyHash
    const QPoint position = QPoint(info.value("x").toInt(), info.value("y").toInt());
    const QSize size = QSize(info.value("width").toInt(), info.value("height").toInt());

    m_rules["position"]->setSuggestedValue(position, forceValue);
    m_rules["size"]->setSuggestedValue(size, forceValue);
    m_rules["minsize"]->setSuggestedValue(size, forceValue);
    m_rules["maxsize"]->setSuggestedValue(size, forceValue);

    NET::WindowType window_type = static_cast<NET::WindowType>(info.value("type", 0).toInt());
    if (window_type == NET::Unknown) {
        window_type = NET::Normal;
    }
    m_rules["types"]->setSuggestedValue(1 << window_type, forceValue);

    const auto ruleForProperty = x11PropertyHash();
    for (QString &property : info.keys()) {
        if (!ruleForProperty.contains(property)) {
            continue;
        }
        const QString ruleKey = ruleForProperty.value(property, QString());
        Q_ASSERT(hasRule(ruleKey));

        m_rules[ruleKey]->setSuggestedValue(info.value(property), forceValue);
    }

    emit dataChanged(index(0), index(rowCount()-1), {RulesModel::SuggestedValueRole});
    if (!forceValue) {
        emit suggestionsChanged();
    }
}


QList<OptionsModel::Data> RulesModel::windowTypesModelData() const
{
    static const auto modelData = QList<OptionsModel::Data> {
        //TODO: Find/create better icons
        { NET::Normal,  i18n("Normal Window")     , QIcon::fromTheme("window")                   },
        { NET::Dialog,  i18n("Dialog Window")     , QIcon::fromTheme("window-duplicate")         },
        { NET::Utility, i18n("Utility Window")    , QIcon::fromTheme("dialog-object-properties") },
        { NET::Dock,    i18n("Dock (panel)")      , QIcon::fromTheme("list-remove")              },
        { NET::Toolbar, i18n("Toolbar")           , QIcon::fromTheme("tools")                    },
        { NET::Menu,    i18n("Torn-Off Menu")     , QIcon::fromTheme("overflow-menu-left")       },
        { NET::Splash,  i18n("Splash Screen")     , QIcon::fromTheme("embosstool")               },
        { NET::Desktop, i18n("Desktop")           , QIcon::fromTheme("desktop")                  },
        // { NET::Override, i18n("Unmanaged Window")   },  deprecated
        { NET::TopMenu, i18n("Standalone Menubar"), QIcon::fromTheme("open-menu-symbolic")       }
    };
    return modelData;
}

QList<OptionsModel::Data> RulesModel::virtualDesktopsModelData() const
{
    QList<OptionsModel::Data> modelData;
    for (const DBusDesktopDataStruct &desktop : m_virtualDesktops) {
        modelData << OptionsModel::Data{
            desktop.position + 1,  // "desktop" setting uses the desktop position (int) starting at 1
            QString::number(desktop.position + 1).rightJustified(2) + QStringLiteral(": ") + desktop.name,
            QIcon::fromTheme("virtual-desktops")
        };
    }
    modelData << OptionsModel::Data{ NET::OnAllDesktops, i18n("All Desktops"), QIcon::fromTheme("window-pin") };
    return modelData;
}


QList<OptionsModel::Data> RulesModel::activitiesModelData() const
{
#ifdef KWIN_BUILD_ACTIVITIES
    QList<OptionsModel::Data> modelData;

    // NULL_ID from kactivities/src/lib/core/consumer.cpp
    modelData << OptionsModel::Data{
        QString::fromLatin1("00000000-0000-0000-0000-000000000000"),
        i18n("All Activities"),
        QIcon::fromTheme("activities")
    };

    const auto activities = m_activities->activities(KActivities::Info::Running);
    if (m_activities->serviceStatus() == KActivities::Consumer::Running) {
        for (const QString &activityId : activities) {
            const KActivities::Info info(activityId);
            modelData << OptionsModel::Data{ activityId, info.name(), QIcon::fromTheme(info.icon()) };
        }
    }

    return modelData;
#else
    return {};
#endif
}

QList<OptionsModel::Data> RulesModel::placementModelData() const
{
    static const auto modelData = QList<OptionsModel::Data> {
        { Placement::Default,      i18n("Default")             },
        { Placement::NoPlacement,  i18n("No Placement")        },
        { Placement::Smart,        i18n("Minimal Overlapping") },
        { Placement::Maximizing,   i18n("Maximized")           },
        { Placement::Cascade,      i18n("Cascaded")            },
        { Placement::Centered,     i18n("Centered")            },
        { Placement::Random,       i18n("Random")              },
        { Placement::ZeroCornered, i18n("In Top-Left Corner")  },
        { Placement::UnderMouse,   i18n("Under Mouse")         },
        { Placement::OnMainWindow, i18n("On Main Window")      }
    };
    return modelData;
}

QList<OptionsModel::Data> RulesModel::focusModelData() const
{
    static const auto modelData = QList<OptionsModel::Data> {
        { 0, i18n("None")    },
        { 1, i18n("Low")     },
        { 2, i18n("Normal")  },
        { 3, i18n("High")    },
        { 4, i18n("Extreme") }
    };
    return modelData;
}

QList<OptionsModel::Data> RulesModel::colorSchemesModelData() const
{
    QList<OptionsModel::Data> modelData;

    KColorSchemeManager schemes;
    QAbstractItemModel *schemesModel = schemes.model();

    // Skip row 0, which is Default scheme
    for (int r = 1; r < schemesModel->rowCount(); r++) {
        const QModelIndex index = schemesModel->index(r, 0);
        modelData << OptionsModel::Data{
            QFileInfo(index.data(Qt::UserRole).toString()).baseName(),
            index.data(Qt::DisplayRole).toString(),
            index.data(Qt::DecorationRole).value<QIcon>()
        };
    }

    return modelData;
}

void RulesModel::detectWindowProperties(int secs)
{
    QTimer::singleShot(secs*1000, this, &RulesModel::selectX11Window);
}

void RulesModel::selectX11Window()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/KWin"),
                                                          QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("queryWindowInfo"));

    QDBusPendingReply<QVariantMap> async = QDBusConnection::sessionBus().asyncCall(message);

    QDBusPendingCallWatcher *callWatcher = new QDBusPendingCallWatcher(async, this);
    connect(callWatcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *self) {
                QDBusPendingReply<QVariantMap> reply = *self;
                self->deleteLater();
                if (!reply.isValid()) {
                    return;
                }
                const QVariantMap windowInfo = reply.value();
                setWindowProperties(windowInfo);
            }
    );
}

void RulesModel::updateVirtualDesktops()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/VirtualDesktopManager"),
                                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
    message.setArguments(QVariantList{
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        QStringLiteral("desktops")
    });

    QDBusPendingReply<QVariant> async = QDBusConnection::sessionBus().asyncCall(message);

    QDBusPendingCallWatcher *callWatcher = new QDBusPendingCallWatcher(async, this);
    connect(callWatcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *self) {
                QDBusPendingReply<QVariant> reply = *self;
                self->deleteLater();
                if (!reply.isValid()) {
                    return;
                }
                m_virtualDesktops = qdbus_cast<KWin::DBusDesktopDataVector>(reply.value());
                emit virtualDesktopsUpdated();
            }
    );
}


} //namespace
