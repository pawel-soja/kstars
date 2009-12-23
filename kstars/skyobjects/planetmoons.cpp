/***************************************************************************
                          planetmoons.cpp  -  description
                             -------------------
    begin                : Sat Mar 13 2009
                         : by Vipul Kumar Singh, Médéric Boquien
    email                : vipulkrsingh@gmail.com, mboquien@free.fr
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include <kdebug.h>

#include "ksnumbers.h"
#include "ksplanet.h"
#include "kssun.h"
#include "trailobject.h"

PlanetMoons::PlanetMoons(){
}

PlanetMoons::~PlanetMoons(){
    qDeleteAll( Moon );
}

QString PlanetMoons::name( int id ) const {
    return Moon[id]->translatedName();
}

TrailObject* PlanetMoons::moonNamed( const QString &name ) const {
  int nmoons = nMoons();
  
  for ( int i=0; i<nmoons; ++i ) {
        if ( Moon[i]->name() == name ) {
            return Moon[i];
        }
    }
    return 0;
}

void PlanetMoons::EquatorialToHorizontal( const dms *LST, const dms *lat ) {
  int nmoons = nMoons();
  
  for ( int i=0; i<nmoons; ++i )
        moon(i)->EquatorialToHorizontal( LST, lat );
}
