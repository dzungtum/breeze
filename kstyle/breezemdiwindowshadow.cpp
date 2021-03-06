/*************************************************************************
 * Copyright (C) 2014 by Hugo Pereira Da Costa <hugo.pereira@free.fr>    *
 *                                                                       *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 *************************************************************************/

#include "breezemdiwindowshadow.h"

#include "breeze.h"
#include "breezeshadowhelper.h"
#include "breezestyleconfigdata.h"

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QTextStream>

namespace Breeze
{

    //____________________________________________________________________
    MdiWindowShadow::MdiWindowShadow( QWidget* parent, TileSet shadowTiles ):
        QWidget( parent ),
        _shadowTiles( shadowTiles )
    {
        setAttribute( Qt::WA_OpaquePaintEvent, false );
        setAttribute( Qt::WA_TransparentForMouseEvents, true );
        setFocusPolicy( Qt::NoFocus );
    }

    //____________________________________________________________________
    void MdiWindowShadow::updateGeometry()
    {
        if( !_widget ) return;

        // metrics
        const int shadowSize = ShadowHelper::shadowSize( StyleConfigData::shadowSize() );
        const int shadowOffset = qMax( shadowSize/2, Metrics::Shadow_Overlap*2 );
        const int size( shadowSize - Metrics::Shadow_Overlap );
        const int topSize( size - shadowOffset );
        const int bottomSize( size );
        const int leftSize( size );
        const int rightSize( size );

        // get tileSet rect
        auto hole = _widget->frameGeometry();
        _shadowTilesRect = hole.adjusted( -leftSize, -topSize, rightSize, bottomSize );

        // get parent MDI area's viewport
        auto parent( parentWidget() );
        if (parent && !qobject_cast<QMdiArea *>(parent) && qobject_cast<QMdiArea*>(parent->parentWidget()))
        { parent = parent->parentWidget(); }

        if( qobject_cast<QAbstractScrollArea *>( parent ) )
        { parent = qobject_cast<QAbstractScrollArea *>( parent )->viewport(); }

        // set geometry
        QRect geometry( _shadowTilesRect );
        if( parent )
        {
            geometry &= parent->rect();
            hole &= parent->rect();
        }

        // update geometry and mask
        const QRegion mask = QRegion( geometry ) - hole.adjusted( 2, 2, -2, -2 );
        if( mask.isEmpty() ) hide();
        else {

            setGeometry( geometry );
            setMask( mask.translated( -geometry.topLeft() ) );
            show();

        }

        // translate rendering rect
        _shadowTilesRect.translate( -geometry.topLeft() );

    }

    //____________________________________________________________________
    void MdiWindowShadow::updateZOrder()
    { stackUnder( _widget ); }

    //____________________________________________________________________
    void MdiWindowShadow::paintEvent( QPaintEvent* event )
    {

        if( !_shadowTiles.isValid() ) return;

        QPainter painter( this );
        painter.setRenderHints( QPainter::Antialiasing );
        painter.setClipRegion( event->region() );
        _shadowTiles.render( _shadowTilesRect, &painter );

    }

    //____________________________________________________________________
    MdiWindowShadowFactory::MdiWindowShadowFactory( QObject* parent ):
        QObject( parent )
    {}

    //____________________________________________________________________________________
    bool MdiWindowShadowFactory::registerWidget( QWidget* widget )
    {

        // check widget type
        auto subwindow( qobject_cast<QMdiSubWindow*>( widget ) );
        if( !subwindow ) return false;
        if( subwindow->widget() && subwindow->widget()->inherits( "KMainWindow" ) ) return false;

        // make sure widget is not already registered
        if( isRegistered( widget ) ) return false;

        // store in set
        _registeredWidgets.insert( widget );

        // create shadow immediatly if widget is already visible
        if( widget->isVisible() )
        {
            installShadow( widget );
            updateShadowGeometry( widget );
            updateShadowZOrder( widget );
        }

        widget->installEventFilter( this );

        // catch object destruction
        connect( widget, SIGNAL(destroyed(QObject*)), SLOT(widgetDestroyed(QObject*)) );

        return true;

    }

    //____________________________________________________________________________________
    void MdiWindowShadowFactory::unregisterWidget( QWidget* widget )
    {
        if( !isRegistered( widget ) ) return;
        widget->removeEventFilter( this );
        _registeredWidgets.remove( widget );
        removeShadow( widget );
    }

    //____________________________________________________________________________________
    bool MdiWindowShadowFactory::eventFilter( QObject* object, QEvent* event )
    {

        switch( event->type() )
        {
            // TODO: possibly implement ZOrderChange event, to make sure that
            // the shadow is always painted on top
            case QEvent::ZOrderChange:
            updateShadowZOrder( object );
            break;

            case QEvent::Destroy:
            if( isRegistered( object ) )
            {
                _registeredWidgets.remove( object );
                removeShadow( object );
            }
            break;

            case QEvent::Hide:
            hideShadows( object );
            break;

            case QEvent::Show:
            installShadow( object );
            updateShadowGeometry( object );
            updateShadowZOrder( object );
            break;

            case QEvent::Move:
            case QEvent::Resize:
            updateShadowGeometry( object );
            break;

            default: break;
        }

        return QObject::eventFilter( object, event );

    }

    //____________________________________________________________________________________
    MdiWindowShadow* MdiWindowShadowFactory::findShadow( QObject* object ) const
    {

        // check object,
        if( !object->parent() ) return nullptr;

        // find existing window shadows
        auto children = object->parent()->children();
        foreach( QObject *child, children )
        {
            if( MdiWindowShadow* shadow = qobject_cast<MdiWindowShadow*>(child) )
            { if( shadow->widget() == object ) return shadow; }
        }

        return nullptr;

    }

    //____________________________________________________________________________________
    void MdiWindowShadowFactory::installShadow( QObject* object )
    {

        // cast
        auto widget( static_cast<QWidget*>( object ) );
        if( !widget->parentWidget() ) return;

        // make sure shadow is not already installed
        if( findShadow( object ) ) return;

        // create new shadow
        auto windowShadow( new MdiWindowShadow( widget->parentWidget(), _shadowTiles ) );
        windowShadow->setWidget( widget );
        return;

    }

    //____________________________________________________________________________________
    void MdiWindowShadowFactory::removeShadow( QObject* object )
    {
        if( MdiWindowShadow* windowShadow = findShadow( object ) )
        {
            windowShadow->hide();
            windowShadow->deleteLater();
        }
    }

    //____________________________________________________________________________________
    void MdiWindowShadowFactory::widgetDestroyed( QObject* object )
    { _registeredWidgets.remove( object ); }

}
