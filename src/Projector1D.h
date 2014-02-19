#ifndef PROJECTOR1D_H
#define PROJECTOR1D_H

#include "Projector.h"
#include "PicParams.h"



//----------------------------------------------------------------------------------------------------------------------
//! class Projector1D: defines a virtual method for projection in 1d3v simulations
//----------------------------------------------------------------------------------------------------------------------
class Projector1D : public Projector
{

public:
    //! Constructor for Projector1D
    Projector1D(PicParams* params, SmileiMPI* smpi) : Projector(params, smpi) {};

    virtual ~Projector1D() {};

    //! \todo comment this overloading of () operator (MG for JD)
    virtual void operator() (ElectroMagn* champs, Particles &particles, int ipart, double gf) = 0;

    //!\todo comment this overloading of () operator (MG for JD)
    virtual void operator() (Field* rho, Particles &particles, int ipart) = 0;

    //!\todo comment this overloading of () operator (MG for JD)
    virtual void operator() (Field* Jx, Field* Jy, Field* Jz, Field* rho, Particles &particles, int ipart, double gf) = 0;

    //!\todo comment this overloading of () operator (MG for JD)
    virtual void operator() (Field* Jx, Field* Jy, Field* Jz, Particles &particles, int ipart, LocalFields Jion) = 0;

    //! \todo comment this overloading of () operator (MG for JD)
    virtual void operator() (double* Jx, double* Jy, double* Jz, Particles &particles, int ipart, double gf, unsigned int bin, unsigned int b_dim0) = 0;

protected:
    //! Inverse of the spatial step 1/dx
    double dx_inv_;
    int index_domain_begin;
};

#endif

