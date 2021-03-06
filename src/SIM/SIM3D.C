// $Id$
//==============================================================================
//!
//! \file SIM3D.C
//!
//! \date Dec 08 2009
//!
//! \author Knut Morten Okstad / SINTEF
//!
//! \brief Solution driver for 3D NURBS-based FEM analysis.
//!
//==============================================================================

#include "SIM3D.h"
#include "ModelGenerator.h"
#include "ASMs3D.h"
#include "Functions.h"
#include "Utilities.h"
#include "Vec3Oper.h"
#include "IFEM.h"
#include "tinyxml.h"
#include <fstream>


SIM3D::SIM3D (unsigned char n1, bool check)
{
  nf.push_back(n1);
  checkRHSys = check;
}


SIM3D::SIM3D (const CharVec& fields, bool check) : nf(fields)
{
  checkRHSys = check;
}


SIM3D::SIM3D (IntegrandBase* itg, unsigned char n, bool check) : SIMgeneric(itg)
{
  nf.push_back(n);
  checkRHSys = check;
}


bool SIM3D::addConnection (int master, int slave, int mIdx,
                           int sIdx, int orient, int basis,
                           bool coordCheck, int dim, int thick)
{
  if (orient < 0 || orient > 7)
  {
    std::cerr <<" *** SIM3D::addConnection: Invalid orientation "<< orient <<"."
              << std::endl;
    return false;
  }

  int lmaster = this->getLocalPatchIndex(master);
  int lslave = this->getLocalPatchIndex(slave);
  if (lmaster > 0 && lslave > 0)
  {
    if (dim < 2) return true; // ignored in serial

    IFEM::cout <<"\tConnecting P"<< slave <<" F"<< sIdx
               <<" to P"<< master <<" F"<< mIdx
               <<" orient "<< orient << std::endl;

    ASM3D* spch = dynamic_cast<ASM3D*>(myModel[lslave-1]);
    ASM3D* mpch = dynamic_cast<ASM3D*>(myModel[lmaster-1]);
    if (spch && mpch)
    {
      std::set<int> bases;
      if (basis == 0)
        for (size_t b = 1; b <= myModel[lslave-1]->getNoBasis(); b++)
          bases.insert(b);
      else
        bases = utl::getDigits(basis);

      for (int b : bases)
        if (!spch->connectPatch(sIdx,*mpch,mIdx,orient,b,coordCheck,thick))
          return false;
    }

    myInterfaces.push_back(ASM::Interface{master, slave, mIdx, sIdx, orient,
                                          dim, basis, thick});
  }
  else
    adm.dd.ghostConnections.insert(ASM::Interface{master, slave,
                                                  mIdx, sIdx, orient,
                                                  dim, basis, thick});

  return true;
}


