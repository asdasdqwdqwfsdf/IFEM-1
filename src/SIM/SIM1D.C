// $Id$
//==============================================================================
//!
//! \file SIM1D.C
//!
//! \date Feb 04 2010
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Solution driver for 1D NURBS-based FEM analysis.
//!
//==============================================================================

#include "SIM1D.h"
#include "ModelGenerator.h"
#include "ASMs1D.h"
#include "Functions.h"
#include "Utilities.h"
#include "Vec3Oper.h"
#include "IFEM.h"
#include "tinyxml.h"
#include <fstream>


SIM1D::SIM1D (unsigned char n1, bool)
{
  nsd = 1;
  nf = n1;
}


SIM1D::SIM1D (const CharVec& fields, bool)
{
  nsd = 1;
  nf = fields.empty() ? 1 : fields.front();
  std::cerr <<"  ** Mixed interpolation not implemented for 1D."<< std::endl;
}


SIM1D::SIM1D (IntegrandBase* itg, unsigned char n) : SIMgeneric(itg)
{
  nsd = 1;
  nf = n;
}


bool SIM1D::addConnection (int master, int slave, int mIdx, int sIdx,
                           int, int basis, bool, int dim, int thick)
{
  if (basis > 0)
  {
    std::cerr <<" *** SIM1D::addConnection: Mixed not implemented."<< std::endl;
    return false;
  }

  int lmaster = this->getLocalPatchIndex(master);
  int lslave = this->getLocalPatchIndex(slave);
  if (lmaster > 0 && lslave > 0)
  {
    if (dim != 0) return false;

    IFEM::cout <<"\tConnecting P"<< slave <<" V"<< sIdx
               <<" to P"<< master <<" V"<< mIdx << std::endl;

    ASM1D* spch = dynamic_cast<ASM1D*>(myModel[lslave-1]);
    ASM1D* mpch = dynamic_cast<ASM1D*>(myModel[lmaster-1]);
    if (spch && mpch && !spch->connectPatch(sIdx,*mpch,mIdx,thick))
      return false;

    myInterfaces.push_back(ASM::Interface{master, slave, mIdx, sIdx, 0,
                                          dim, basis, thick});
  }
  else
    adm.dd.ghostConnections.insert(ASM::Interface{master, slave,
                                                  mIdx, sIdx, 0,
                                                  dim, basis, thick});

  return true;
}


