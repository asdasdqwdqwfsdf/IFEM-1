// $Id$
//==============================================================================
//!
//! \file ExprFunctions.C
//!
//! \date Dec 1 2011
//!
//! \author Arne Morten Kvarving / SINTEF
//!
//! \brief Expression function implementations.
//!
//==============================================================================

#include "ExprFunctions.h"
#include "Vec3.h"
#include "Tensor.h"
#include "expreval.h"


/*!
  Prints an error message with the exception occured to std::cerr.
*/

static void ExprException (const ExprEval::Exception& exc, const char* task,
                           const char* function = nullptr)
{
  std::cerr <<"\n *** Error "<< task <<" function";
  if (function)
    std::cerr <<" \""<< function <<"\"";
  if (!exc.GetValue().empty())
    std::cerr <<", "<< exc.GetValue();

  switch (exc.GetType()) {
  case ExprEval::Exception::Type_NotFoundException:
    std::cerr <<": Not found";
    break;
  case ExprEval::Exception::Type_AlreadyExistsException:
    std::cerr <<": Already exists";
    break;
  case ExprEval::Exception::Type_NullPointerException:
    std::cerr <<": Null pointer";
    break;
  case ExprEval::Exception::Type_MathException:
    std::cerr <<": Math exception, "<< exc.GetError();
    break;
  case ExprEval::Exception::Type_DivideByZeroException:
    std::cerr <<": Division by zero";
    break;
  case ExprEval::Exception::Type_NoValueListException:
    std::cerr <<": No value list";
    break;
  case ExprEval::Exception::Type_NoFunctionListException:
    std::cerr <<": No function list";
    break;
  case ExprEval::Exception::Type_AbortException:
    std::cerr <<": Abort";
    break;
  case ExprEval::Exception::Type_EmptyExpressionException:
    std::cerr <<": Empty expression";
    break;
  case ExprEval::Exception::Type_UnknownTokenException:
    std::cerr <<": Unknown token";
    break;
  case ExprEval::Exception::Type_InvalidArgumentCountException:
    std::cerr <<": Invalid argument count";
    break;
  case ExprEval::Exception::Type_ConstantAssignException:
    std::cerr <<": Constant assign";
    break;
  case ExprEval::Exception::Type_ConstantReferenceException:
    std::cerr <<": Constant reference";
    break;
  case ExprEval::Exception::Type_SyntaxException:
    std::cerr <<": Syntax error";
    break;
  case ExprEval::Exception::Type_UnmatchedParenthesisException:
    std::cerr <<": Unmatched parenthesis";
    break;
  default:
    std::cerr <<": Unknown exception";
  }
  std::cerr << std::endl;
  EvalFunc::numError++;
}


int EvalFunc::numError = 0;


EvalFunc::EvalFunc (const char* function, const char* x, Real eps)
  : gradient(nullptr), dx(eps)
{
  try {
#ifdef USE_OPENMP
    size_t nalloc = omp_get_max_threads();
#else
    size_t nalloc = 1;
#endif
    expr.resize(nalloc);
    f.resize(nalloc);
    v.resize(nalloc);
    expr.resize(nalloc);
    arg.resize(nalloc);
    for (size_t i = 0; i < nalloc; ++i) {
      expr[i] = new ExprEval::Expression;
      f[i] = new ExprEval::FunctionList;
      v[i] = new ExprEval::ValueList;
      f[i]->AddDefaultFunctions();
      v[i]->AddDefaultValues();
      v[i]->Add(x,0.0,false);
      expr[i]->SetFunctionList(f[i]);
      expr[i]->SetValueList(v[i]);
      expr[i]->Parse(function);
      arg[i] = v[i]->GetAddress(x);
    }
  }
  catch (ExprEval::Exception& e) {
    this->cleanup();
    ExprException(e,"parsing",function);
  }
}


EvalFunc::~EvalFunc ()
{
  this->cleanup();
}


void EvalFunc::cleanup ()
{
  for (ExprEval::Expression* it : expr)
    delete it;
  for (ExprEval::FunctionList* it : f)
    delete it;
  for (ExprEval::ValueList* it : v)
    delete it;
  delete gradient;
  expr.clear();
  f.clear();
  v.clear();
  arg.clear();
}


void EvalFunc::derivative (const std::string& function, const char* x)
{
  if (!gradient)
    gradient = new EvalFunc(function.c_str(),x);
}


Real EvalFunc::evaluate (const Real& x) const
{
  Real result = Real(0);
  size_t i = 0;
#ifdef USE_OPENMP
  i = omp_get_thread_num();
#endif
  if (i >= arg.size())
    return result;
  try {
    *arg[i] = x;
    result = expr[i]->Evaluate();
  }
  catch (ExprEval::Exception& e) {
    ExprException(e,"evaluating expression");
  }

  return result;
}


