// $Id$
//==============================================================================
//!
//! \file SplineField3D.C
//!
//! \date Mar 28 2011
//!
//! \author Runar Holdahl / SINTEF
//!
//! \brief Class for spline-based finite element scalar field in 3D.
//!
//==============================================================================

#include "GoTools/trivariate/SplineVolume.h"

#include "SplineField3D.h"
#include "ASMs3D.h"
#include "ItgPoint.h"
#include "CoordinateMapping.h"
#include "Utilities.h"
#include "Vec3.h"
#include <array>


SplineField3D::SplineField3D (const ASMs3D* patch,
                              const RealArray& v, char nbasis,
                              char cmp, const char* name)
  : FieldBase(name), basis(patch->getBasis(nbasis)), vol(patch->getVolume())
{
  const int n1 = basis->numCoefs(0);
  const int n2 = basis->numCoefs(1);
  const int n3 = basis->numCoefs(2);
  nno = n1*n2*n3;

  const int p1 = basis->order(0);
  const int p2 = basis->order(1);
  const int p3 = basis->order(2);
  nelm = (n1-p1+1)*(n2-p2+1)*(n3-p3+1);

  nsd = patch->getNoSpaceDim();

  size_t ofs = 0;
  for (char i = 1; i < nbasis; ++i)
    ofs += patch->getNoNodes(i)*patch->getNoFields(i);
  auto vit = v.begin()+ofs;
  values.resize(nno);
  int nf = patch->getNoFields(nbasis);
  int ndof = nf > 1 && cmp > 0 ? nf*nno : nno;
  auto end = v.size() > ofs+ndof ? vit+ndof : v.end();
  if (nf == 1 || cmp == 0)
    std::copy(vit,end,values.begin());
  else
    for (size_t i = 0; i < nno && vit != end; ++i, vit += nf)
      values[i] = *(vit+cmp-1);
}


double SplineField3D::valueNode (size_t node) const
{
  return node > 0 && node <= nno ? values(node) : 0.0;
}


double SplineField3D::valueFE (const ItgPoint& x) const
{
  if (!basis) return false;

  // Evaluate the basis functions at the given point
  Go::BasisPts spline;
#pragma omp critical
  basis->computeBasis(x.u,x.v,x.w,spline);

  // Evaluate the solution field at the given point
  IntVec ip;
  ASMs3D::scatterInd(basis->numCoefs(0),basis->numCoefs(1),basis->numCoefs(2),
		     basis->order(0),basis->order(1),basis->order(2),
		     spline.left_idx,ip);

  Vector Vnod;
  utl::gather(ip,1,values,Vnod);
  return Vnod.dot(spline.basisValues);
}


double SplineField3D::valueCoor (const Vec4& x) const
{
  if (x.u)
    return this->valueFE(ItgPoint(x.u[0],x.u[1],x.u[2]));

  // Use with caution, very slow!
  Go::Point pt(x.x,x.y,x.z), clopt(3);
  double clo_u, clo_v, clo_w, dist;
#pragma omp critical
  vol->closestPoint(pt, clo_u, clo_v, clo_w, clopt, dist, 1.0e-5);

  return this->valueFE(ItgPoint(clo_u,clo_v,clo_w));
}


bool SplineField3D::valueGrid (RealArray& val, const int* npe) const
{
  val.clear();
  if (!basis) return false;

  // Compute parameter values of the visualization points
  std::array<RealArray,3> gpar;
  for (int dir = 0; dir < 3; dir++)
  {
    int nSegPerSpan = npe[dir] - 1;
    if (nSegPerSpan < 1) return false;

    RealArray::const_iterator uit = basis->basis(dir).begin();
    double ucurr = 0.0, uprev = *(uit++);
    while (uit != basis->basis(dir).end())
    {
      ucurr = *(uit++);
      if (ucurr > uprev)
        if (nSegPerSpan == 1)
          gpar[dir].push_back(uprev);
        else for (int i = 0; i < nSegPerSpan; i++)
        {
          double xg = (double)(2*i-nSegPerSpan)/(double)nSegPerSpan;
          gpar[dir].push_back(0.5*(ucurr*(1.0+xg) + uprev*(1.0-xg)));
        }
      uprev = ucurr;
    }

    if (ucurr > gpar[dir].back())
      gpar[dir].push_back(ucurr);
  }

  // Evaluate the field in the visualization points
  val.reserve(gpar[0].size()*gpar[1].size()*gpar[2].size());
  for (double w : gpar[2])
    for (double v : gpar[1])
      for (double u : gpar[0])
      {
        Go::BasisPts spline;
#pragma omp critical
        basis->computeBasis(u,v,w,spline);

        IntVec ip;
        ASMs3D::scatterInd(basis->numCoefs(0),basis->numCoefs(1),
                           basis->numCoefs(2),basis->order(0),
                           basis->order(1),basis->order(2),spline.left_idx,ip);

        Vector Vnod;
        utl::gather(ip,1,values,Vnod);
        val.push_back(Vnod.dot(spline.basisValues));
      }

  return true;
}