bool SIM3D::parseGeometryTag (const TiXmlElement* elem)
{
  IFEM::cout <<"  Parsing <"<< elem->Value() <<">"<< std::endl;

  if (!strcasecmp(elem->Value(),"refine") && !isRefined)
  {
    IntVec patches;
    if (!this->parseTopologySet(elem,patches))
      return false;

    RealArray xi;
    if (!utl::parseKnots(elem,xi))
    {
      int addu = 0, addv = 0, addw = 0;
      utl::getAttribute(elem,"u",addu);
      utl::getAttribute(elem,"v",addv);
      utl::getAttribute(elem,"w",addw);
      for (int j : patches)
      {
        IFEM::cout <<"\tRefining P"<< j
                   <<" "<< addu <<" "<< addv <<" "<< addw << std::endl;
        ASM3D* pch = dynamic_cast<ASM3D*>(this->getPatch(j,true));
        if (pch)
        {
          pch->uniformRefine(0,addu);
          pch->uniformRefine(1,addv);
          pch->uniformRefine(2,addw);
        }
      }
    }
    else
    {
      // Non-uniform (graded) refinement
      int dir = 1;
      utl::getAttribute(elem,"dir",dir);
      for (int j : patches)
      {
        IFEM::cout <<"\tRefining P"<< j <<" dir="<< dir
                   <<" with grading "<< elem->FirstChild()->Value() <<":";
        for (size_t i = 0; i < xi.size(); i++)
          IFEM::cout << (i%10 || xi.size() < 11 ? " " : "\n\t") << xi[i];
        IFEM::cout << std::endl;
        ASM3D* pch = dynamic_cast<ASM3D*>(this->getPatch(j,true));
        if (pch) pch->refine(dir-1,xi);
      }
    }
  }

  else if (!strcasecmp(elem->Value(),"raiseorder") && !isRefined)
  {
    IntVec patches;
    if (!this->parseTopologySet(elem,patches))
      return false;

    int addu = 0, addv = 0, addw = 0;
    utl::getAttribute(elem,"u",addu);
    utl::getAttribute(elem,"v",addv);
    utl::getAttribute(elem,"w",addw);
    for (int j : patches)
    {
      IFEM::cout <<"\tRaising order of P"<< j
                 <<" "<< addu <<" "<< addv <<" "<< addw << std::endl;
      ASM3D* pch = dynamic_cast<ASM3D*>(this->getPatch(j,true));
      if (pch) pch->raiseOrder(addu,addv,addw);
    }
  }

  else if (!strcasecmp(elem->Value(),"topology"))
  {
    if (!this->createFEMmodel()) return false;

    const TiXmlElement* child = elem->FirstChildElement("connection");
    for (; child; child = child->NextSiblingElement())
    {
      int master = 0, slave = 0, mIdx = 0, sIdx = 0;
      int orient = 0, basis = 0, dim = 2;
      bool periodic = false;
      utl::getAttribute(child,"master",master);
      if (!utl::getAttribute(child,"midx",mIdx))
        utl::getAttribute(child,"mface",mIdx);
      utl::getAttribute(child,"slave",slave);
      if (!utl::getAttribute(child,"sidx",sIdx))
        utl::getAttribute(child,"sface",sIdx);
      utl::getAttribute(child,"orient",orient);
      utl::getAttribute(child,"basis",basis);
      utl::getAttribute(child,"periodic",periodic);
      utl::getAttribute(child,"dim",dim);

      if (master == slave ||
          master < 1 || master > nGlPatches ||
          slave  < 1 || slave  > nGlPatches)
      {
        std::cerr <<" *** SIM3D::parse: Invalid patch indices "
                  << master <<" "<< slave << std::endl;
        return false;
      }

      if (!this->addConnection(master, slave, mIdx, sIdx,
                               orient, basis, !periodic, dim))
      {
        std::cerr <<" *** SIM3D::parse: Error establishing connection."
                  << std::endl;
        return false;
      }
    }
  }

  else if (!strcasecmp(elem->Value(),"periodic"))
    return this->parsePeriodic(elem);

  else if (!strcasecmp(elem->Value(),"collapse"))
  {
    if (!this->createFEMmodel()) return false;

    int patch = 0, face = 1, edge = 0;
    utl::getAttribute(elem,"patch",patch);
    utl::getAttribute(elem,"face",face);
    utl::getAttribute(elem,"edge",edge);

    if (patch < 1 || patch > nGlPatches)
    {
      std::cerr <<" *** SIM3D::parse: Invalid patch index "
                << patch << std::endl;
      return false;
    }

    IFEM::cout <<"\tCollapsed face P"<< patch <<" F"<< face;
    if (edge > 0) IFEM::cout <<" on to edge "<< edge;
    IFEM::cout << std::endl;
    ASMs3D* pch = dynamic_cast<ASMs3D*>(this->getPatch(patch,true));
    if (pch) return pch->collapseFace(face,edge);
  }

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
        else if ((pch = ASM3D::create(opt.discretization,nf)))
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
        std::cerr <<" *** SIM3D::parseGeometryTag: Failed to create projection"
                  <<" basis, check patch file specification."<< std::endl;
        return false;
      }
  }

  return true;
}


bool SIM3D::parseBCTag (const TiXmlElement* elem)
{
  if (!strcasecmp(elem->Value(),"fixpoint") && !ignoreDirichlet)
  {
    if (!this->createFEMmodel()) return false;

    int patch = 0, code = 123;
    double rx = 0.0, ry = 0.0, rz = 0.0;
    utl::getAttribute(elem,"patch",patch);
    utl::getAttribute(elem,"code",code);
    utl::getAttribute(elem,"rx",rx);
    utl::getAttribute(elem,"ry",ry);
    utl::getAttribute(elem,"rz",rz);
    int pid = this->getLocalPatchIndex(patch);
    if (pid < 1) return pid == 0;

    ASM3D* pch = dynamic_cast<ASM3D*>(myModel[pid-1]);
    if (!pch) return false;

    IFEM::cout <<"\tConstraining P"<< patch
               <<" point at "<< rx <<" "<< ry <<" "<< rz
               <<" with code "<< code << std::endl;
    pch->constrainNode(rx,ry,rz,code);
  }

  return true;
}


