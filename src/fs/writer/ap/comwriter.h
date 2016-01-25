/*
 * AirportComWriter.h
 *
 *  Created on: 01.05.2015
 *      Author: alex
 */

#ifndef WRITER_COMWRITER_H_
#define WRITER_COMWRITER_H_

#include "fs/writer/writerbase.h"
#include "fs/bgl/ap/com.h"

namespace atools {
namespace fs {
namespace writer {

class ComWriter :
  public atools::fs::writer::WriterBase<atools::fs::bgl::Com>
{
public:
  ComWriter(atools::sql::SqlDatabase& db, atools::fs::writer::DataWriter& dataWriter)
    : WriterBase(db, dataWriter, "com")
  {
  }

  virtual ~ComWriter()
  {
  }

protected:
  virtual void writeObject(const atools::fs::bgl::Com *type);

};

} // namespace writer
} // namespace fs
} // namespace atools

#endif /* WRITER_COMWRITER_H_ */
