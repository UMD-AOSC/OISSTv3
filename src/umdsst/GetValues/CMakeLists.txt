umdsst_target_sources(
    GetValues.cc
    GetValues.h
    LinearGetValues.cc
    LinearGetValues.h

    # Note: these are temporary, and should be removed once
    # Locations and GeoVaLs have a proper c++ interface
    LocationsWrapper.h
    GeoVaLsWrapper.h
    GeoVaLsWrapper.f90
    GeoVaLsWrapperAD.h
    GeoVaLsWrapperAD.f90
)