bool SIM1D::parseGeometryTag (const TiXmlElement* elem)
{
  IFEM::cout <<"  Parsing <"<< elem->Value() <<">"<< std::endl;

  if (!strcasecmp(elem->Value(),"refine") && !isRefined)
  {
    IntVec patches;
    if (!this->parseTopologySet(elem,patches))
      return false;

    RealArray xi;
    if (!this->parseXi(elem,xi) && !utl::parseKnots(elem,xi))
    {
      int addu = 0;
      utl::getAttribute(elem,"u",addu);
      if (addu > 0)
        for (int j : patches)
        {
          IFEM::cout <<"\tRefining P"<< j <<" "<< addu << std::endl;
          ASM1D* pch = dynamic_cast<ASM1D*>(this->getPatch(j,true));
          if (pch) pch->uniformRefine(addu);
        }
    }
    else if (!xi.empty())
    {
      const char* refdata = nullptr;
      if (elem->FirstChild())
        refdata = elem->FirstChild()->Value();

      for (int j : patches)
      {
        IFEM::cout <<"\tRefining P"<< j <<" with ";
        if (refdata && isalpha(refdata[0]))
          IFEM::cout <<"grading "<< refdata <<":";
        else
          IFEM::cout <<"explicit knots:";
        for (size_t i = 0; i < xi.size(); i++)
          IFEM::cout << (i%10 || xi.size() < 11 ? " " : "\n\t") << xi[i];
        IFEM::cout << std::endl;
        ASM1D* pch = dynamic_cast<ASM1D*>(this->getPatch(j,true));
        if (pch) pch->refine(xi);
      }
    }
  }

  else if (!strcasecmp(elem->Value(),"raiseorder") && !isRefined)
  {
    IntVec patches;
    if (!this->parseTopologySet(elem,patches))
      return false;

    int addu = 0;
    utl::getAttribute(elem,"u",addu);
    for (int j : patches)
    {
      IFEM::cout <<"\tRaising order of P"<< j <<" "<< addu << std::endl;
      ASM1D* pch = dynamic_cast<ASM1D*>(this->getPatch(j,true));
      if (pch) pch->raiseOrder(addu);
    }
  }

  else if (!strcasecmp(elem->Value(),"topology"))
  {
    if (!this->createFEMmodel()) return false;

    const TiXmlElement* child = elem->FirstChildElement("connection");
    for (; child; child = child->NextSiblingElement())
    {
      int master = 0, slave = 0, mVert = 0, sVert = 0;
      utl::getAttribute(child,"master",master);
      utl::getAttribute(child,"mvert",mVert);
      utl::getAttribute(child,"slave",slave);
      utl::getAttribute(child,"svert",sVert);

      if (master == slave ||
          master < 1 || master > (int)myModel.size() ||
          slave  < 1 || slave  > (int)myModel.size())
      {
        std::cerr <<" *** SIM1D::parse: Invalid patch indices "
                  << master <<" "<< slave << std::endl;
        return false;
      }

      IFEM::cout <<"\tConnecting P"<< slave <<" V"<< sVert
                 <<" to P"<< master <<" V"<< mVert << std::endl;

      ASM1D* spch = dynamic_cast<ASM1D*>(myModel[slave-1]);
      ASM1D* mpch = dynamic_cast<ASM1D*>(myModel[master-1]);
      if (spch && mpch && !spch->connectPatch(sVert,*mpch,mVert))
        return false;
    }
  }

  else if (!strcasecmp(elem->Value(),"periodic"))
    return this->parsePeriodic(elem);

  else if (!strcasecmp(elem->Value(),"projection") && !isRefined)
  {
    const TiXmlElement* child = elem->FirstChildElement();
    if (child && !strncasecmp(child->Value(),"patch",5) && child->FirstChild())
    {
      // Read projection basis from file
      const char* patch = child->FirstChild()->Value();
      std::istream* isp = getPatchStream(child->Value(),patch);
      if (!isp) return false;

      for (ASMbase* pch : myModel)
        pch->createProjectionBasis(false);

      bool ok = true;
      for (int pid = 1; isp->good() && ok; pid++)
      {
        IFEM::cout <<"\tReading projection basis for patch "<< pid << std::endl;
        ASMbase* pch = this->getPatch(pid,true);
        if (pch)
          ok = pch->read(*isp);
        else if ((pch = ASM1D::create(ASM::Spline,nsd,nf)))
        {
          // Skip this patch
          ok = pch->read(*isp);
          delete pch;
        }
      }

      delete isp;
      if (!ok) return false;
    }
    else // Generate separate projection basis from current geometry basis
      for (ASMbase* pch : myModel)
        pch->createProjectionBasis(true);

    // Apply refine and/raise-order commands to define the projection basis
    for (; child; child = child->NextSiblingElement())
      if (!strcasecmp(child->Value(),"refine") ||
          !strcasecmp(child->Value(),"raiseorder"))
        if (!this->parseGeometryTag(child))
          return false;

    for (ASMbase* pch : myModel)
      if (!pch->createProjectionBasis(false))
      {
        std::cerr <<" *** SIM1D::parseGeometryTag: Failed to create projection"
                  <<" basis, check patch file specification."<< std::endl;
        return false;
      }
  }

  return true;
}


bool SIM1D::parseBCTag (const TiXmlElement* elem)
{
  if (!strcasecmp(elem->Value(),"fixpoint") && !ignoreDirichlet)
  {
    if (!this->createFEMmodel()) return false;

    int patch = 0, code = 123;
    double rx = 0.0;
    utl::getAttribute(elem,"patch",patch);
    utl::getAttribute(elem,"code",code);
    utl::getAttribute(elem,"rx",rx);
    int pid = this->getLocalPatchIndex(patch);
    if (pid < 1) return pid == 0;

    IFEM::cout <<"\tConstraining P"<< patch
               <<" point at "<< rx <<" with code "<< code << std::endl;

    ASM1D* pch = dynamic_cast<ASM1D*>(myModel[pid-1]);
    if (pch) pch->constrainNode(rx,code);
  }

  return true;
}


bool SIM1D::parse (const TiXmlElement* elem)
{
  // Check if the number of dimensions is specified
  int idim = nsd;
  if (!strcasecmp(elem->Value(),"geometry"))
    if (utl::getAttribute(elem,"dim",idim))
      nsd = idim;

  bool result = this->SIMgeneric::parse(elem);

  const TiXmlElement* child = elem->FirstChildElement();
  for (; child; child = child->NextSiblingElement())
    if (!strcasecmp(elem->Value(),"geometry"))
      result &= this->parseGeometryTag(child);
    else if (!strcasecmp(elem->Value(),"boundaryconditions"))
      result &= this->parseBCTag(child);

  if (myGen && result)
    result = myGen->createTopology(*this);

  delete myGen;
  myGen = nullptr;

  return result;
}


