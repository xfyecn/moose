//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

// MOOSE includes
#include "EqualValueEmbeddedConstraint.h"
#include "FEProblem.h"
#include "DisplacedProblem.h"
#include "AuxiliarySystem.h"
#include "SystemBase.h"
#include "Assembly.h"
#include "MooseMesh.h"
#include "Executioner.h"
#include "AddVariableAction.h"

#include "libmesh/string_to_enum.h"
#include "libmesh/sparse_matrix.h"

registerMooseObject("MooseApp", EqualValueEmbeddedConstraint);

template <>
InputParameters
validParams<EqualValueEmbeddedConstraint>()
{
  MooseEnum orders(AddVariableAction::getNonlinearVariableOrders());
  InputParameters params = validParams<NodeElemConstraint>();
  params.addClassDescription("This is a constraint enforcing overlapping portions of two blocks to "
                             "have the same variable value");
  params.set<bool>("use_displaced_mesh") = false;
  MooseEnum formulation("kinematic penalty", "kinematic");
  params.addParam<MooseEnum>(
      "formulation", formulation, "Formulation used to enforce the constraint");
  params.addRequiredParam<Real>(
      "penalty",
      "Penalty parameter used in constraint enforcement for kinematic and penalty formulations.");

  return params;
}

EqualValueEmbeddedConstraint::EqualValueEmbeddedConstraint(const InputParameters & parameters)
  : NodeElemConstraint(parameters),
    _displaced_problem(parameters.get<FEProblemBase *>("_fe_problem_base")->getDisplacedProblem()),
    _fe_problem(*parameters.get<FEProblem *>("_fe_problem")),
    _formulation(getParam<MooseEnum>("formulation").getEnum<Formulation>()),
    _penalty(getParam<Real>("penalty")),
    _residual_copy(_sys.residualGhosted())
{
  _overwrite_slave_residual = false;
  prepareSlaveToMasterMap();
}

void
EqualValueEmbeddedConstraint::prepareSlaveToMasterMap()
{
  // get mesh pointLocator
  std::unique_ptr<PointLocatorBase> pointLocator = _mesh.getPointLocator();
  pointLocator->enable_out_of_mesh_mode();
  const std::set<subdomain_id_type> allowed_subdomains{_master};

  // slave id and master id
  dof_id_type sid, mid;

  // prepare _slave_to_master_map
  std::set<dof_id_type> unique_slave_node_ids;
  const MeshBase & meshhelper = _mesh.getMesh();
  for (const auto & elem : as_range(meshhelper.active_subdomain_elements_begin(_slave),
                                    meshhelper.active_subdomain_elements_end(_slave)))
  {
    for (auto & sn : elem->node_ref_range())
    {
      sid = sn.id();
      if (_slave_to_master_map.find(sid) == _slave_to_master_map.end())
      {
        // master element
        const Elem * me = pointLocator->operator()(sn, &allowed_subdomains);
        if (me != NULL)
        {
          mid = me->id();
          _slave_to_master_map.insert(std::pair<dof_id_type, dof_id_type>(sid, mid));
          _subproblem.addGhostedElem(mid);
        }
      }
    }
  }
}

bool
EqualValueEmbeddedConstraint::shouldApply()
{
  // master element
  auto it = _slave_to_master_map.find(_current_node->id());

  if (it != _slave_to_master_map.end())
  {
    const Elem * master_elem = _mesh.elemPtr(it->second);
    std::vector<Point> points = {*_current_node};

    // reinit variables on the master element at the slave point
    _fe_problem.setNeighborSubdomainID(master_elem, 0);
    _fe_problem.reinitNeighborPhys(master_elem, points, 0);

    reinitConstraint();

    return true;
  }
  return false;
}

void
EqualValueEmbeddedConstraint::reinitConstraint()
{
  const Node * node = _current_node;
  unsigned int sys_num = _sys.number();
  dof_id_type dof_number = node->dof_number(sys_num, _var.number(), 0);

  switch (_formulation)
  {
    case Formulation::KINEMATIC:
      _constraint_residual = -_residual_copy(dof_number);
      break;

    case Formulation::PENALTY:
      _constraint_residual = _penalty * (_u_slave[0] - _u_master[0]);
      break;

    default:
      mooseError("Invalid formulation");
      break;
  }
}

