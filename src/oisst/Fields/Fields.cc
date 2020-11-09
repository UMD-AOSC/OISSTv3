/*
 * (C) Copyright 2020-2020 UCAR, University of Maryland
 *
 * This software is licensed under the terms of the Apache Licence Version 2.0
 * which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
 */

#include <limits>
#include <string>

#include "netcdf"

#include "oisst/Fields/Fields.h"
#include "oisst/Geometry/Geometry.h"
#include "oisst/State/State.h"

#include "atlas/array.h"
#include "atlas/field.h"
#include "atlas/option.h"

#include "oops/util/abor1_cpp.h"
#include "oops/util/Logger.h"
#include "oops/util/missingValues.h"

using atlas::array::make_view;
using atlas::option::name;

namespace oisst {

// ----------------------------------------------------------------------------

  Fields::Fields(const Geometry & geom, const oops::Variables & vars,
                 const util::DateTime & vt)
    : geom_(new Geometry(geom)), missing_(util::missingValue(this->missing_)),
      time_(vt), vars_(vars) {
    // the constructor that gets called by everything (all State
    //  and Increment constructors ultimately end up here)
    if (vars_.size() != 1) {
      util::abor1_cpp("Fields::Fields(), vars_.size() != 1",
                       __FILE__, __LINE__);
    }

    atlasFieldSet_.reset(new atlas::FieldSet());
    for (int i = 0; i < vars_.size(); i++) {
      std::string var = vars_[i];
      atlas::Field fld = geom_->atlasFunctionSpace()->createField<double>(
                         name(var));
      auto fd = make_view<double, 1>(fld);
      fd.assign(0.0);

      atlasFieldSet_->add(fld);
    }
  }

// ----------------------------------------------------------------------------

  Fields::Fields(const Fields & other)
    : Fields(*other.geom_, other.vars_, other.time_) {
    // copy data from object other
    *this = other;
  }

// ----------------------------------------------------------------------------
  Fields & Fields::operator =(const Fields & other) {
    const int size = geom_->atlasFunctionSpace()->size();
    for (int i = 0; i < vars_.size(); i++) {
      auto fd       = make_view<double, 1>(atlasFieldSet_->field(0));
      auto fd_other = make_view<double, 1>(other.atlasFieldSet_->field(0));
      for (int j = 0; j < size; j++)
        fd(j) = fd_other(j);
    }
    return *this;
  }

// ----------------------------------------------------------------------------

  Fields & Fields::operator+=(const Fields &other) {
    const int size = geom_->atlasFunctionSpace()->size();
    auto fd       = make_view<double, 1>(atlasFieldSet_->field(0));
    auto fd_other = make_view<double, 1>(other.atlasFieldSet_->field(0));

    for (int j = 0; j < size; j++) {
      if (fd(j) == missing_ || fd_other(j) == other.missing_)
        fd(j) = missing_;
      else
        fd(j) += fd_other(j);
    }

    return *this;
  }

// ----------------------------------------------------------------------------

  void Fields::accumul(const double &zz, const Fields &rhs) {
    const size_t size = geom_->atlasFunctionSpace()->size();
    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));
    auto fd_rhs = make_view<double, 1>(rhs.atlasFieldSet()->field(0));

    for (size_t i = 0; i < size; i++) {
      if (fd(i) == missing_ || fd_rhs(i) == missing_)
        fd(i) = missing_;
      else
        fd(i) +=  zz*fd_rhs(i);
    }
  }

// ----------------------------------------------------------------------------

  double Fields::norm() const {
    const int size = geom_->atlasFunctionSpace()->size();
    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));

    int nValid = 0;
    double norm = 0.0, s = 0.0;
    for (int i = 0; i < size; i++) {
      if (fd(i) != missing_) {
        nValid += 1;
        s += fd(i)*fd(i);
      }
    }

    if (nValid == 0)
      norm = 0.0;
    else
      norm = sqrt(s/(1.0*nValid));

    return norm;
  }

// ----------------------------------------------------------------------------

  void Fields::zero() {
    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));
    const int size = geom_->atlasFunctionSpace()->size();

    for (int i = 0; i < size; i++)
        fd(i) = 0.0;
  }

