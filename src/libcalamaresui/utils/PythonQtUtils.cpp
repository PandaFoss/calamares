/* === This file is part of Calamares - <https://github.com/calamares> ===
 *
 *   Copyright 2016, Teo Mrnjavac <teo@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PythonQtUtils.h"

#include "utils/CalamaresUtils.h"
#include "utils/Logger.h"
#include "utils/Retranslator.h"

#include <QMutex>
#include <QThread>
#include <QWidget>

namespace CalamaresUtils
{

QVariant
PythonQtModule::lookupAndCall( PythonQtObjectPtr object,
               const QStringList& candidateNames,
               const QVariantList& args,
               const QVariantMap& kwargs )
{
    QMutexLocker l(&m_locker);

    Q_ASSERT( object );
    Q_ASSERT( !candidateNames.isEmpty() );

    for ( const QString& name : candidateNames )
    {
        PythonQtObjectPtr callable = PythonQt::self()->lookupCallable( object, name );
        if ( callable )
            return callable.call( args, kwargs );
    }

    // If we haven't found a callable with the given names, we force an error:
    return PythonQt::self()->call( object, candidateNames.first(), args, kwargs );
}

QVariant PythonQtModule::lookupAndCall(const QStringList& candidateNames, const QVariantList& args, const QVariantMap& kwargs)
{
    return lookupAndCall( m_module, candidateNames, args, kwargs );
}


PythonQtModule::PythonQtModule(const QString& name)
    : m_module( PythonQt::self()->createModuleFromScript( name ) )
    , m_moduleName( name )
{
}

PythonQtModule::~PythonQtModule()
{
}

void
PythonQtModule::load(const QString& modulePath)
{
    if ( isNull() )
        return;

    static const QLatin1Literal calamares_module_annotation(
        "_calamares_module_typename = ''\n"
        "def calamares_module(viewmodule_type):\n"
        "    global _calamares_module_typename\n"
        "    _calamares_module_typename = viewmodule_type.__name__\n"
        "    return viewmodule_type\n" );

    // Load in the decorator
    PythonQt::self()->evalScript( m_module, calamares_module_annotation );

    // Load the module
    PythonQt::self()->evalFile( m_module, modulePath );
    if ( PythonQt::self()->hadError() )
        cError() << "Loading" << modulePath << "probably failed.";

    // The @calamares_module decorator should have filled _calamares_module_typename
    // for us.
    m_viewclassName = m_module.getVariable( "_calamares_module_typename" ).toString();
}

::PythonQtObjectPtr
PythonQtModule::createViewStep(QWidget* parent)
{
    if ( m_viewclassName.isEmpty() )
    {
        cError() << "No view class name set by module" << m_moduleName;
        return nullptr;
    }
    if ( !PythonQt::self() )
    {
        cError() << "PythonQt not initialized";
        return nullptr;
    }

    // Instantiate an object of the class marked with @calamares_module and
    // store it as _calamares_module.
    PythonQt::self()->evalScript( m_module, QString( "_calamares_module = %1()" ).arg( m_viewclassName ) );
    auto obj = PythonQt::self()->lookupObject( m_module, "_calamares_module" );

    if ( obj.isNull() )
    {
        cError() << "Could not create view step from" << m_viewclassName;
        return nullptr;
    }

    obj.addObject( "_basewidget", parent );
    PythonQt::self()->evalScript( m_module, QStringLiteral( "_calamares_module._basewidget.layout().addWidget(_calamares_module.widget())" ) );

    CALAMARES_RETRANSLATE_WIDGET( parent,
        this->lookupAndCall( obj,
                                       { "retranslate" },
                                       { CalamaresUtils::translatorLocaleName() } );
    )

    return obj;
}

}