Real
EqualValueEmbeddedConstraint::computeQpSlaveValue()
{
  return _u_slave[_qp];
}

Real
EqualValueEmbeddedConstraint::computeQpResidual(Moose::ConstraintType type)
{
  Real resid = _constraint_residual;

  switch (type)
  {
    case Moose::Slave:
    {
      if (_formulation == Formulation::KINEMATIC)
      {
        Real pen_force = _penalty * (_u_slave[_qp] - _u_master[_qp]);
        resid += pen_force;
      }
      return _test_slave[_i][_qp] * resid;
    }

    case Moose::Master:
      return _test_master[_i][_qp] * -resid;
  }

  return 0.0;
}

Real
EqualValueEmbeddedConstraint::computeQpJacobian(Moose::ConstraintJacobianType type)
{
  unsigned int sys_num = _sys.number();
  const Real penalty = _penalty;
  Real curr_jac, slave_jac;

  switch (type)
  {
    case Moose::SlaveSlave:
      switch (_formulation)
      {
        case Formulation::KINEMATIC:
          curr_jac = (*_jacobian)(_current_node->dof_number(sys_num, _var.number(), 0),
                                  _connected_dof_indices[_j]);
          return -curr_jac + _phi_slave[_j][_qp] * penalty * _test_slave[_i][_qp];
        case Formulation::PENALTY:
          return _phi_slave[_j][_qp] * penalty * _test_slave[_i][_qp];
        default:
          mooseError("Invalid formulation");
      }

    case Moose::SlaveMaster:
      switch (_formulation)
      {
        case Formulation::KINEMATIC:
          return -_phi_master[_j][_qp] * penalty * _test_slave[_i][_qp];
        case Formulation::PENALTY:
          return -_phi_master[_j][_qp] * penalty * _test_slave[_i][_qp];
        default:
          mooseError("Invalid formulation");
      }

    case Moose::MasterSlave:
      switch (_formulation)
      {
        case Formulation::KINEMATIC:
          slave_jac = (*_jacobian)(_current_node->dof_number(sys_num, _var.number(), 0),
                                   _connected_dof_indices[_j]);
          return slave_jac * _test_master[_i][_qp];
        case Formulation::PENALTY:
          return -_phi_slave[_j][_qp] * penalty * _test_master[_i][_qp];
        default:
          mooseError("Invalid formulation");
      }

    case Moose::MasterMaster:
      switch (_formulation)
      {
        case Formulation::KINEMATIC:
          return 0.0;
        case Formulation::PENALTY:
          return _test_master[_i][_qp] * penalty * _phi_master[_j][_qp];
        default:
          mooseError("Invalid formulation");
      }
  }
  return 0.0;
}

Real
EqualValueEmbeddedConstraint::computeQpOffDiagJacobian(Moose::ConstraintJacobianType type,
                                                       unsigned int /*jvar*/)
{
  Real curr_jac, slave_jac;
  unsigned int sys_num = _sys.number();

  switch (type)
  {
    case Moose::SlaveSlave:
      curr_jac = (*_jacobian)(_current_node->dof_number(sys_num, _var.number(), 0),
                              _connected_dof_indices[_j]);
      return -curr_jac;

    case Moose::SlaveMaster:
      return 0.0;

    case Moose::MasterSlave:
      switch (_formulation)
      {
        case Formulation::KINEMATIC:
          slave_jac = (*_jacobian)(_current_node->dof_number(sys_num, _var.number(), 0),
                                   _connected_dof_indices[_j]);
          return slave_jac * _test_master[_i][_qp];
        case Formulation::PENALTY:
          return 0.0;
        default:
          mooseError("Invalid formulation");
      }

    case Moose::MasterMaster:
      return 0.0;
  }

  return 0.0;
}

