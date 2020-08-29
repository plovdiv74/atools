/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "fs/db/ap/airportwriter.h"
#include "fs/db/meta/bglfilewriter.h"
#include "fs/db/meta/sceneryareawriter.h"
#include "fs/db/ap/rw/runwaywriter.h"
#include "fs/db/ap/approachwriter.h"
#include "fs/db/ap/startwriter.h"
#include "fs/db/ap/helipadwriter.h"
#include "fs/db/ap/parkingwriter.h"
#include "fs/db/ap/apronwriter.h"
#include "fs/db/ap/taxipathwriter.h"
#include "fs/bgl/nl/namelist.h"
#include "fs/bgl/nl/namelistentry.h"
#include "fs/db/datawriter.h"
#include "fs/bgl/util.h"
#include "geo/calculations.h"
#include "fs/bgl/ap/rw/runway.h"
#include "fs/bgl/ap/del/deleteairport.h"
#include "fs/db/dbairportindex.h"
#include "fs/db/nav/waypointwriter.h"
#include "fs/db/ap/comwriter.h"
#include "fs/db/ap/deleteairportwriter.h"
#include "fs/bgl/ap/parking.h"
#include "fs/scenery/languagejson.h"
#include "atools.h"

namespace atools {
namespace fs {
namespace db {

using atools::fs::bgl::ap::AVGAS;
using atools::fs::bgl::ap::JET_FUEL;
using atools::fs::bgl::Namelist;
using atools::fs::bgl::NamelistEntry;
using atools::fs::bgl::Com;
using atools::fs::bgl::Airport;
using atools::fs::bgl::Runway;
using atools::fs::bgl::Apron;
using atools::fs::bgl::Apron2;
using atools::fs::bgl::DeleteAirport;
using atools::geo::meterToFeet;

void AirportWriter::setNameLists(const QList<const Namelist *>& namelists)
{
  nameListIndex.clear();
  for(const Namelist *iter : namelists)
    for(const NamelistEntry& i : iter->getNameList())
      nameListIndex[i.getAirportIdent()] = &i;
}

void AirportWriter::writeObject(const Airport *type)
{
  if(!getOptions().isIncludedAirportIdent(type->getIdent()))
    return;

  if(type->isEmpty())
  {
    qWarning() << "Skipping empty airport" << type->getIdent();
    return;
  }

  DataWriter& dw = getDataWriter();
  BglFileWriter *bglFileWriter = dw.getBglFileWriter();

  if(type->isMsfsDummyAirport())
  {
    // Do a shortcut for MSFS dummies which transport only procedures and COM
    // Instead of writing a new airport simply add COM and procedures

    // Get the other airport id and remember the current one
    int predId = airportIdByIdent(type->getIdent());
    int currentId = dw.getAirportWriter()->setCurrentId(predId);

    // Update index
    currentIdent = type->getIdent();
    currentPos = type->getPosition().getPos();
    getAirportIndex()->add(type->getIdent(), predId);

    // Write features with other airport id
    ComWriter *comWriter = dw.getAirportComWriter();
    comWriter->write(type->getComs());

    ApproachWriter *appWriter = dw.getApproachWriter();
    appWriter->write(type->getApproaches());

    updateMsfsAirport(type, predId);

    // reset airport id to current
    dw.getAirportWriter()->setCurrentId(currentId);
  }
  else
  {
    // Check if this is an addon airport
    bool isRealAddon = getOptions().isAddonLocalPath(dw.getSceneryAreaWriter()->getCurrentSceneryLocalPath());
    bool isAddon = getOptions().isAddonDirectory(bglFileWriter->getCurrentFilepath()) && isRealAddon;

    if(isRealAddon && type->getDeleteAirports().isEmpty())
      qInfo() << "Addon airport without delete record" << type->getIdent();

    int nextAirportId = getNextId();

    const DeleteAirport *delAp = nullptr;
    if(!type->getDeleteAirports().isEmpty())
      delAp = &type->getDeleteAirports().first();

    deleteProcessor.init(delAp, type, getCurrentId());

    if(getOptions().isDeletes())
    {
      if(delAp != nullptr || isRealAddon)
        // Now delete the stock/default airport
        deleteProcessor.preProcessDelete();
    }

    QStringList sceneryLocalPaths, bglFilenames;
    if(!deleteProcessor.getSceneryLocalPath().isEmpty())
      sceneryLocalPaths.append(deleteProcessor.getSceneryLocalPath());
    if(!deleteProcessor.getBglFilename().isEmpty())
      bglFilenames.append(deleteProcessor.getBglFilename());

    sceneryLocalPaths.append(dw.getSceneryAreaWriter()->getCurrentSceneryLocalPath());
    bglFilenames.append(bglFileWriter->getCurrentFilename());

    // Get and write country, state and city =====================
    bindNullString(":country");
    bindNullString(":state");
    bindNullString(":city");

    if(type->getRegion().isEmpty())
      bindNullString(":region");
    else
      bind(":region", type->getRegion());

    NameListMapConstIterType it = nameListIndex.find(type->getIdent());
    if(it != nameListIndex.end())
    {
      const NamelistEntry *nl = it.value();
      if(nl != nullptr)
      {
        bind(":country", getDataWriter().getLanguage(nl->getCountryName()));
        bind(":state", getDataWriter().getLanguage(nl->getStateName()));
        bind(":city", getDataWriter().getLanguage(nl->getCityName()));

        if(!nl->getRegionIdent().isEmpty())
          bind(":region", nl->getRegionIdent());
      }
      else if(!type->isMsfsDummyAirport())
        qWarning().nospace().noquote() << "NameEntry for airport " << type->getIdent() << " is null";
    }
    else if(!type->isMsfsDummyAirport())
      qWarning().nospace().noquote() << "NameEntry for airport " << type->getIdent() << " not found";

    bind(":airport_id", nextAirportId);
    bind(":file_id", bglFileWriter->getCurrentId());
    bind(":ident", type->getIdent());
    bindNullString(":icao");
    bindNullString(":iata");
    bindNullString(":xpident");

    bind(":name", getDataWriter().getLanguage(type->getName()));

    bind(":fuel_flags", type->getFuelFlags());

    bindBool(":has_avgas", (type->getFuelFlags() & AVGAS) == AVGAS);
    bindBool(":has_jetfuel", (type->getFuelFlags() & JET_FUEL) == JET_FUEL);

    bindBool(":has_tower_object", type->hasTowerObj());

    int towerFrequency, unicomFrequency, awosFrequency, asosFrequency, atisFrequency;
    Airport::extractMainComFrequencies(
      type->getComs(), towerFrequency, unicomFrequency, awosFrequency, asosFrequency, atisFrequency);
    bindIntOrNull(":tower_frequency", towerFrequency);
    bindIntOrNull(":atis_frequency", atisFrequency);
    bindIntOrNull(":awos_frequency", awosFrequency);
    bindIntOrNull(":asos_frequency", asosFrequency);
    bindIntOrNull(":unicom_frequency", unicomFrequency);

    bindBool(":is_closed", type->isAirportClosed());
    bindBool(":is_military", type->isMilitary());

    bindBool(":is_addon", isAddon);
    bindBool(":is_3d", 0);

    bind(":num_com", type->getComs().size());

    bind(":num_parking_gate", type->getNumParkingGate());
    bind(":num_parking_ga_ramp", type->getNumParkingGaRamp());

    bind(":num_parking_cargo", type->getNumParkingCargo());
    bind(":num_parking_mil_cargo", type->getNumParkingMilitaryCargo());
    bind(":num_parking_mil_combat", type->getNumParkingMilitaryCombat());

    bind(":num_approach", type->getApproaches().size());

    int numHard = 0, numSoft = 0, numWater = 0;
    for(const Runway& rw : type->getRunways())
    {
      atools::fs::bgl::Surface surface = rw.getSurface();
      if(!rw.getMaterialUuid().isNull())
        surface = atools::fs::bgl::surface::surfaceToType(getDataWriter().getSurface(rw.getMaterialUuid()));

      numHard += atools::fs::bgl::surface::isHard(surface);
      numSoft += atools::fs::bgl::surface::isSoft(surface);
      numWater += atools::fs::bgl::surface::isWater(surface);
    }

    bind(":num_runway_soft", numSoft);
    bind(":num_runway_hard", numHard);
    bind(":num_runway_water", numWater);

    bind(":num_runway_light", type->getNumLightRunway());
    bind(":num_runway_end_closed", type->getNumRunwayEndClosed());
    bind(":num_runway_end_vasi", type->getNumRunwayEndVasi());
    bind(":num_runway_end_als", type->getNumRunwayEndApproachLight());
    bind(":num_apron", type->getAprons().size());
    bind(":num_taxi_path", type->getTaxiPaths().size());
    bind(":num_helipad", type->getHelipads().size());
    bind(":num_jetway", type->getNumJetway());
    bind(":num_starts", type->getStarts().size());
    bindNullInt(":num_runway_end_ils"); // Will be set later by SQL script "update_ils_ids.sql"

    bind(":longest_runway_length", roundToInt(meterToFeet(type->getLongestRunwayLength())));
    bind(":longest_runway_width", roundToInt(meterToFeet(type->getLongestRunwayWidth())));
    bind(":longest_runway_heading", type->getLongestRunwayHeading());
    bind(":longest_runway_surface", atools::fs::bgl::surface::surfaceToDbStr(type->getLongestRunwaySurface()));

    bind(":num_runways", type->getRunways().size());

    bind(":largest_parking_ramp",
         bgl::util::enumToStr(bgl::Parking::parkingTypeToStr, type->getLargestParkingGaRamp()));
    bind(":largest_parking_gate", bgl::util::enumToStr(bgl::Parking::parkingTypeToStr, type->getLargestParkingGate()));

    bind(":rating", type->calculateRating(isAddon));

    bind(":scenery_local_path", sceneryLocalPaths.join(", "));
    bind(":bgl_filename", bglFilenames.join(", "));

    bind(":left_lonx", type->getBoundingRect().getTopLeft().getLonX());
    bind(":top_laty", type->getBoundingRect().getTopLeft().getLatY());
    bind(":right_lonx", type->getBoundingRect().getBottomRight().getLonX());
    bind(":bottom_laty", type->getBoundingRect().getBottomRight().getLatY());

    bind(":mag_var", getDataWriter().getMagVar(type->getPosition().getPos(), type->getMagVar()));

    if(!type->getTowerPosition().getPos().isNull() && type->getTowerPosition().getPos().isValidRange() &&
       !type->getTowerPosition().getPos().isPole())
    {
      bind(":tower_altitude", roundToInt(meterToFeet(type->getPosition().getAltitude())));
      bind(":tower_lonx", type->getTowerPosition().getLonX());
      bind(":tower_laty", type->getTowerPosition().getLatY());
    }
    else
    {
      bindNullFloat(":tower_altitude");
      bindNullFloat(":tower_lonx");
      bindNullFloat(":tower_laty");
    }

    bind(":altitude", roundToInt(meterToFeet(type->getPosition().getAltitude())));
    bind(":lonx", type->getPosition().getLonX());
    bind(":laty", type->getPosition().getLatY());

    // Write the airport to the database
    executeStatement();

    // Update index
    currentIdent = type->getIdent();
    currentPos = type->getPosition().getPos();
    getAirportIndex()->add(type->getIdent(), getCurrentId());

    // Write all subrecords now since the airport id is not available - this keeps the foreign keys valid
    RunwayWriter *rwWriter = dw.getRunwayWriter();
    rwWriter->write(type->getRunways());

    WaypointWriter *waypointWriter = dw.getWaypointWriter();
    waypointWriter->write(type->getWaypoints());

    ComWriter *comWriter = dw.getAirportComWriter();
    comWriter->write(type->getComs());

    ApproachWriter *appWriter = dw.getApproachWriter();
    appWriter->write(type->getApproaches());

    // Helipads will take the start index + the current start id as reference to starts
    HelipadWriter *heliWriter = dw.getHelipadWriter();
    heliWriter->write(type->getHelipads());

    // Starts have to be written after helipads
    StartWriter *startWriter = dw.getStartWriter();
    startWriter->write(type->getStarts());

    ParkingWriter *parkWriter = dw.getParkingWriter();
    parkWriter->write(type->getParkings());

    // Apron writer needs two records
    ApronWriter *apronWriter = dw.getApronWriter();
    const QList<bgl::Apron>& aprons = type->getAprons();
    const QList<bgl::Apron2>& aprons2 = type->getAprons2();
    for(int i = 0; i < aprons.size(); i++)
      apronWriter->writeOne(std::make_pair(&aprons.at(i), aprons2.isEmpty() ? nullptr : &aprons2.at(i)));

    TaxiPathWriter *taxiWriter = dw.getTaxiPathWriter();
    taxiWriter->write(type->getTaxiPaths());

    if(getOptions().isDeletes())
    {
      if(delAp != nullptr)
      {
        // Write metadata for delete record
        dw.getDeleteAirportWriter()->writeOne(delAp);

        if(getOptions().isDeletes())
          // Now delete the stock/default airport
          deleteProcessor.postProcessDelete();
      }
      else if(isRealAddon)
        deleteProcessor.postProcessDelete();
    }
  }
}

int atools::fs::db::AirportWriter::airportIdByIdent(const QString& ident)
{
  atools::sql::SqlQuery query(getDataWriter().getDatabase());
  query.prepare("select airport_id from airport where ident = :ident");
  query.bindValue(":ident", ident);
  query.exec();
  int newId = -1;
  if(query.next())
    newId = query.valueInt(0);

  if(newId == -1)
    qWarning() << Q_FUNC_INFO << "Other airport with ident" << ident << "not found";
  return newId;
}

void atools::fs::db::AirportWriter::updateMsfsAirport(const Airport *type, int predId)
{
  int towerFrequency, unicomFrequency, awosFrequency, asosFrequency, atisFrequency;
  Airport::extractMainComFrequencies(type->getComs(), towerFrequency, unicomFrequency, awosFrequency, asosFrequency,
                                     atisFrequency);

  atools::sql::SqlQuery query(getDataWriter().getDatabase());
  query.prepare("update airport set tower_frequency = :tower_frequency, atis_frequency = :atis_frequency, "
                "awos_frequency = :awos_frequency, asos_frequency = :asos_frequency, "
                "unicom_frequency = :unicom_frequency, num_com = :num_com, num_approach = :num_approach "
                "where airport_id = :id");

  query.bindValue(":tower_frequency", towerFrequency);
  query.bindValue(":atis_frequency", atisFrequency);
  query.bindValue(":awos_frequency", awosFrequency);
  query.bindValue(":asos_frequency", asosFrequency);
  query.bindValue(":unicom_frequency", unicomFrequency);
  query.bindValue(":num_com", type->getComs().size());
  query.bindValue(":num_approach", type->getApproaches().size());
  query.bindValue(":id", predId);
  query.exec();
}

} // namespace writer
} // namespace fs
} // namespace atools
