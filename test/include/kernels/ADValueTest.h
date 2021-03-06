//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html
#ifndef ADVALUETEST_H_
#define ADVALUETEST_H_

#include "ADKernel.h"

template <ComputeStage>
class ADValueTest;

declareADValidParams(ADValueTest);

template <ComputeStage compute_stage>
class ADValueTest : public ADKernel<compute_stage>
{
public:
  ADValueTest(const InputParameters & parameters);

protected:
  virtual ADResidual computeQpResidual();

  usingKernelMembers;
};

#endif /* ADVALUETEST_H_ */