bool SIM1D::parse (char* keyWord, std::istream& is)
{
  char* cline = nullptr;
  if (!strncasecmp(keyWord,"REFINE",6))
  {
    int nref = atoi(keyWord+6);
    if (isRefined)
    {
      // Just read through the next lines without doing anything
      for (int i = 0; i < nref && utl::readLine(is); i++);
      return true;
    }

    IFEM::cout <<"\nNumber of patch refinements: "<< nref << std::endl;
    for (int i = 0; i < nref && (cline = utl::readLine(is)); i++)
    {
      bool uniform = !strchr(cline,'.');
      int patch = atoi(strtok(cline," "));
      if (patch == 0 || abs(patch) > (int)myModel.size())
      {
	std::cerr <<" *** SIM1D::parse: Invalid patch index "
		  << patch << std::endl;
	return false;
      }
      int ipatch = patch-1;
      if (patch < 0)
      {
	ipatch = 0;
	patch = -patch;
      }
      if (uniform)
      {
	int addu = atoi(strtok(nullptr," "));
	for (int j = ipatch; j < patch; j++)
        {
          IFEM::cout <<"\tRefining P"<< j+1 <<" "<< addu << std::endl;
          dynamic_cast<ASM1D*>(myModel[j])->uniformRefine(addu);
        }
      }
      else
      {
	RealArray xi;
	if (utl::parseKnots(xi))
	  for (int j = ipatch; j < patch; j++)
	  {
	    IFEM::cout <<"\tRefining P"<< j+1;
	    for (size_t i = 0; i < xi.size(); i++)
	      IFEM::cout <<" "<< xi[i];
	    IFEM::cout << std::endl;
	    dynamic_cast<ASM1D*>(myModel[j])->refine(xi);
	  }
      }
    }
  }

  else if (!strncasecmp(keyWord,"RAISEORDER",10))
  {
    int nref = atoi(keyWord+10);
    if (isRefined)
    {
      // Just read through the next lines without doing anything
      for (int i = 0; i < nref && utl::readLine(is); i++);
      return true;
    }

    IFEM::cout <<"\nNumber of order raise: "<< nref << std::endl;
    for (int i = 0; i < nref && (cline = utl::readLine(is)); i++)
    {
      int patch = atoi(strtok(cline," "));
      int addu  = atoi(strtok(nullptr," "));
      if (patch == 0 || abs(patch) > (int)myModel.size())
      {
	std::cerr <<" *** SIM1D::parse: Invalid patch index "
		  << patch << std::endl;
	return false;
      }
      int ipatch = patch-1;
      if (patch < 0)
      {
	ipatch = 0;
	patch = -patch;
      }
      for (int j = ipatch; j < patch; j++)
      {
        IFEM::cout <<"\tRaising order of P"<< j+1
                   <<" "<< addu << std::endl;
        dynamic_cast<ASM1D*>(myModel[j])->raiseOrder(addu);
      }
    }
  }

  else if (!strncasecmp(keyWord,"TOPOLOGY",8))
  {
    if (!this->createFEMmodel()) return false;

    int ntop = atoi(keyWord+8);
    IFEM::cout <<"\nNumber of patch connections: "<< ntop << std::endl;
    for (int i = 0; i < ntop && (cline = utl::readLine(is)); i++)
    {
      int master = atoi(strtok(cline," "));
      int mVert  = atoi(strtok(nullptr," "));
      int slave  = atoi(strtok(nullptr," "));
      int sVert  = atoi(strtok(nullptr," "));
      if (master == slave ||
	  master < 1 || master > (int)myModel.size() ||
	  slave  < 1 || slave  > (int)myModel.size())
      {
	std::cerr <<" *** SIM1D::parse: Invalid patch indices "
		  << master <<" "<< slave << std::endl;
	return false;
      }
      IFEM::cout <<"\tConnecting P"<< slave <<" V"<< sVert
                 <<" to P"<< master <<" E"<< mVert << std::endl;
      ASM1D* spch = dynamic_cast<ASM1D*>(myModel[slave-1]);
      ASM1D* mpch = dynamic_cast<ASM1D*>(myModel[master-1]);
      if (!spch->connectPatch(sVert,*mpch,mVert))
	return false;
    }
  }

  else if (!strncasecmp(keyWord,"CONSTRAINTS",11))
  {
    if (ignoreDirichlet) return true; // Ignore all boundary conditions
    if (!this->createFEMmodel()) return false;

    int ngno = 0;
    int ncon = atoi(keyWord+11);
    IFEM::cout <<"\nNumber of constraints: "<< ncon << std::endl;
    for (int i = 0; i < ncon && (cline = utl::readLine(is)); i++)
    {
      int patch = atoi(strtok(cline," "));
      int pvert = atoi(strtok(nullptr," "));
      int bcode = atoi(strtok(nullptr," "));
      double pd = (cline = strtok(nullptr," ")) ? atof(cline) : 0.0;
      if (pd == 0.0)
      {
	if (!this->addConstraint(patch,pvert,0,bcode%1000000,0,ngno))
	  return false;
      }
      else
      {
	int code = 1000000 + bcode;
	while (myScalars.find(code) != myScalars.end())
	  code += 1000000;

	if (!this->addConstraint(patch,pvert,0,bcode%1000000,code,ngno))
	  return false;

	IFEM::cout <<" ";
	cline = strtok(nullptr," ");
	myScalars[code] = const_cast<RealFunc*>(utl::parseRealFunc(cline,pd));
      }
      IFEM::cout << std::endl;
    }
  }

  else if (!strncasecmp(keyWord,"FIXPOINTS",9))
  {
    if (ignoreDirichlet) return true; // Ignore all boundary conditions
    if (!this->createFEMmodel()) return false;

    int nfix = atoi(keyWord+9);
    IFEM::cout <<"\nNumber of fixed points: "<< nfix << std::endl;
    for (int i = 0; i < nfix && (cline = utl::readLine(is)); i++)
    {
      int patch = atoi(strtok(cline," "));
      double rx = atof(strtok(nullptr," "));
      int bcode = (cline = strtok(nullptr," ")) ? atoi(cline) : 123;

      ASM1D* pch = dynamic_cast<ASM1D*>(this->getPatch(patch,true));
      if (pch)
      {
        IFEM::cout <<"\tConstraining P"<< patch
                   <<" point at "<< rx <<" with code "<< bcode << std::endl;
        pch->constrainNode(rx,bcode);
      }
    }
  }

  else
    return this->SIMgeneric::parse(keyWord,is);

  return true;
}


