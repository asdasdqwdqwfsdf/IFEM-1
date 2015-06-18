// $Id$
//==============================================================================
//!
//! \file ASMmxBase.h
//!
//! \date Dec 28 2010
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Base class for spline-based mixed finite element assembly drivers.
//!
//==============================================================================

#ifndef _ASM_MX_BASE_H
#define _ASM_MX_BASE_H

#include "MatVec.h"


/*!
  \brief Base class for spline-based mixed finite element assembly drivers.
*/

class ASMmxBase
{
protected:
  //! \brief The constructor sets the number of field variables.
  //! \param[in] n_f1 Number of nodal variables in field 1
  //! \param[in] n_f2 Number of nodal variables in field 2
  ASMmxBase(unsigned char n_f1, unsigned char n_f2);

  //! \brief Initializes the patch level MADOF array.
  //! \param[in] MLGN Matrix of local-to-global node numbers
  //! \param[out] sysMadof System-level matrix of accumulated DOFs
  void initMx(const std::vector<int>& MLGN, const int* sysMadof);

  //! \brief Extracts nodal results for this patch from the global vector.
  //! \param[in] globVec Global solution vector in DOF-order
  //! \param[out] nodeVec Nodal result vector for this patch
  //! \param[in] basis Which basis to extract the nodal values for
  void extractNodeVecMx(const Vector& globVec, Vector& nodeVec,
			int basis = 0) const;

  //! \brief Injects nodal results for this patch into a global vector.
  //! \param[in] globVec Global solution vector in DOF-order
  //! \param[out] nodeVec Nodal result vector for this patch
  //! \param[in] basis Which basis to extract the nodal values for
  void injectNodeVecMx(Vector& globVec, const Vector& nodeVec,
                       int basis = 0) const;

  //! \brief Extract the primary solution field at the specified nodes.
  //! \param[out] sField Solution field
  //! \param[in] locSol Solution vector local to current patch
  //! \param[in] nodes 1-based local node numbers to extract solution for
  bool getSolutionMx(Matrix& sField, const Vector& locSol,
		     const std::vector<int>& nodes) const;

public:
  //! If \e true, the first basis represents the geometry
  static bool geoUsesBasis1;

  //! \brief Mixed formulation type
  enum MixedType {
    FULL_CONT_RAISE_BASIS1,    //!< Full continuity, raise order and use as basis 1
    REDUCED_CONT_RAISE_BASIS1, //!< Reduced continuity, raise order and use as basis 1
    FULL_CONT_RAISE_BASIS2,    //!< Full continuity, raise order and use as basis 2
    REDUCED_CONT_RAISE_BASIS2, //!< Reduced continuity, raise order and use as basis 2
  };

  static MixedType Type; //!< Type of mixed formulation used

private:
  std::vector<int> MADOF; //!< Matrix of accumulated DOFs for this patch

protected:
  size_t nb1; //!< Number of basis functions in first basis
  size_t nb2; //!< Number of basis functions in second basis

  unsigned char nf1; //!< Number of solution fields using first basis
  unsigned char nf2; //!< Number of solution fields using second basis
};

#endif
