// $Id$
//==============================================================================
//!
//! \file SIMLinEl2D.h
//!
//! \date Feb 04 2010
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Solution driver for 2D NURBS-based linear elastic FEM analysis.
//!
//==============================================================================

#ifndef _SIM_LIN_EL_2D_H
#define _SIM_LIN_EL_2D_H

#include "SIM2D.h"
#include "SIMenums.h"


/*!
  \brief Driver class for 2D isogeometric FEM analysis of elasticity problems.
  \details The class incapsulates data and methods for solving linear elasticity
  problems using NURBS-based finite elements. It reimplements the parse method
  of the parent class.
*/

class SIMLinEl2D : public SIM2D
{
  //! \brief Struct for storage of physical material property parameters.
  struct IsoMat
  {
    double E;   //!< Young's modulus
    double nu;  //!< Poisson's ratio
    double rho; //!< Mass density
    //! \brief Default constructor.
    IsoMat() { E = nu = rho = 0.0; }
    //! \brief Constructor initializing a material instance.
    IsoMat(double Emod, double Poiss, double D) : E(Emod), nu(Poiss), rho(D) {}
  };

  typedef std::vector<IsoMat> MaterialVec; //!< Vector of material data sets

public:
  //! \brief Default constructor.
  //! \param[in] form Problem formulation option
  //! \param[in] planeStress Plane stress/plane strain option
  SIMLinEl2D(int form = SIM::LINEAR, bool planeStress = true);
  //! \brief Empty destructor.
  virtual ~SIMLinEl2D() {}

protected:
  //! \brief Parses a data section from the input stream.
  //! \param[in] keyWord Keyword of current data section to read
  //! \param is The file stream to read from
  virtual bool parse(char* keyWord, std::istream& is);

  //! \brief Initializes material properties for integration of interior terms.
  //! \param[in] propInd Physical property index
  virtual bool initMaterial(size_t propInd);

  //! \brief Initializes for integration of Neumann terms for a given property.
  //! \param[in] propInd Physical property index
  virtual bool initNeumann(size_t propInd);

private:
  MaterialVec mVec; //!< Material data
};

#endif