Real EvalFunc::deriv (Real x) const
{
  if (gradient)
    return gradient->evaluate(x);

  // Evaluate derivative using central difference
  return (this->evaluate(x+0.5*dx) - this->evaluate(x-0.5*dx)) / dx;
}


EvalFunction::EvalFunction (const char* function) : gradient{}, dgradient{}
{
  try {
#ifdef USE_OPENMP
    size_t nalloc = omp_get_max_threads();
#else
    size_t nalloc = 1;
#endif
    expr.resize(nalloc);
    f.resize(nalloc);
    v.resize(nalloc);
    expr.resize(nalloc);
    arg.resize(nalloc);
    for (size_t i = 0; i < nalloc; ++i) {
      expr[i] = new ExprEval::Expression;
      f[i] = new ExprEval::FunctionList;
      v[i] = new ExprEval::ValueList;
      f[i]->AddDefaultFunctions();
      v[i]->AddDefaultValues();
      v[i]->Add("x",0.0,false);
      v[i]->Add("y",0.0,false);
      v[i]->Add("z",0.0,false);
      v[i]->Add("t",0.0,false);
      expr[i]->SetFunctionList(f[i]);
      expr[i]->SetValueList(v[i]);
      expr[i]->Parse(function);
      arg[i].x = v[i]->GetAddress("x");
      arg[i].y = v[i]->GetAddress("y");
      arg[i].z = v[i]->GetAddress("z");
      arg[i].t = v[i]->GetAddress("t");
    }
  }
  catch (ExprEval::Exception& e) {
    this->cleanup();
    ExprException(e,"parsing",function);
  }

  // Checking if the expression is time-independent
  // Note, this will also catch things like tan(x), but...
  std::string expr(function);
  IAmConstant = expr.find_first_of('t') > expr.size();
}


EvalFunction::~EvalFunction ()
{
  this->cleanup();
}


void EvalFunction::cleanup ()
{
  for (ExprEval::Expression* it : expr)
    delete it;
  for (ExprEval::FunctionList* it : f)
    delete it;
  for (ExprEval::ValueList* it : v)
    delete it;
  for (EvalFunction* it : gradient)
    delete it;
  for (EvalFunction* it : dgradient)
    delete it;
  expr.clear();
  f.clear();
  v.clear();
  arg.clear();
  gradient.fill(nullptr);
  dgradient.fill(nullptr);
}


/*!
  \brief Static helper converting an index pair into a single index.
  \details Assuming Voigt notation ordering; 11, 22, 33, 12, 23, 13.
*/

static int voigtIdx (int d1, int d2)
{
  if (d1 > d2)
    std::swap(d1,d2); // Assuming symmetry

  if (d1 < 1 || d2 > 3)
    return -1; // Out-of-range
  else if (d2-d1 == 0) // diagonal term, 11, 22 and 33
    return d1-1;
  else if (d2-d1 == 1) // off-diagonal term, 12 and 23
    return d2+1;
  else // off-diagonal term, 13
    return 5;
}


void EvalFunction::addDerivative (const std::string& function,
                                  const std::string& variables,
                                  int d1, int d2)
{
  if (d1 > 0 && d1 <= 3 && d2 < 1) // A first derivative is specified
  {
    if (!gradient[--d1])
      gradient[d1] = new EvalFunction((variables+function).c_str());
  }
  else if ((d1 = voigtIdx(d1,d2)) >= 0) // A second derivative is specified
  {
    if (!dgradient[d1])
      dgradient[d1] = new EvalFunction((variables+function).c_str());
  }
}


Real EvalFunction::evaluate (const Vec3& X) const
{
  const Vec4* Xt = dynamic_cast<const Vec4*>(&X);
  Real result = Real(0);
  try {
    size_t i = 0;
#ifdef USE_OPENMP
    i = omp_get_thread_num();
#endif
    if (i >= arg.size())
      return result;

    *arg[i].x = X.x;
    *arg[i].y = X.y;
    *arg[i].z = X.z;
    *arg[i].t = Xt ? Xt->t : Real(0);
    result = expr[i]->Evaluate();
  }
  catch (ExprEval::Exception& e) {
    ExprException(e,"evaluating expression");
  }

  return result;
}


Real EvalFunction::deriv (const Vec3& X, int dir) const
{
  if (dir < 1 || dir > 3 || !gradient[--dir])
    return Real(0);

  return gradient[dir]->evaluate(X);
}


Real EvalFunction::dderiv (const Vec3& X, int d1, int d2) const
{
  if (d1 > d2)
    std::swap(d1,d2); // Assuming symmetry

  if (d1 < 1 || d2 > 3)
    return Real(0);

  // Assuming Voigt notation ordering; 11, 22, 33, 12, 23, 13
  if (d2-d1 == 0) // diagoal term, 11, 22 and 33
    --d1;
  else if (d2-d1 == 1) // off-diagonal term, 12 and 23
    d1 = d2+1;
  else // off-diagonal term, 13
    d1 = 5;

  return dgradient[d1] ? dgradient[d1]->evaluate(X) : Real(0);
}