bool SIM3D::parse (const TiXmlElement* elem)
{
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


bool SIM3D::parse (char* keyWord, std::istream& is)
{
  char* cline = nullptr;
  if (!strncasecmp(keyWord,"REFINE",6))
  {
    int nref = atoi(keyWord+6);
    if (isRefined) // just read through the next lines without doing anything
      for (int i = 0; i < nref && utl::readLine(is); i++);
    else
    {
      ASM3D* pch = nullptr;
      IFEM::cout <<"\nNumber of patch refinements: "<< nref << std::endl;
      for (int i = 0; i < nref && (cline = utl::readLine(is)); i++)
      {
        bool uniform = !strchr(cline,'.');
        int patch = atoi(strtok(cline," "));
        if (patch == 0 || abs(patch) > (int)myModel.size())
        {
          std::cerr <<" *** SIM3D::parse: Invalid patch index "
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
          int addv = atoi(strtok(nullptr," "));
          int addw = atoi(strtok(nullptr," "));
          for (int j = ipatch; j < patch; j++)
            if ((pch = dynamic_cast<ASM3D*>(myModel[j])))
            {
              IFEM::cout <<"\tRefining P"<< j+1
                         <<" "<< addu <<" "<< addv <<" "<< addw << std::endl;
              pch->uniformRefine(0,addu);
              pch->uniformRefine(1,addv);
              pch->uniformRefine(2,addw);
            }
        }
        else
        {
          RealArray xi;
          int dir = atoi(strtok(nullptr," "));
          if (utl::parseKnots(xi))
            for (int j = ipatch; j < patch; j++)
              if ((pch = dynamic_cast<ASM3D*>(myModel[j])))
              {
                IFEM::cout <<"\tRefining P"<< j+1 <<" dir="<< dir;
                for (double u : xi) IFEM::cout <<" "<< u;
                IFEM::cout << std::endl;
                pch->refine(dir-1,xi);
              }
        }
      }
    }
  }

  else if (!strncasecmp(keyWord,"RAISEORDER",10))
  {
    int nref = atoi(keyWord+10);
    if (isRefined) // just read through the next lines without doing anything
      for (int i = 0; i < nref && utl::readLine(is); i++);
    else
    {
      ASM3D* pch = nullptr;
      IFEM::cout <<"\nNumber of order raise: "<< nref << std::endl;
      for (int i = 0; i < nref && (cline = utl::readLine(is)); i++)
      {
        int patch = atoi(strtok(cline," "));
        int addu  = atoi(strtok(nullptr," "));
        int addv  = atoi(strtok(nullptr," "));
        int addw  = atoi(strtok(nullptr," "));
        if (patch == 0 || abs(patch) > (int)myModel.size())
        {
          std::cerr <<" *** SIM3D::parse: Invalid patch index "
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
          if ((pch = dynamic_cast<ASM3D*>(myModel[j])))
          {
            IFEM::cout <<"\tRaising order of P"<< j+1
                       <<" "<< addu <<" "<< addv <<" "<< addw << std::endl;
            pch->raiseOrder(addu,addv,addw);
          }
      }
    }
  }

  else if (!strncasecmp(keyWord,"TOPOLOGYFILE",12))
  {
    if (!this->createFEMmodel()) return false;

    size_t i = 12; while (i < strlen(keyWord) && isspace(keyWord[i])) i++;
    std::ifstream ist(keyWord+i);
    if (ist)
      IFEM::cout <<"\nReading data file "<< keyWord+i << std::endl;
    else
    {
      std::cerr <<" *** SIM3D::parse: Failure opening input file "
                << std::string(keyWord+i) << std::endl;
      return false;
    }

    while ((cline = utl::readLine(ist)))
    {
      int master = atoi(strtok(cline," "))+1;
      int mFace  = atoi(strtok(nullptr," "))+1;
      int slave  = atoi(strtok(nullptr," "))+1;
      int sFace  = atoi(strtok(nullptr," "))+1;
      int swapd  = atoi(strtok(nullptr," "));
      int rev_u  = atoi(strtok(nullptr," "));
      int rev_v  = atoi(strtok(nullptr," "));
      int orient = 4*swapd+2*rev_u+rev_v;
      if (master == slave ||
          master < 1 || master > (int)myModel.size() ||
          slave  < 1 || slave  > (int)myModel.size())
      {
        std::cerr <<" *** SIM3D::parse: Invalid patch indices "
                  << master <<" "<< slave << std::endl;
        return false;
      }
      IFEM::cout <<"\tConnecting P"<< slave <<" F"<< sFace
                 <<" to P"<< master <<" F"<< mFace
                 <<" orient "<< orient << std::endl;
      ASMs3D* spch = static_cast<ASMs3D*>(myModel[slave-1]);
      ASMs3D* mpch = static_cast<ASMs3D*>(myModel[master-1]);
      if (!spch->connectPatch(sFace,*mpch,mFace,orient))
        return false;
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
      int mFace  = atoi(strtok(nullptr," "));
      int slave  = atoi(strtok(nullptr," "));
      int sFace  = atoi(strtok(nullptr," "));
      int orient = (cline = strtok(nullptr," ")) ? atoi(cline) : 0;
      if (master == slave ||
          master < 1 || master > (int)myModel.size() ||
          slave  < 1 || slave  > (int)myModel.size())
      {
        std::cerr <<" *** SIM3D::parse: Invalid patch indices "
                  << master <<" "<< slave << std::endl;
        return false;
      }
      IFEM::cout <<"\tConnecting P"<< slave <<" F"<< sFace
                 <<" to P"<< master <<" F"<< mFace
                 <<" orient "<< orient << std::endl;
      ASMs3D* spch = static_cast<ASMs3D*>(myModel[slave-1]);
      ASMs3D* mpch = static_cast<ASMs3D*>(myModel[master-1]);
      if (!spch->connectPatch(sFace,*mpch,mFace,orient))
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
      int pface = atoi(strtok(nullptr," "));
      int bcode = atoi(strtok(nullptr," "));
      double pd = (cline = strtok(nullptr," ")) ? atof(cline) : 0.0;

      patch = this->getLocalPatchIndex(patch);
      if (patch < 1) continue;

      int ldim = pface < 0 ? 0 : 2;
      if (pface < 0) pface = -pface;

      if (pface > 10)
      {
        if (!this->addConstraint(patch,pface%10,pface/10,pd,bcode))
          return false;
      }
      else if (pd == 0.0)
      {
        if (!this->addConstraint(patch,pface,ldim,bcode%1000000,0,ngno))
          return false;
      }
      else
      {
        int code = 1000000 + bcode;
        while (myScalars.find(code) != myScalars.end())
          code += 1000000;

        if (!this->addConstraint(patch,pface,ldim,bcode%1000000,-code,ngno))
          return false;

        IFEM::cout <<" ";
        cline = strtok(nullptr," ");
        myScalars[code] = const_cast<RealFunc*>(utl::parseRealFunc(cline,pd));
      }
      if (pface < 10) IFEM::cout << std::endl;
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
      double ry = atof(strtok(nullptr," "));
      double rz = atof(strtok(nullptr," "));
      int bcode = (cline = strtok(nullptr," ")) ? atoi(cline) : 123;

      ASM3D* pch = dynamic_cast<ASM3D*>(this->getPatch(patch,true));
      if (pch)
      {
        IFEM::cout <<"\tConstraining P"<< patch
                   <<" point at "<< rx <<" "<< ry <<" "<< rz
                   <<" with code "<< bcode << std::endl;
        pch->constrainNode(rx,ry,rz,bcode);
      }
    }
  }

  else
    return this->SIMgeneric::parse(keyWord,is);

  return true;
}


bool SIM3D::addConstraint (int patch, int lndx, int ldim, int dirs, int code,
                           int& ngnod, char basis)
{
  // Lambda function for error message generation
  auto&& error = [](const char* message, int idx)
  {
    std::cerr <<" *** SIM3D::addConstraint: Invalid "
              << message <<" ("<< idx <<")."<< std::endl;
    return false;
  };

  if (patch < 1 || patch > (int)myModel.size())
    return error("patch index",patch);

  int aldim = abs(ldim);
  bool open = ldim < 0; // open means without face edges or edge ends
  bool project = lndx < -10;
  if (project) lndx += 10;
  if (lndx < 0 && aldim > 3) aldim = 2; // local tangent direction is indicated

  IFEM::cout <<"\tConstraining P"<< patch;
  if (aldim < 3)
    IFEM::cout << (ldim == 0 ? " V" : (aldim == 1 ? " E" : " F")) << lndx;
  IFEM::cout <<" in direction(s) "<< dirs;
  if (lndx < 0) IFEM::cout << (project ? " (local projected)" : " (local)");
  if (code != 0) IFEM::cout <<" code = "<< abs(code);
  if (basis > 1) IFEM::cout <<" basis = "<< (int)basis;
#if SP_DEBUG > 1
  std::cout << std::endl;
#endif

  // Must dynamic cast here, since ASM3D is not derived from ASMbase
  ASM3D* pch = dynamic_cast<ASM3D*>(myModel[patch-1]);
  if (!pch) return error("3D patch",patch);

  switch (aldim)
    {
    case 0: // Vertex constraints
      switch (lndx)
        {
        case 1: pch->constrainCorner(-1,-1,-1,dirs,abs(code),basis); break;
        case 2: pch->constrainCorner( 1,-1,-1,dirs,abs(code),basis); break;
        case 3: pch->constrainCorner(-1, 1,-1,dirs,abs(code),basis); break;
        case 4: pch->constrainCorner( 1, 1,-1,dirs,abs(code),basis); break;
        case 5: pch->constrainCorner(-1,-1, 1,dirs,abs(code),basis); break;
        case 6: pch->constrainCorner( 1,-1, 1,dirs,abs(code),basis); break;
        case 7: pch->constrainCorner(-1, 1, 1,dirs,abs(code),basis); break;
        case 8: pch->constrainCorner( 1, 1, 1,dirs,abs(code),basis); break;
        default:
          IFEM::cout << std::endl;
          return error("vertex index",lndx);
        }
      break;

    case 1: // Edge constraints
      if (lndx > 0 && lndx <= 12)
        pch->constrainEdge(lndx,open,dirs,code,basis);
      else
      {
        IFEM::cout << std::endl;
        return error("edge index",lndx);
      }
      break;

    case 2: // Face constraints
      switch (lndx)
        {
        case  1: pch->constrainFace(-1,open,dirs,code,basis); break;
        case  2: pch->constrainFace( 1,open,dirs,code,basis); break;
        case  3: pch->constrainFace(-2,open,dirs,code,basis); break;
        case  4: pch->constrainFace( 2,open,dirs,code,basis); break;
        case  5: pch->constrainFace(-3,open,dirs,code,basis); break;
        case  6: pch->constrainFace( 3,open,dirs,code,basis); break;
        case -1:
          ngnod += pch->constrainFaceLocal(-1,open,dirs,code,project,ldim);
          break;
        case -2:
          ngnod += pch->constrainFaceLocal( 1,open,dirs,code,project,ldim);
          break;
        case -3:
          ngnod += pch->constrainFaceLocal(-2,open,dirs,code,project,ldim);
          break;
        case -4:
          ngnod += pch->constrainFaceLocal( 2,open,dirs,code,project,ldim);
          break;
        case -5:
          ngnod += pch->constrainFaceLocal(-3,open,dirs,code,project,ldim);
          break;
        case -6:
          ngnod += pch->constrainFaceLocal( 3,open,dirs,code,project,ldim);
          break;
        default:
          IFEM::cout << std::endl;
          return error("face index",lndx);
        }
      break;

    case 3: // Volume constraints
      myModel[patch-1]->constrainPatch(dirs,code);
      break;

    case 4: // Explicit nodal constraints
      myModel[patch-1]->constrainNodes(myModel[patch-1]->getNodeSet(lndx),
                                       dirs,code);
      break;

    default:
      IFEM::cout << std::endl;
      return error("local dimension switch",ldim);
    }

  return true;
}


bool SIM3D::addConstraint (int patch, int lndx, int line, double xi,
                           int dirs, char basis)
{
  // Lambda function for error message generation
  auto&& error = [](const char* message, int idx)
  {
    std::cerr <<" *** SIM3D::addConstraint: Invalid "
              << message <<" ("<< idx <<")."<< std::endl;
    return false;
  };

  if (patch < 1 || patch > (int)myModel.size())
    return error("patch index",patch);

  IFEM::cout <<"\tConstraining P"<< patch
             <<" F"<< lndx <<" L"<< line <<" at xi="<< xi
             <<" in direction(s) "<< dirs
             <<" basis = " << (int)basis << std::endl;

  // Must dynamic cast here, since ASM3D is not derived from ASMbase
  ASM3D* pch = dynamic_cast<ASM3D*>(myModel[patch-1]);
  if (!pch) return error("3D patch",patch);

  switch (line)
    {
    case 1: // Face line constraints in local I-direction
      switch (lndx)
        {
        case 1: pch->constrainLine(-1,2,xi,dirs,0,basis); break;
        case 2: pch->constrainLine( 1,2,xi,dirs,0,basis); break;
        case 3: pch->constrainLine(-2,3,xi,dirs,0,basis); break;
        case 4: pch->constrainLine( 2,3,xi,dirs,0,basis); break;
        case 5: pch->constrainLine(-3,1,xi,dirs,0,basis); break;
        case 6: pch->constrainLine( 3,1,xi,dirs,0,basis); break;
        default: error("face index",lndx);
        }
      break;

    case 2: // Face line constraints in local J-direction
      switch (lndx)
        {
        case 1: pch->constrainLine(-1,3,xi,dirs,0,basis); break;
        case 2: pch->constrainLine( 1,3,xi,dirs,0,basis); break;
        case 3: pch->constrainLine(-2,1,xi,dirs,0,basis); break;
        case 4: pch->constrainLine( 2,1,xi,dirs,0,basis); break;
        case 5: pch->constrainLine(-3,2,xi,dirs,0,basis); break;
        case 6: pch->constrainLine( 3,2,xi,dirs,0,basis); break;
        default: return error("face index",lndx);
        }
      break;

    default:
      return error("face line index",line);
    }

  return true;
}


ASMbase* SIM3D::readPatch (std::istream& isp, int pchInd, const CharVec& unf,
                           const char* whiteSpace) const
{
  const CharVec& uunf = unf.empty() ? nf : unf;
  bool isMixed = uunf.size() > 1 && uunf[1] > 0;
  ASMbase* pch = ASM3D::create(opt.discretization,uunf,isMixed);
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
      if (checkRHSys && dynamic_cast<ASM3D*>(pch)->checkRightHandSystem())
        IFEM::cout <<"\tSwapped."<< std::endl;
      pch->idx = myModel.size();
    }
  }

  return pch;
}