/*!
  \brief Local-scope convenience function for error message generation.
*/

static bool constrError (const char* lab, int idx)
{
  std::cerr <<" *** SIM1D::addConstraint: Invalid "<< lab << idx << std::endl;
  return false;
}


bool SIM1D::addConstraint (int patch, int lndx, int ldim, int dirs, int code,
                           int& ngnod, char)
{
  if (patch < 1 || patch > (int)myModel.size())
    return constrError("patch index ",patch);

  if (lndx < -10) lndx += 10; // no projection in 1D

  IFEM::cout <<"\tConstraining P"<< patch;
  if (ldim == 0)
    IFEM::cout << " V"<< abs(lndx);
  IFEM::cout <<" in direction(s) "<< dirs;
  if (code != 0) IFEM::cout <<" code = "<< code;
#if SP_DEBUG > 1
  std::cout << std::endl;
#endif

  ASM1D* pch = dynamic_cast<ASM1D*>(myModel[patch-1]);
  if (ldim != 0) // Curve constraint
    myModel[patch-1]->constrainPatch(dirs,code);
  else switch (lndx) // Vertex constraints
    {
    case  1: pch->constrainNode(0.0,dirs,code); break;
    case  2: pch->constrainNode(1.0,dirs,code); break;
    case -1:
      ngnod += pch->constrainEndLocal(0,dirs,code);
      break;
    case -2:
      ngnod += pch->constrainEndLocal(1,dirs,code);
      break;
    default:
      IFEM::cout << std::endl;
      return constrError("vertex index ",lndx);
    }

  return true;
}


ASMbase* SIM1D::readPatch (std::istream& isp, int pchInd, const CharVec& unf,
                           const char* whiteSpace) const
{
  ASMbase* pch = ASM1D::create(opt.discretization,nsd,
                               unf.empty() ? nf : unf.front());
  if (pch)
  {
    if (!pch->read(isp) || this->getLocalPatchIndex(pchInd+1) < 1)
    {
      delete pch;
      pch = nullptr;
    }
    else
    {
      if (whiteSpace)
        IFEM::cout << whiteSpace <<"Reading patch "<< pchInd+1 << std::endl;
      pch->idx = myModel.size();
    }
  }

  return pch;
}


ModelGenerator* SIM1D::getModelGenerator (const TiXmlElement* geo) const
{
  return new DefaultGeometry1D(geo);
}


Vector SIM1D::getSolution (const Vector& psol, double u,
                           int deriv, int patch) const
{
  return this->SIMgeneric::getSolution(psol,&u,deriv,patch);
}
