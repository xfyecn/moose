/****************************************************************/
/*               DO NOT MODIFY THIS HEADER                      */
/* MOOSE - Multiphysics Object Oriented Simulation Environment  */
/*                                                              */
/*           (c) 2010 Battelle Energy Alliance, LLC             */
/*                   ALL RIGHTS RESERVED                        */
/*                                                              */
/*          Prepared by Battelle Energy Alliance, LLC           */
/*            Under Contract No. DE-AC07-05ID14517              */
/*            With the U. S. Department of Energy               */
/*                                                              */
/*            See COPYRIGHT for full restrictions               */
/****************************************************************/

#ifndef INTEGRATEDBCBASE_H
#define INTEGRATEDBCBASE_H

#include "BoundaryCondition.h"
#include "RandomInterface.h"
#include "CoupleableMooseVariableDependencyIntermediateInterface.h"
#include "MaterialPropertyInterface.h"

// Forward declarations
class IntegratedBCBase;
template <typename>
class MooseVariableFE;
typedef MooseVariableFE<Real> MooseVariable;
typedef MooseVariableFE<VectorValue<Real>> VectorMooseVariable;

template <>
InputParameters validParams<IntegratedBCBase>();

/**
 * Base class for deriving any boundary condition of a integrated type
 */
class IntegratedBCBase : public BoundaryCondition,
                         public RandomInterface,
                         public CoupleableMooseVariableDependencyIntermediateInterface,
                         public MaterialPropertyInterface
{
public:
  IntegratedBCBase(const InputParameters & parameters);

  virtual ~IntegratedBCBase();

  virtual void computeResidual() = 0;
  virtual void computeJacobian() = 0;
  /**
   * Computes d-ivar-residual / d-jvar...
   */
  virtual void computeJacobianBlock(MooseVariableFEBase & jvar) = 0;
  /**
   * Computes jacobian block with respect to a scalar variable
   * @param jvar The number of the scalar variable
   */
  virtual void computeJacobianBlockScalar(unsigned int jvar) = 0;

  /**
   * Compute this IntegratedBCBase's contribution to the diagonal Jacobian entries
   * corresponding to nonlocal dofs of the variable
   */
  virtual void computeNonlocalJacobian() {}

  /**
   * Computes d-residual / d-jvar... corresponding to nonlocal dofs of the jvar
   * and stores the result in nonlocal ke
   */
  virtual void computeNonlocalOffDiagJacobian(unsigned int /* jvar */) {}

protected:
  /// current element
  const Elem *& _current_elem;
  /// Volume of the current element
  const Real & _current_elem_volume;
  /// current side of the current element
  unsigned int & _current_side;
  /// current side element
  const Elem *& _current_side_elem;
  /// Volume of the current side
  const Real & _current_side_volume;

  /// quadrature point index
  unsigned int _qp;
  /// active quadrature rule
  QBase *& _qrule;
  /// active quadrature points
  const MooseArray<Point> & _q_point;
  /// transformed Jacobian weights
  const MooseArray<Real> & _JxW;
  /// coordinate transformation
  const MooseArray<Real> & _coord;
  /// i-th, j-th index for enumerating test and shape functions
  unsigned int _i, _j;

  /// The aux variables to save the residual contributions to
  bool _has_save_in;
  std::vector<MooseVariableFEBase *> _save_in;
  std::vector<AuxVariableName> _save_in_strings;

  /// The aux variables to save the diagonal Jacobian contributions to
  bool _has_diag_save_in;
  std::vector<MooseVariableFEBase *> _diag_save_in;
  std::vector<AuxVariableName> _diag_save_in_strings;

  virtual Real computeQpResidual() = 0;
  virtual Real computeQpJacobian() { return 0; }
  /**
   * This is the virtual that derived classes should override for computing an off-diagonal jacobian
   * component.
   */
  virtual Real computeQpOffDiagJacobian(unsigned int /*jvar*/) { return 0; }
};

#endif /* INTEGRATEDBCBASE_H */