void SIM3D::readNodes (std::istream& isn)
{
  while (isn.good())
  {
    int patch = 0;
    isn >> patch;
    int pid = this->getLocalPatchIndex(patch+1);
    if (pid < 0) return;

    if (!this->readNodes(isn,pid-1))
    {
      std::cerr <<" *** SIM3D::readNodes: Failed to assign node numbers"
                <<" for patch "<< patch+1 << std::endl;
      return;
    }
  }
}


bool SIM3D::readNodes (std::istream& isn, int pchInd, int basis, bool oneBased)
{
  int i;
  ASMs3D::BlockNodes n;

  for (i = 0; i <  8 && isn.good(); i++)
    isn >> n.ibnod[i];
  for (i = 0; i < 12 && isn.good(); i++)
    isn >> n.edges[i].icnod >> n.edges[i].incr;
  for (i = 0; i <  6 && isn.good(); i++)
    isn >> n.faces[i].isnod >> n.faces[i].incrI >> n.faces[i].incrJ;
  isn >> n.iinod;

  if (!isn.good() || pchInd < 0) return true;

  if (!oneBased)
  {
    // We always require the node numbers to be 1-based
    for (i = 0; i <  8; i++) ++n.ibnod[i];
    for (i = 0; i < 12; i++) ++n.edges[i].icnod;
    for (i = 0; i <  6; i++) ++n.faces[i].isnod;
    ++n.iinod;
  }

  return static_cast<ASMs3D*>(myModel[pchInd])->assignNodeNumbers(n,basis);
}


void SIM3D::clonePatches (const PatchVec& patches,
                          const std::map<int,int>& glb2locN)
{
  for (ASMbase* patch : patches)
  {
    ASM3D* pch3D = dynamic_cast<ASM3D*>(patch);
    if (pch3D) myModel.push_back(pch3D->clone(nf));
  }

  g2l = &glb2locN;

  if (nGlPatches == 0)
    nGlPatches = myModel.size();
}


ModelGenerator* SIM3D::getModelGenerator (const TiXmlElement* geo) const
{
  return new DefaultGeometry3D(geo);
}


Vector SIM3D::getSolution (const Vector& psol, double u, double v, double w,
                           int deriv, int patch) const
{
  double par[3] = { u, v, w };
  return this->SIMgeneric::getSolution(psol,par,deriv,patch);
}