// ----------------------------------------------------------------------------

  void Fields::read(const eckit::Configuration & conf) {
    int time = 0, lon = 0, lat = 0;
    std::string filename;

    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));
    const int size = geom_->atlasFunctionSpace()->size();

    // get filename
    if (!conf.get("filename", filename))
      util::abor1_cpp("Increment::read(), Get filename failed.",
        __FILE__, __LINE__);

    // open netCDF file
    netCDF::NcFile file(filename.c_str(), netCDF::NcFile::read);
    if (file.isNull())
      util::abor1_cpp("Increment::read(), Create netCDF file failed.",
        __FILE__, __LINE__);

    // get file dimensions
    time = static_cast<int>(file.getDim("time").getSize());
    lon  = static_cast<int>(file.getDim("lon").getSize());
    lat  = static_cast<int>(file.getDim("lat").getSize());
    if (time != 1 ||
        lat != static_cast<int>(geom_->atlasFunctionSpace()->grid().ny()) ||
        lon != static_cast<int>((((atlas::RegularLonLatGrid&)  // LC: no &?
               (geom_->atlasFunctionSpace()->grid()))).nx()) ) {
      util::abor1_cpp("Fields::read(), lat!=ny or lon!=nx",
        __FILE__, __LINE__);
    }

    // get sst data
    netCDF::NcVar sstVar;
    sstVar = file.getVar("sst");
    if (sstVar.isNull())
      util::abor1_cpp("Get sst var failed.", __FILE__, __LINE__);
    float  sstData[lat][lon];
    sstVar.getVar(sstData);  // if used double, read-in data would be wrong.

    // mask missing values
    const double epsilon = 1.0e-6;
    const double missing_nc = -32768.0;
    for (int j = 0; j < lat; j++)
      for (int i = 0; i < lon; i++)
        if (abs(sstData[j][i]-(missing_nc)) < epsilon)
          sstData[j][i] = missing_;

    // float to double
    int idx = 0;
    for (int j = 0; j < lat; j++)
      for (int i = 0; i < lon; i++)
        fd(idx++) = static_cast<double>(sstData[j][i]);
  }

// ----------------------------------------------------------------------------

  void Fields::write(const eckit::Configuration & conf) const {
    int lat, lon, time = 1;
    std::string filename;

    // get filename
    if (!conf.get("filename", filename))
      util::abor1_cpp("Increment::write(), Get filename failed.",
                      __FILE__, __LINE__);
    else
      oops::Log::info() << "Increment::write(), filename=" << filename
                        << std::endl;

    // create netCDF file
    netCDF::NcFile file(filename.c_str(), netCDF::NcFile::replace);
    if (file.isNull())
      util::abor1_cpp("Increment::write(), Create netCDF file failed.",
                      __FILE__, __LINE__);

    // define dims
    lat = geom_->atlasFunctionSpace()->grid().ny();
    lon =((atlas::RegularLonLatGrid)(geom_->atlasFunctionSpace()->grid())).nx();

    // unlimited dim if without size parameter, then it'll be 0,
    // what about the size?
    netCDF::NcDim timeDim = file.addDim("time", 1);
    netCDF::NcDim latDim  = file.addDim("lat" , lat);
    netCDF::NcDim lonDim  = file.addDim("lon" , lon);
    if (timeDim.isNull() || latDim.isNull() || lonDim.isNull())
      util::abor1_cpp("Increment::write(), Define dims failed.",
                      __FILE__, __LINE__);

    std::vector<netCDF::NcDim> dims;
    dims.push_back(timeDim);
    dims.push_back(latDim);
    dims.push_back(lonDim);

    // Lignag: define coordinate vars "lat" and "lon"
    // Ligang: define units atts for coordinate vars

    // inside OISSTv3/JEDI, use double, read/write use float
    // to make it consistent with the netCDF files.
    netCDF::NcVar sstVar = file.addVar(std::string("sst"),
                                       netCDF::ncFloat, dims);

    // define units atts for data vars
    const float fillvalue = -32768.0;
    sstVar.putAtt("units", "K");
    sstVar.putAtt("_FillValue", netCDF::NcFloat(), fillvalue);
    sstVar.putAtt("missing_value", netCDF::NcFloat(), fillvalue);

    // write data to the file
    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));
    float sstData[time][lat][lon];
    int idx = 0;
    for (int j = 0; j < lat; j++)
      for (int i = 0; i < lon; i++) {
        if (fd(idx) == missing_)
          sstData[0][j][i] = fillvalue;
        else
          sstData[0][j][i] = static_cast<float>(fd(idx));
        idx++;
      }

    sstVar.putVar(sstData);

    oops::Log::info() << "Fields::write(), Successfully write data to file!"
                      << std::endl;
  }

// ----------------------------------------------------------------------------

  std::shared_ptr<const Geometry> Fields::geometry() const {
    return geom_;
  }

// ----------------------------------------------------------------------------

  std::shared_ptr<atlas::FieldSet> Fields::atlasFieldSet() const {
    return atlasFieldSet_;
  }

// ----------------------------------------------------------------------------

  void Fields::print(std::ostream & os) const {
    auto fd = make_view<double, 1>(atlasFieldSet_->field(0));
    const int size = geom_->atlasFunctionSpace()->size();
    double mean = 0.0, sum = 0.0,
           min = std::numeric_limits<double>::max(),
           max = std::numeric_limits<double>::min();
    int nValid = 0;

    for (int i = 0; i < size; i++)
      if (fd(i) != missing_) {
        if (fd(i) < min) min = fd(i);
        if (fd(i) > max) max = fd(i);

        sum += fd(i);
        nValid++;
      }

    if (nValid == 0) {
      mean = 0.0;
      oops::Log::debug() << "State::print(), nValid == 0!" << std::endl;
    } else {
      mean = sum / (1.0*nValid);
    }

    os << "min = " << min << ", max = " << max << ", mean = " << mean
       << std::endl;
  }

// ----------------------------------------------------------------------------

}  // namespace oisst