bool SplineField3D::gradFE (const ItgPoint& x, Vector& grad) const
{
  if (!basis) return false;
  if (!vol)   return false;

  // Evaluate the basis functions at the given point
  Go::BasisDerivs spline;
#pragma omp critical
  vol->computeBasis(x.u,x.v,x.w,spline);

  const int uorder = vol->order(0);
  const int vorder = vol->order(1);
  const int worder = vol->order(2);
  const size_t nen = uorder*vorder*worder;

  Matrix dNdu(nen,3), dNdX;
  for (size_t n = 1; n <= nen; n++)
  {
    dNdu(n,1) = spline.basisDerivs_u[n-1];
    dNdu(n,2) = spline.basisDerivs_v[n-1];
    dNdu(n,3) = spline.basisDerivs_w[n-1];
  }

  IntVec ip;
  ASMs3D::scatterInd(vol->numCoefs(0),vol->numCoefs(1),vol->numCoefs(2),
		     uorder,vorder,worder,spline.left_idx,ip);

  // Evaluate the Jacobian inverse
  Matrix Xnod(nsd,ip.size()), Jac;
  for (size_t i = 0; i < ip.size(); i++)
    Xnod.fillColumn(1+i,&(*vol->coefs_begin())+vol->dimension()*ip[i]);
  if (!utl::Jacobian(Jac,dNdX,Xnod,dNdu))
    return false; // Singular Jacobian

  // Evaluate the gradient of the solution field at the given point
  if (basis != vol)
  {
    // Mixed formulation, the solution uses a different basis than the geometry
#pragma omp critical
    basis->computeBasis(x.u,x.v,x.w,spline);

    const size_t nbf = basis->order(0)*basis->order(1)*basis->order(2);
    dNdu.resize(nbf,3);
    for (size_t n = 1; n <= nbf; n++)
    {
      dNdu(n,1) = spline.basisDerivs_u[n-1];
      dNdu(n,2) = spline.basisDerivs_v[n-1];
      dNdu(n,3) = spline.basisDerivs_w[n-1];
    }
    dNdX.multiply(dNdu,Jac); // dNdX = dNdu * Jac

    ip.clear();
    ASMs3D::scatterInd(basis->numCoefs(0),basis->numCoefs(1),basis->numCoefs(2),
		       basis->order(0),basis->order(1),basis->order(2),
		       spline.left_idx,ip);
  }

  Vector Vnod;
  utl::gather(ip,1,values,Vnod);
  return dNdX.multiply(Vnod,grad,true); // grad = dNdX * Vnod^t
}


bool SplineField3D::hessianFE (const ItgPoint& x, Matrix& H) const
{
  if (!basis) return false;
  if (!vol)  return false;

  Go::BasisDerivs2 spline2;
  Matrix3D d2Ndu2;
  IntVec ip;
  if (vol == basis) {
#pragma omp critical
    vol->computeBasis(x.u,x.v,x.w,spline2);

    const size_t nen = vol->order(0)*vol->order(1)*vol->order(2);
    d2Ndu2.resize(nen,3,3);
    for (size_t n = 1; n <= nen; n++) {
      d2Ndu2(n,1,1) = spline2.basisDerivs_uu[n-1];
      d2Ndu2(n,1,2) = d2Ndu2(n,2,1) = spline2.basisDerivs_uv[n-1];
      d2Ndu2(n,1,3) = d2Ndu2(n,3,1) = spline2.basisDerivs_uw[n-1];
      d2Ndu2(n,2,2) = spline2.basisDerivs_vv[n-1];
      d2Ndu2(n,2,3) = d2Ndu2(n,3,2) = spline2.basisDerivs_vw[n-1];
      d2Ndu2(n,3,3) = spline2.basisDerivs_ww[n-1];
    }

    ASMs3D::scatterInd(vol->numCoefs(0),vol->numCoefs(1),vol->numCoefs(2),
		       vol->order(0),vol->order(1),vol->order(2),
		       spline2.left_idx,ip);
  }
  else {
    // Mixed formulation, the solution uses a different basis than the geometry
#pragma omp critical
    basis->computeBasis(x.u,x.v,x.w,spline2);

    const size_t nbf = basis->order(0)*basis->order(1)*basis->order(2);
    d2Ndu2.resize(nbf,3,3);
    for (size_t n = 1; n <= nbf; n++) {
      d2Ndu2(n,1,1) = spline2.basisDerivs_uu[n-1];
      d2Ndu2(n,1,2) = d2Ndu2(n,2,1) = spline2.basisDerivs_uv[n-1];
      d2Ndu2(n,1,3) = d2Ndu2(n,3,1) = spline2.basisDerivs_uw[n-1];
      d2Ndu2(n,2,2) = spline2.basisDerivs_vv[n-1];
      d2Ndu2(n,2,3) = d2Ndu2(n,3,2) = spline2.basisDerivs_vw[n-1];
      d2Ndu2(n,3,3) = spline2.basisDerivs_ww[n-1];
    }

    ASMs3D::scatterInd(basis->numCoefs(0),basis->numCoefs(1),basis->numCoefs(2),
		       basis->order(0),basis->order(1),basis->order(2),
		       spline2.left_idx,ip);
  }

  Vector Vnod;
  utl::gather(ip,1,values,Vnod);
  return H.multiply(d2Ndu2,Vnod);
}