/*!
  \brief Static helper that splits a function expression into components.
*/

static std::vector<std::string> splitComps (const std::string& functions,
                                            const std::string& variables)
{
  std::vector<std::string> comps;
  size_t pos1 = functions.find("|");
  size_t pos2 = 0;
  while (pos2 < functions.size())
  {
    std::string func(variables);
    if (!func.empty() && func[func.size()-1] != ';')
      func += ';';
    if (pos1 == std::string::npos)
      func += functions.substr(pos2);
    else
      func += functions.substr(pos2,pos1-pos2);
    comps.push_back(func);
    pos2 = pos1 > 0 && pos1 < std::string::npos ? pos1+1 : pos1;
    pos1 = functions.find("|",pos1+1);
  }

  return comps;
}


EvalFunctions::EvalFunctions (const std::string& functions,
                              const std::string& variables)
{
  std::vector<std::string> components = splitComps(functions,variables);
  for (const std::string& comp : components)
    p.push_back(new EvalFunction(comp.c_str()));
}


EvalFunctions::~EvalFunctions ()
{
  for (EvalFunction* it : p)
    delete it;
}


void EvalFunctions::addDerivative (const std::string& functions,
                                   const std::string& variables, int d1, int d2)
{
  std::vector<std::string> components = splitComps(functions,variables);
  for (size_t i = 0; i < p.size() && i < components.size(); i++)
    p[i]->addDerivative(components[i],variables,d1,d2);
}


template<>
Vec3 VecFuncExpr::evaluate (const Vec3& X) const
{
  Vec3 result;

  for (size_t i = 0; i < 3 && i < nsd; ++i)
    result[i] = (*p[i])(X);

  return result;
}


template<>
Vec3 VecFuncExpr::deriv (const Vec3& X, int dir) const
{
  Vec3 result;

  for (size_t i = 0; i < 3 && i < nsd; ++i)
    result[i] = p[i]->deriv(X,dir);

  return result;
}


template<>
Vec3 VecFuncExpr::dderiv (const Vec3& X, int d1, int d2) const
{
  Vec3 result;

  for (size_t i = 0; i < 3 && i < nsd; ++i)
    result[i] = p[i]->dderiv(X,d1,d2);

  return result;
}


template<>
void TensorFuncExpr::setNoDims ()
{
  if (p.size() > 8)
    nsd = 3;
  else if (p.size() > 3)
    nsd = 2;
  else if (p.size() > 0)
    nsd = 1;

  ncmp = nsd*nsd;
}


template<>
Tensor TensorFuncExpr::evaluate (const Vec3& X) const
{
  Tensor sigma(nsd);

  size_t i, j, k = 0;
  for (i = 1; i <= nsd; ++i)
    for (j = 1; j <= nsd; ++j)
      sigma(i,j) = (*p[k++])(X);

  return sigma;
}


template<>
Tensor TensorFuncExpr::deriv (const Vec3& X, int dir) const
{
  Tensor sigma(nsd);

  size_t i, j, k = 0;
  for (i = 1; i <= nsd; ++i)
    for (j = 1; j <= nsd; ++j)
      sigma(i,j) = p[k++]->deriv(X,dir);

  return sigma;
}


template<>
Tensor TensorFuncExpr::dderiv (const Vec3& X, int d1, int d2) const
{
  Tensor sigma(nsd);

  size_t i, j, k = 0;
  for (i = 1; i <= nsd; ++i)
    for (j = 1; j <= nsd; ++j)
      sigma(i,j) = p[k++]->dderiv(X,d1,d2);

  return sigma;
}


template<>
void STensorFuncExpr::setNoDims ()
{
  if (p.size() > 5)
    nsd = 3;
  else if (p.size() > 2)
    nsd = 2;
  else if (p.size() > 0)
    nsd = 1;

  ncmp = p.size() == 4 ? 4 : (nsd+1)*nsd/2;
}


template<>
SymmTensor STensorFuncExpr::evaluate (const Vec3& X) const
{
  SymmTensor sigma(nsd,p.size()==4);

  std::vector<Real>& svec = sigma;
  for (size_t i = 0; i < svec.size(); i++)
    svec[i] = (*p[i])(X);

  return sigma;
}


template<>
SymmTensor STensorFuncExpr::deriv (const Vec3& X, int dir) const
{
  SymmTensor sigma(nsd,p.size()==4);

  std::vector<Real>& svec = sigma;
  for (size_t i = 0; i < svec.size(); i++)
    svec[i] = p[i]->deriv(X,dir);

  return sigma;
}


template<>
SymmTensor STensorFuncExpr::dderiv (const Vec3& X, int d1, int d2) const
{
  SymmTensor sigma(nsd,p.size()==4);

  std::vector<Real>& svec = sigma;
  for (size_t i = 0; i < svec.size(); i++)
    svec[i] = p[i]->dderiv(X,d1,d2);

  return sigma;
}