void
EqualValueEmbeddedConstraint::computeJacobian()
{
  getConnectedDofIndices(_var.number());

  DenseMatrix<Number> & Knn =
      _assembly.jacobianBlockNeighbor(Moose::NeighborNeighbor, _master_var.number(), _var.number());

  _Kee.resize(_test_slave.size(), _connected_dof_indices.size());

  for (_i = 0; _i < _test_slave.size(); _i++)
    // Loop over the connected dof indices so we can get all the jacobian contributions
    for (_j = 0; _j < _connected_dof_indices.size(); _j++)
      _Kee(_i, _j) += computeQpJacobian(Moose::SlaveSlave);

  DenseMatrix<Number> & Ken =
      _assembly.jacobianBlockNeighbor(Moose::ElementNeighbor, _var.number(), _var.number());
  if (Ken.m() && Ken.n())
    for (_i = 0; _i < _test_slave.size(); _i++)
      for (_j = 0; _j < _phi_master.size(); _j++)
        Ken(_i, _j) += computeQpJacobian(Moose::SlaveMaster);

  _Kne.resize(_test_master.size(), _connected_dof_indices.size());
  for (_i = 0; _i < _test_master.size(); _i++)
    // Loop over the connected dof indices so we can get all the jacobian contributions
    for (_j = 0; _j < _connected_dof_indices.size(); _j++)
      _Kne(_i, _j) += computeQpJacobian(Moose::MasterSlave);

  if (Knn.m() && Knn.n())
    for (_i = 0; _i < _test_master.size(); _i++)
      for (_j = 0; _j < _phi_master.size(); _j++)
        Knn(_i, _j) += computeQpJacobian(Moose::MasterMaster);
}

void
EqualValueEmbeddedConstraint::computeOffDiagJacobian(unsigned int jvar)
{
  getConnectedDofIndices(jvar);

  _Kee.resize(_test_slave.size(), _connected_dof_indices.size());

  DenseMatrix<Number> & Knn =
      _assembly.jacobianBlockNeighbor(Moose::NeighborNeighbor, _master_var.number(), jvar);

  for (_i = 0; _i < _test_slave.size(); _i++)
    // Loop over the connected dof indices so we can get all the jacobian contributions
    for (_j = 0; _j < _connected_dof_indices.size(); _j++)
      _Kee(_i, _j) += computeQpOffDiagJacobian(Moose::SlaveSlave, jvar);

  DenseMatrix<Number> & Ken =
      _assembly.jacobianBlockNeighbor(Moose::ElementNeighbor, _var.number(), jvar);
  for (_i = 0; _i < _test_slave.size(); _i++)
    for (_j = 0; _j < _phi_master.size(); _j++)
      Ken(_i, _j) += computeQpOffDiagJacobian(Moose::SlaveMaster, jvar);

  _Kne.resize(_test_master.size(), _connected_dof_indices.size());
  if (_Kne.m() && _Kne.n())
    for (_i = 0; _i < _test_master.size(); _i++)
      // Loop over the connected dof indices so we can get all the jacobian contributions
      for (_j = 0; _j < _connected_dof_indices.size(); _j++)
        _Kne(_i, _j) += computeQpOffDiagJacobian(Moose::MasterSlave, jvar);

  for (_i = 0; _i < _test_master.size(); _i++)
    for (_j = 0; _j < _phi_master.size(); _j++)
      Knn(_i, _j) += computeQpOffDiagJacobian(Moose::MasterMaster, jvar);
}

void
EqualValueEmbeddedConstraint::getConnectedDofIndices(unsigned int var_num)
{
  NodeElemConstraint::getConnectedDofIndices(var_num);

  _phi_slave.resize(_connected_dof_indices.size());

  dof_id_type current_node_var_dof_index = _sys.getVariable(0, var_num).nodalDofIndex();

  // Fill up _phi_slave so that it is 1 when j corresponds to the dof associated with this node
  // and 0 for every other dof
  // This corresponds to evaluating all of the connected shape functions at _this_ node
  _qp = 0;
  for (unsigned int j = 0; j < _connected_dof_indices.size(); j++)
  {
    _phi_slave[j].resize(1);

    if (_connected_dof_indices[j] == current_node_var_dof_index)
      _phi_slave[j][_qp] = 1.0;
    else
      _phi_slave[j][_qp] = 0.0;
  }
}
