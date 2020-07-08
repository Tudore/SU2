/*!
 * \file CFVMFlowSolverBase.hpp
 * \brief Base class template for all FVM flow solvers.
 * \version 7.0.5 "Blackbird"
 *
 * SU2 Project Website: https://su2code.github.io
 *
 * The SU2 Project is maintained by the SU2 Foundation
 * (http://su2foundation.org)
 *
 * Copyright 2012-2020, SU2 Contributors (cf. AUTHORS.md)
 *
 * SU2 is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * SU2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with SU2. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "CFVMFlowSolverBase.inl"
#include "../gradients/computeGradientsGreenGauss.hpp"
#include "../gradients/computeGradientsLeastSquares.hpp"
#include "../limiters/computeLimiters.hpp"


template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::AeroCoeffsArray::allocate(int size) {
  _size = size;
  CD = new su2double[size]; CL = new su2double[size]; CSF = new su2double[size]; CEff = new su2double[size];
  CFx = new su2double[size]; CFy = new su2double[size]; CFz = new su2double[size]; CMx = new su2double[size];
  CMy = new su2double[size]; CMz = new su2double[size]; CoPx = new su2double[size]; CoPy = new su2double[size];
  CoPz = new su2double[size]; CT = new su2double[size]; CQ = new su2double[size]; CMerit = new su2double[size];
  setZero();
}

template<class V, ENUM_REGIME R>
CFVMFlowSolverBase<V,R>::AeroCoeffsArray::~AeroCoeffsArray() {
  delete [] CD; delete [] CL; delete [] CSF; delete [] CEff;
  delete [] CFx; delete [] CFy; delete [] CFz; delete [] CMx;
  delete [] CMy; delete [] CMz; delete [] CoPx; delete [] CoPy;
  delete [] CoPz; delete [] CT; delete [] CQ; delete [] CMerit;
}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::AeroCoeffsArray::setZero(int i) {
  CD[i] = CL[i] = CSF[i] = CEff[i] = 0.0;
  CFx[i] = CFy[i] = CFz[i] = CMx[i] = 0.0;
  CMy[i] = CMz[i] = CoPx[i] = CoPy[i] = 0.0;
  CoPz[i] = CT[i] = CQ[i] = CMerit[i] = 0.0;
}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::Allocate(const CConfig& config) {

  unsigned short iVar, iMarker;
  unsigned long iPoint, iVertex;

  /*--- Define some auxiliar vector related with the residual ---*/

  Residual_RMS  = new su2double[nVar]();
  Residual_Max  = new su2double[nVar]();

  /*--- Define some structures for locating max residuals ---*/

  Point_Max = new unsigned long[nVar]();
  Point_Max_Coord = new su2double*[nVar];
  for (iVar = 0; iVar < nVar; iVar++) {
    Point_Max_Coord[iVar] = new su2double[nDim]();
  }

  /*--- Define some auxiliar vector related with the undivided lapalacian computation ---*/

  if (config.GetKind_ConvNumScheme_Flow() == SPACE_CENTERED) {
    iPoint_UndLapl = new su2double [nPoint];
    jPoint_UndLapl = new su2double [nPoint];
  }

  /*--- Initialize the solution and right hand side vectors for storing
   the residuals and updating the solution (always needed even for
   explicit schemes). ---*/

  LinSysSol.Initialize(nPoint, nPointDomain, nVar, 0.0);
  LinSysRes.Initialize(nPoint, nPointDomain, nVar, 0.0);

  /*--- Allocates a 2D array with variable "outer" sizes and init to 0. ---*/

  auto Alloc2D = [](unsigned long M, const unsigned long* N, su2double**& X) {
    X = new su2double* [M];
    for(unsigned long i = 0; i < M; ++i)
      X[i] = new su2double [N[i]] ();
  };

  /*--- Allocates a 3D array with variable "middle" sizes and init to 0. ---*/

  auto Alloc3D = [](unsigned long M, const unsigned long* N, unsigned long P, su2double***& X) {
    X = new su2double** [M];
    for(unsigned long i = 0; i < M; ++i) {
      X[i] = new su2double* [N[i]];
      for(unsigned long j = 0; j < N[i]; ++j)
        X[i][j] = new su2double [P] ();
    }
  };

  /*--- Store the value of the characteristic primitive variables at the boundaries ---*/

  Alloc3D(nMarker, nVertex, nPrimVar, CharacPrimVar);

  /*--- Store the value of the Total Pressure at the inlet BC ---*/

  Alloc2D(nMarker, nVertex, Inlet_Ttotal);

  /*--- Store the value of the Total Temperature at the inlet BC ---*/

  Alloc2D(nMarker, nVertex, Inlet_Ptotal);

  /*--- Store the value of the Flow direction at the inlet BC ---*/

  Alloc3D(nMarker, nVertex, nDim, Inlet_FlowDir);

  /*--- Force definition and coefficient arrays for all of the markers ---*/

  Alloc2D(nMarker, nVertex, CPressure);
  Alloc2D(nMarker, nVertex, CPressureTarget);

  /*--- Non dimensional aerodynamic coefficients ---*/

  InvCoeff.allocate(nMarker);
  MntCoeff.allocate(nMarker);
  ViscCoeff.allocate(nMarker);
  SurfaceInvCoeff.allocate(config.GetnMarker_Monitoring());
  SurfaceMntCoeff.allocate(config.GetnMarker_Monitoring());
  SurfaceViscCoeff.allocate(config.GetnMarker_Monitoring());
  SurfaceCoeff.allocate(config.GetnMarker_Monitoring());

  /*--- Heat flux coefficients. ---*/

  HF_Visc = new su2double[nMarker];
  MaxHF_Visc = new su2double[nMarker];

  Surface_HF_Visc = new su2double[config.GetnMarker_Monitoring()];
  Surface_MaxHF_Visc = new su2double[config.GetnMarker_Monitoring()];

  /*--- Supersonic coefficients ---*/

  CNearFieldOF_Inv = new su2double[nMarker];

  /*--- Initializate quantities for SlidingMesh Interface ---*/

  SlidingState = new su2double*** [nMarker]();
  SlidingStateNodes = new int* [nMarker]();

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    if (config.GetMarker_All_KindBC(iMarker) == FLUID_INTERFACE) {

      SlidingState[iMarker] = new su2double**[nVertex[iMarker]]();
      SlidingStateNodes[iMarker]  = new int [nVertex[iMarker]]();

      for (iPoint = 0; iPoint < nVertex[iMarker]; iPoint++)
        SlidingState[iMarker][iPoint] = new su2double*[nPrimVar+1]();
    }
  }

  /*--- Heat flux in all the markers ---*/

  Alloc2D(nMarker, nVertex, HeatFlux);
  Alloc2D(nMarker, nVertex, HeatFluxTarget);

  /*--- Y plus in all the markers ---*/

  Alloc2D(nMarker, nVertex, YPlus);

  /*--- Skin friction in all the markers ---*/

  Alloc3D(nMarker, nVertex, nDim, CSkinFriction);

  /*--- Store the values of the temperature and the heat flux density at the boundaries,
   used for coupling with a solid donor cell ---*/
  constexpr auto nHeatConjugateVar = 4u;

  HeatConjugateVar = new su2double** [nMarker];
  for (iMarker = 0; iMarker < nMarker; iMarker++) {
    HeatConjugateVar[iMarker] = new su2double* [nVertex[iMarker]];
    for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++) {
      HeatConjugateVar[iMarker][iVertex] = new su2double [nHeatConjugateVar]();
      HeatConjugateVar[iMarker][iVertex][0] = config.GetTemperature_FreeStreamND();
    }
  }

  /*--- Only initialize when there is a Marker_Fluid_Load defined
   *--- (this avoids overhead in all other cases while a more permanent structure is being developed) ---*/
  if((config.GetnMarker_Fluid_Load() > 0) && (MGLevel == MESH_0)){

    InitVertexTractionContainer();

    if (config.GetDiscrete_Adjoint())
      InitVertexTractionAdjointContainer();
  }

  /*--- Initialize the BGS residuals in FSI problems. ---*/
  if (config.GetMultizone_Residual()){

    Residual_BGS = new su2double[nVar];
    for (iVar = 0; iVar < nVar; iVar++)
      Residual_BGS[iVar] = 1.0;

    Residual_Max_BGS = new su2double[nVar];
    for (iVar = 0; iVar < nVar; iVar++)
      Residual_Max_BGS[iVar] = 1.0;

    /*--- Define some structures for locating max residuals ---*/

    Point_Max_BGS = new unsigned long[nVar]();
    Point_Max_Coord_BGS = new su2double*[nVar];
    for (iVar = 0; iVar < nVar; iVar++) {
      Point_Max_Coord_BGS[iVar] = new su2double[nDim]();
    }
  }

}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::HybridParallelInitialization(const CConfig& config,
                                                           CGeometry& geometry) {
#ifdef HAVE_OMP
  /*--- Get the edge coloring. If the expected parallel efficiency becomes too low setup the
   *    reducer strategy. Where one loop is performed over edges followed by a point loop to
   *    sum the fluxes for each cell and set the diagonal of the system matrix. ---*/

  su2double parallelEff = 1.0;
  const auto& coloring = geometry.GetEdgeColoring(&parallelEff);

  /*--- The decision to use the strategy is local to each rank. ---*/
  ReducerStrategy = parallelEff < COLORING_EFF_THRESH;

  /*--- When using the reducer force a single color to reduce the color loop overhead. ---*/
  if (ReducerStrategy && (coloring.getOuterSize()>1))
    geometry.SetNaturalEdgeColoring();

  if (!coloring.empty()) {
    /*--- If the reducer strategy is used we are not constrained by group
     *    size as we have no other edge loops in the Euler/NS solvers. ---*/
    auto groupSize = ReducerStrategy? 1ul : geometry.GetEdgeColorGroupSize();
    auto nColor = coloring.getOuterSize();
    EdgeColoring.reserve(nColor);

    for(auto iColor = 0ul; iColor < nColor; ++iColor)
      EdgeColoring.emplace_back(coloring.innerIdx(iColor), coloring.getNumNonZeros(iColor), groupSize);
  }

  /*--- If the reducer strategy is not being forced (by EDGE_COLORING_GROUP_SIZE=0) print some messages. ---*/
  if (config.GetEdgeColoringGroupSize() != 1<<30) {

    su2double minEff = 1.0;
    SU2_MPI::Reduce(&parallelEff, &minEff, 1, MPI_DOUBLE, MPI_MIN, MASTER_NODE, MPI_COMM_WORLD);

    int tmp = ReducerStrategy, numRanksUsingReducer = 0;
    SU2_MPI::Reduce(&tmp, &numRanksUsingReducer, 1, MPI_INT, MPI_SUM, MASTER_NODE, MPI_COMM_WORLD);

    if (minEff < COLORING_EFF_THRESH) {
      cout << "WARNING: On " << numRanksUsingReducer << " MPI ranks the coloring efficiency was less than "
           << COLORING_EFF_THRESH << " (min value was " << minEff << ").\n"
           << "         Those ranks will now use a fallback strategy, better performance may be possible\n"
           << "         with a different value of config option EDGE_COLORING_GROUP_SIZE (default 512)." << endl;
    }
  }

  if (ReducerStrategy)
    EdgeFluxes.Initialize(geometry.GetnEdge(), geometry.GetnEdge(), nVar, nullptr);

  omp_chunk_size = computeStaticChunkSize(nPoint, omp_get_max_threads(), OMP_MAX_SIZE);
#else
  EdgeColoring[0] = DummyGridColor<>(geometry.GetnEdge());
#endif
}

template<class V, ENUM_REGIME R>
CFVMFlowSolverBase<V,R>::~CFVMFlowSolverBase() {

  unsigned short iMarker, iVar, iDim;
  unsigned long iVertex;

  delete [] CNearFieldOF_Inv;
  delete [] HF_Visc;
  delete [] MaxHF_Visc;
  delete [] Surface_HF_Visc;
  delete [] Surface_MaxHF_Visc;

  if (SlidingState != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      if ( SlidingState[iMarker] != nullptr ) {
        for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++)
          if ( SlidingState[iMarker][iVertex] != nullptr ){
            for (iVar = 0; iVar < nPrimVar+1; iVar++)
              delete [] SlidingState[iMarker][iVertex][iVar];
            delete [] SlidingState[iMarker][iVertex];
          }
        delete [] SlidingState[iMarker];
      }
    }
    delete [] SlidingState;
  }

  if ( SlidingStateNodes != nullptr ){
    for (iMarker = 0; iMarker < nMarker; iMarker++){
      if (SlidingStateNodes[iMarker] != nullptr)
        delete [] SlidingStateNodes[iMarker];
    }
    delete [] SlidingStateNodes;
  }

  if (CPressure != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++)
      delete [] CPressure[iMarker];
    delete [] CPressure;
  }

  if (CPressureTarget != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++)
      delete [] CPressureTarget[iMarker];
    delete [] CPressureTarget;
  }

  if (CharacPrimVar != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++)
        delete [] CharacPrimVar[iMarker][iVertex];
      delete [] CharacPrimVar[iMarker];
    }
    delete [] CharacPrimVar;
  }

  if (Inlet_Ttotal != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++)
      if (Inlet_Ttotal[iMarker] != nullptr)
        delete [] Inlet_Ttotal[iMarker];
    delete [] Inlet_Ttotal;
  }

  if (Inlet_Ptotal != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++)
      if (Inlet_Ptotal[iMarker] != nullptr)
        delete [] Inlet_Ptotal[iMarker];
    delete [] Inlet_Ptotal;
  }

  if (Inlet_FlowDir != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      if (Inlet_FlowDir[iMarker] != nullptr) {
        for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++)
          delete [] Inlet_FlowDir[iMarker][iVertex];
        delete [] Inlet_FlowDir[iMarker];
      }
    }
    delete [] Inlet_FlowDir;
  }

  if (HeatFlux != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      delete [] HeatFlux[iMarker];
    }
    delete [] HeatFlux;
  }

  if (HeatFluxTarget != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      delete [] HeatFluxTarget[iMarker];
    }
    delete [] HeatFluxTarget;
  }

  if (YPlus != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      delete [] YPlus[iMarker];
    }
    delete [] YPlus;
  }

  if (CSkinFriction != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iDim = 0; iDim < nDim; iDim++) {
        delete [] CSkinFriction[iMarker][iDim];
      }
      delete [] CSkinFriction[iMarker];
    }
    delete [] CSkinFriction;
  }

  if (HeatConjugateVar != nullptr) {
    for (iMarker = 0; iMarker < nMarker; iMarker++) {
      for (iVertex = 0; iVertex < nVertex[iMarker]; iVertex++) {
        delete [] HeatConjugateVar[iMarker][iVertex];
      }
      delete [] HeatConjugateVar[iMarker];
    }
    delete [] HeatConjugateVar;
  }

  delete nodes;
}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::SetPrimitive_Gradient_GG(CGeometry *geometry, const CConfig *config, bool reconstruction) {

  const auto& primitives = nodes->GetPrimitive();
  auto& gradient = reconstruction? nodes->GetGradient_Reconstruction() : nodes->GetGradient_Primitive();

  computeGradientsGreenGauss(this, PRIMITIVE_GRADIENT, PERIODIC_PRIM_GG, *geometry,
                             *config, primitives, 0, nPrimVarGrad, gradient);
}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::SetPrimitive_Gradient_LS(CGeometry *geometry, const CConfig *config, bool reconstruction) {

  /*--- Set a flag for unweighted or weighted least-squares. ---*/
  bool weighted;

  if (reconstruction)
    weighted = (config->GetKind_Gradient_Method_Recon() == WEIGHTED_LEAST_SQUARES);
  else
    weighted = (config->GetKind_Gradient_Method() == WEIGHTED_LEAST_SQUARES);

  const auto& primitives = nodes->GetPrimitive();
  auto& rmatrix = nodes->GetRmatrix();
  auto& gradient = reconstruction? nodes->GetGradient_Reconstruction() : nodes->GetGradient_Primitive();
  PERIODIC_QUANTITIES kindPeriodicComm = weighted? PERIODIC_PRIM_LS : PERIODIC_PRIM_ULS;

  computeGradientsLeastSquares(this, PRIMITIVE_GRADIENT, kindPeriodicComm, *geometry, *config,
                               weighted, primitives, 0, nPrimVarGrad, gradient, rmatrix);
}

template<class V, ENUM_REGIME R>
void CFVMFlowSolverBase<V,R>::SetPrimitive_Limiter(CGeometry *geometry, const CConfig *config) {

  auto kindLimiter = static_cast<ENUM_LIMITER>(config->GetKind_SlopeLimit_Flow());
  const auto& primitives = nodes->GetPrimitive();
  const auto& gradient = nodes->GetGradient_Reconstruction();
  auto& primMin = nodes->GetSolution_Min();
  auto& primMax = nodes->GetSolution_Max();
  auto& limiter = nodes->GetLimiter_Primitive();

  computeLimiters(kindLimiter, this, PRIMITIVE_LIMITER, PERIODIC_LIM_PRIM_1, PERIODIC_LIM_PRIM_2,
            *geometry, *config, 0, nPrimVarGrad, primitives, gradient, primMin, primMax, limiter);
}

template<class V, ENUM_REGIME FlowRegime>
void CFVMFlowSolverBase<V,FlowRegime>::Pressure_Forces(const CGeometry* geometry, const CConfig* config) {

  unsigned long iVertex, iPoint;
  unsigned short iDim, iMarker, Boundary, Monitoring, iMarker_Monitoring;
  su2double Pressure = 0.0, factor, NFPressOF, RefVel2 = 0.0,
  RefTemp, RefDensity = 0.0, RefPressure, Mach2Vel, Mach_Motion;
  const su2double *Normal = nullptr, *Coord = nullptr;
  string Marker_Tag, Monitoring_Tag;
  su2double AxiFactor;

  su2double Alpha = config->GetAoA()*PI_NUMBER/180.0;
  su2double Beta = config->GetAoS()*PI_NUMBER/180.0;
  su2double RefArea = config->GetRefArea();
  su2double RefLength = config->GetRefLength();
  su2double Gas_Constant = config->GetGas_ConstantND();
  auto Origin = config->GetRefOriginMoment(0);
  bool axisymmetric = config->GetAxisymmetric();

  /// TODO: Move these ifs to specialized functions.

  if (FlowRegime == COMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dynamic meshes, use the motion Mach number as a reference value
     for computing the force coefficients. Otherwise, use the freestream values,
     which is the standard convention. ---*/

    RefTemp = Temperature_Inf;
    RefDensity = Density_Inf;
    if (dynamic_grid) {
      Mach2Vel = sqrt(Gamma*Gas_Constant*RefTemp);
      Mach_Motion = config->GetMach_Motion();
      RefVel2 = (Mach_Motion*Mach2Vel)*(Mach_Motion*Mach2Vel);
    } else {
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
  }

  if (FlowRegime == INCOMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dimensional or non-dim based on initial values, use
     the far-field state (inf). For a custom non-dim based
     on user-provided reference values, use the ref values
     to compute the forces. ---*/

    if ((config->GetRef_Inc_NonDim() == DIMENSIONAL) ||
        (config->GetRef_Inc_NonDim() == INITIAL_VALUES)) {
      RefDensity  = Density_Inf;
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
    else if (config->GetRef_Inc_NonDim() == REFERENCE_VALUES) {
      RefDensity = config->GetInc_Density_Ref();
      RefVel2    = config->GetInc_Velocity_Ref()*config->GetInc_Velocity_Ref();
    }
  }

  factor = 1.0 / (0.5*RefDensity*RefArea*RefVel2);

  /*--- Reference pressure is always the far-field value. ---*/

  RefPressure = Pressure_Inf;

  /*-- Variables initialization ---*/

  TotalCoeff.setZero();

  Total_CNearFieldOF = 0.0; Total_Heat = 0.0;  Total_MaxHeat = 0.0;

  AllBoundInvCoeff.setZero();

  AllBound_CNearFieldOF_Inv = 0.0;

  SurfaceInvCoeff.setZero();
  SurfaceCoeff.setZero();

  /*--- Loop over the Euler and Navier-Stokes markers ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Boundary   = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    /*--- Obtain the origin for the moment computation for a particular marker ---*/

    if (Monitoring == YES) {
      for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
        Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
        Marker_Tag = config->GetMarker_All_TagBound(iMarker);
        if (Marker_Tag == Monitoring_Tag)
          Origin = config->GetRefOriginMoment(iMarker_Monitoring);
      }
    }

    if (config->GetSolid_Wall(iMarker) || (Boundary == NEARFIELD_BOUNDARY) ||
        (Boundary == INLET_FLOW) || (Boundary == OUTLET_FLOW) ||
        (Boundary == ACTDISK_INLET) || (Boundary == ACTDISK_OUTLET)||
        (Boundary == ENGINE_INFLOW) || (Boundary == ENGINE_EXHAUST)) {

      /*--- Forces initialization at each Marker ---*/

      InvCoeff.setZero(iMarker);

      CNearFieldOF_Inv[iMarker] = 0.0;

      su2double ForceInviscid[MAXNDIM] = {0.0}, MomentInviscid[MAXNDIM] = {0.0};
      su2double MomentX_Force[MAXNDIM] = {0.0}, MomentY_Force[MAXNDIM] = {0.0}, MomentZ_Force[MAXNDIM] = {0.0};

      NFPressOF = 0.0;

      /*--- Loop over the vertices to compute the forces ---*/

      for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();

        Pressure = nodes->GetPressure(iPoint);

        CPressure[iMarker][iVertex] = (Pressure - RefPressure)*factor*RefArea;

        /*--- Note that the pressure coefficient is computed at the
         halo cells (for visualization purposes), but not the forces ---*/

        if ( (geometry->nodes->GetDomain(iPoint)) && (Monitoring == YES) ) {

          Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
          Coord = geometry->nodes->GetCoord(iPoint);

          /*--- Quadratic objective function for the near-field.
           This uses the infinity pressure regardless of Mach number. ---*/

          NFPressOF += 0.5*(Pressure - Pressure_Inf)*(Pressure - Pressure_Inf)*Normal[nDim-1];

          su2double MomentDist[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            MomentDist[iDim] = Coord[iDim] - Origin[iDim];
          }

          /*--- Axisymmetric simulations ---*/

          if (axisymmetric) AxiFactor = 2.0*PI_NUMBER*geometry->nodes->GetCoord(iPoint, 1);
          else AxiFactor = 1.0;

          /*--- Force computation, note the minus sign due to the
           orientation of the normal (outward) ---*/

          su2double Force[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            Force[iDim] = -(Pressure - Pressure_Inf) * Normal[iDim] * factor * AxiFactor;
            ForceInviscid[iDim] += Force[iDim];
          }

          /*--- Moment with respect to the reference axis ---*/

          if (nDim == 3) {
            MomentInviscid[0] += (Force[2]*MomentDist[1]-Force[1]*MomentDist[2])/RefLength;
            MomentX_Force[1]  += (-Force[1]*Coord[2]);
            MomentX_Force[2]  += (Force[2]*Coord[1]);

            MomentInviscid[1] += (Force[0]*MomentDist[2]-Force[2]*MomentDist[0])/RefLength;
            MomentY_Force[2]  += (-Force[2]*Coord[0]);
            MomentY_Force[0]  += (Force[0]*Coord[2]);
          }
          MomentInviscid[2] += (Force[1]*MomentDist[0]-Force[0]*MomentDist[1])/RefLength;
          MomentZ_Force[0]  += (-Force[0]*Coord[1]);
          MomentZ_Force[1]  += (Force[1]*Coord[0]);
        }

      }

      /*--- Project forces and store the non-dimensional coefficients ---*/

      if (Monitoring == YES) {

        if (Boundary != NEARFIELD_BOUNDARY) {
          if (nDim == 2) {
            InvCoeff.CD[iMarker]     =  ForceInviscid[0]*cos(Alpha) + ForceInviscid[1]*sin(Alpha);
            InvCoeff.CL[iMarker]     = -ForceInviscid[0]*sin(Alpha) + ForceInviscid[1]*cos(Alpha);
            InvCoeff.CEff[iMarker]   = InvCoeff.CL[iMarker] / (InvCoeff.CD[iMarker]+EPS);
            InvCoeff.CMz[iMarker]    = MomentInviscid[2];
            InvCoeff.CoPx[iMarker]   = MomentZ_Force[1];
            InvCoeff.CoPy[iMarker]   = -MomentZ_Force[0];
            InvCoeff.CFx[iMarker]    = ForceInviscid[0];
            InvCoeff.CFy[iMarker]    = ForceInviscid[1];
            InvCoeff.CT[iMarker]     = -InvCoeff.CFx[iMarker];
            InvCoeff.CQ[iMarker]     = -InvCoeff.CMz[iMarker];
            InvCoeff.CMerit[iMarker] = InvCoeff.CT[iMarker] / (InvCoeff.CQ[iMarker] + EPS);
          }
          if (nDim == 3) {
            InvCoeff.CD[iMarker]      =  ForceInviscid[0]*cos(Alpha)*cos(Beta) + ForceInviscid[1]*sin(Beta) + ForceInviscid[2]*sin(Alpha)*cos(Beta);
            InvCoeff.CL[iMarker]      = -ForceInviscid[0]*sin(Alpha) + ForceInviscid[2]*cos(Alpha);
            InvCoeff.CSF[iMarker]     = -ForceInviscid[0]*sin(Beta)*cos(Alpha) + ForceInviscid[1]*cos(Beta) - ForceInviscid[2]*sin(Beta)*sin(Alpha);
            InvCoeff.CEff[iMarker]    = InvCoeff.CL[iMarker] / (InvCoeff.CD[iMarker] + EPS);
            InvCoeff.CMx[iMarker]     = MomentInviscid[0];
            InvCoeff.CMy[iMarker]     = MomentInviscid[1];
            InvCoeff.CMz[iMarker]     = MomentInviscid[2];
            InvCoeff.CoPx[iMarker]    = -MomentY_Force[0];
            InvCoeff.CoPz[iMarker]    = MomentY_Force[2];
            InvCoeff.CFx[iMarker]     = ForceInviscid[0];
            InvCoeff.CFy[iMarker]     = ForceInviscid[1];
            InvCoeff.CFz[iMarker]     = ForceInviscid[2];
            InvCoeff.CT[iMarker]      = -InvCoeff.CFz[iMarker];
            InvCoeff.CQ[iMarker]      = -InvCoeff.CMz[iMarker];
            InvCoeff.CMerit[iMarker]  = InvCoeff.CT[iMarker] / (InvCoeff.CQ[iMarker] + EPS);
          }

          AllBoundInvCoeff.CD           += InvCoeff.CD[iMarker];
          AllBoundInvCoeff.CL           += InvCoeff.CL[iMarker];
          AllBoundInvCoeff.CSF          += InvCoeff.CSF[iMarker];
          AllBoundInvCoeff.CEff          = AllBoundInvCoeff.CL / (AllBoundInvCoeff.CD + EPS);
          AllBoundInvCoeff.CMx          += InvCoeff.CMx[iMarker];
          AllBoundInvCoeff.CMy          += InvCoeff.CMy[iMarker];
          AllBoundInvCoeff.CMz          += InvCoeff.CMz[iMarker];
          AllBoundInvCoeff.CoPx         += InvCoeff.CoPx[iMarker];
          AllBoundInvCoeff.CoPy         += InvCoeff.CoPy[iMarker];
          AllBoundInvCoeff.CoPz         += InvCoeff.CoPz[iMarker];
          AllBoundInvCoeff.CFx          += InvCoeff.CFx[iMarker];
          AllBoundInvCoeff.CFy          += InvCoeff.CFy[iMarker];
          AllBoundInvCoeff.CFz          += InvCoeff.CFz[iMarker];
          AllBoundInvCoeff.CT           += InvCoeff.CT[iMarker];
          AllBoundInvCoeff.CQ           += InvCoeff.CQ[iMarker];
          AllBoundInvCoeff.CMerit        = AllBoundInvCoeff.CT / (AllBoundInvCoeff.CQ + EPS);

          /*--- Compute the coefficients per surface ---*/

          for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
            Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
            Marker_Tag = config->GetMarker_All_TagBound(iMarker);
            if (Marker_Tag == Monitoring_Tag) {
              SurfaceInvCoeff.CL[iMarker_Monitoring]      += InvCoeff.CL[iMarker];
              SurfaceInvCoeff.CD[iMarker_Monitoring]      += InvCoeff.CD[iMarker];
              SurfaceInvCoeff.CSF[iMarker_Monitoring]     += InvCoeff.CSF[iMarker];
              SurfaceInvCoeff.CEff[iMarker_Monitoring]     = InvCoeff.CL[iMarker] / (InvCoeff.CD[iMarker] + EPS);
              SurfaceInvCoeff.CFx[iMarker_Monitoring]     += InvCoeff.CFx[iMarker];
              SurfaceInvCoeff.CFy[iMarker_Monitoring]     += InvCoeff.CFy[iMarker];
              SurfaceInvCoeff.CFz[iMarker_Monitoring]     += InvCoeff.CFz[iMarker];
              SurfaceInvCoeff.CMx[iMarker_Monitoring]     += InvCoeff.CMx[iMarker];
              SurfaceInvCoeff.CMy[iMarker_Monitoring]     += InvCoeff.CMy[iMarker];
              SurfaceInvCoeff.CMz[iMarker_Monitoring]     += InvCoeff.CMz[iMarker];
            }
          }

        }

        /*--- At the Nearfield SU2 only cares about the pressure coeffient ---*/

        else {
          CNearFieldOF_Inv[iMarker] = NFPressOF;
          AllBound_CNearFieldOF_Inv += CNearFieldOF_Inv[iMarker];
        }

      }

    }
  }

#ifdef HAVE_MPI

  /*--- Add AllBound information using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    auto Allreduce = [](su2double x) {
      su2double tmp = x; x = 0.0;
      SU2_MPI::Allreduce(&tmp, &x, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      return x;
    };
    AllBoundInvCoeff.CD = Allreduce(AllBoundInvCoeff.CD);
    AllBoundInvCoeff.CL = Allreduce(AllBoundInvCoeff.CL);
    AllBoundInvCoeff.CSF = Allreduce(AllBoundInvCoeff.CSF);
    AllBoundInvCoeff.CEff = AllBoundInvCoeff.CL / (AllBoundInvCoeff.CD + EPS);

    AllBoundInvCoeff.CMx = Allreduce(AllBoundInvCoeff.CMx);
    AllBoundInvCoeff.CMy = Allreduce(AllBoundInvCoeff.CMy);
    AllBoundInvCoeff.CMz = Allreduce(AllBoundInvCoeff.CMz);

    AllBoundInvCoeff.CoPx = Allreduce(AllBoundInvCoeff.CoPx);
    AllBoundInvCoeff.CoPy = Allreduce(AllBoundInvCoeff.CoPy);
    AllBoundInvCoeff.CoPz = Allreduce(AllBoundInvCoeff.CoPz);

    AllBoundInvCoeff.CFx = Allreduce(AllBoundInvCoeff.CFx);
    AllBoundInvCoeff.CFy = Allreduce(AllBoundInvCoeff.CFy);
    AllBoundInvCoeff.CFz = Allreduce(AllBoundInvCoeff.CFz);

    AllBoundInvCoeff.CT = Allreduce(AllBoundInvCoeff.CT);
    AllBoundInvCoeff.CQ = Allreduce(AllBoundInvCoeff.CQ);
    AllBoundInvCoeff.CMerit = AllBoundInvCoeff.CT / (AllBoundInvCoeff.CQ + EPS);
    AllBound_CNearFieldOF_Inv = Allreduce(AllBound_CNearFieldOF_Inv);

  }

  /*--- Add the forces on the surfaces using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    int nMarkerMon = config->GetnMarker_Monitoring();

    /*--- Use the same buffer for all reductions. We could avoid the copy back into
     *    the original variable by swaping pointers, but it is safer this way... ---*/

    su2double* buffer = new su2double [nMarkerMon];

    auto Allreduce_inplace = [buffer](int size, su2double* x) {
      SU2_MPI::Allreduce(x, buffer, size, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      for(int i=0; i<size; ++i) x[i] = buffer[i];
    };

    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CL);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CD);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CSF);

    for (iMarker_Monitoring = 0; iMarker_Monitoring < nMarkerMon; iMarker_Monitoring++)
      SurfaceInvCoeff.CEff[iMarker_Monitoring] = SurfaceInvCoeff.CL[iMarker_Monitoring] / (SurfaceInvCoeff.CD[iMarker_Monitoring] + EPS);

    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CFx);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CFy);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CFz);

    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CMx);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CMy);
    Allreduce_inplace(nMarkerMon, SurfaceInvCoeff.CMz);

    delete [] buffer;

  }

#endif

  /*--- Update the total coefficients (note that all the nodes have the same value) ---*/

  TotalCoeff.CD            = AllBoundInvCoeff.CD;
  TotalCoeff.CL            = AllBoundInvCoeff.CL;
  TotalCoeff.CSF           = AllBoundInvCoeff.CSF;
  TotalCoeff.CEff          = TotalCoeff.CL / (TotalCoeff.CD + EPS);
  TotalCoeff.CFx           = AllBoundInvCoeff.CFx;
  TotalCoeff.CFy           = AllBoundInvCoeff.CFy;
  TotalCoeff.CFz           = AllBoundInvCoeff.CFz;
  TotalCoeff.CMx           = AllBoundInvCoeff.CMx;
  TotalCoeff.CMy           = AllBoundInvCoeff.CMy;
  TotalCoeff.CMz           = AllBoundInvCoeff.CMz;
  TotalCoeff.CoPx          = AllBoundInvCoeff.CoPx;
  TotalCoeff.CoPy          = AllBoundInvCoeff.CoPy;
  TotalCoeff.CoPz          = AllBoundInvCoeff.CoPz;
  TotalCoeff.CT            = AllBoundInvCoeff.CT;
  TotalCoeff.CQ            = AllBoundInvCoeff.CQ;
  TotalCoeff.CMerit        = TotalCoeff.CT / (TotalCoeff.CQ + EPS);
  Total_CNearFieldOF       = AllBound_CNearFieldOF_Inv;

  /*--- Update the total coefficients per surface (note that all the nodes have the same value)---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    SurfaceCoeff.CL[iMarker_Monitoring]      = SurfaceInvCoeff.CL[iMarker_Monitoring];
    SurfaceCoeff.CD[iMarker_Monitoring]      = SurfaceInvCoeff.CD[iMarker_Monitoring];
    SurfaceCoeff.CSF[iMarker_Monitoring]     = SurfaceInvCoeff.CSF[iMarker_Monitoring];
    SurfaceCoeff.CEff[iMarker_Monitoring]    = SurfaceInvCoeff.CL[iMarker_Monitoring] / (SurfaceInvCoeff.CD[iMarker_Monitoring] + EPS);
    SurfaceCoeff.CFx[iMarker_Monitoring]     = SurfaceInvCoeff.CFx[iMarker_Monitoring];
    SurfaceCoeff.CFy[iMarker_Monitoring]     = SurfaceInvCoeff.CFy[iMarker_Monitoring];
    SurfaceCoeff.CFz[iMarker_Monitoring]     = SurfaceInvCoeff.CFz[iMarker_Monitoring];
    SurfaceCoeff.CMx[iMarker_Monitoring]     = SurfaceInvCoeff.CMx[iMarker_Monitoring];
    SurfaceCoeff.CMy[iMarker_Monitoring]     = SurfaceInvCoeff.CMy[iMarker_Monitoring];
    SurfaceCoeff.CMz[iMarker_Monitoring]     = SurfaceInvCoeff.CMz[iMarker_Monitoring];
  }

}

template<class V, ENUM_REGIME FlowRegime>
void CFVMFlowSolverBase<V,FlowRegime>::Momentum_Forces(const CGeometry* geometry, const CConfig* config) {

  unsigned long iVertex, iPoint;
  unsigned short iDim, iMarker, Boundary, Monitoring, iMarker_Monitoring;
  su2double Area, factor, RefVel2 = 0.0, RefTemp, RefDensity = 0.0,  Mach2Vel, Mach_Motion, MassFlow, Density;
  const su2double *Normal = nullptr, *Coord = nullptr;
  string Marker_Tag, Monitoring_Tag;
  su2double AxiFactor;

  su2double Alpha = config->GetAoA()*PI_NUMBER/180.0;
  su2double Beta = config->GetAoS()*PI_NUMBER/180.0;
  su2double RefArea = config->GetRefArea();
  su2double RefLength = config->GetRefLength();
  su2double Gas_Constant = config->GetGas_ConstantND();
  auto Origin = config->GetRefOriginMoment(0);
  bool axisymmetric = config->GetAxisymmetric();

  /// TODO: Move these ifs to specialized functions.

  if (FlowRegime == COMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dynamic meshes, use the motion Mach number as a reference value
     for computing the force coefficients. Otherwise, use the freestream values,
     which is the standard convention. ---*/

    RefTemp = Temperature_Inf;
    RefDensity = Density_Inf;
    if (dynamic_grid) {
      Mach2Vel = sqrt(Gamma*Gas_Constant*RefTemp);
      Mach_Motion = config->GetMach_Motion();
      RefVel2 = (Mach_Motion*Mach2Vel)*(Mach_Motion*Mach2Vel);
    } else {
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
  }

  if (FlowRegime == INCOMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dimensional or non-dim based on initial values, use
     the far-field state (inf). For a custom non-dim based
     on user-provided reference values, use the ref values
     to compute the forces. ---*/

    if ((config->GetRef_Inc_NonDim() == DIMENSIONAL) ||
        (config->GetRef_Inc_NonDim() == INITIAL_VALUES)) {
      RefDensity  = Density_Inf;
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
    else if (config->GetRef_Inc_NonDim() == REFERENCE_VALUES) {
      RefDensity = config->GetInc_Density_Ref();
      RefVel2    = config->GetInc_Velocity_Ref()*config->GetInc_Velocity_Ref();
    }
  }

  factor = 1.0 / (0.5*RefDensity*RefArea*RefVel2);

  /*-- Variables initialization ---*/

  AllBoundMntCoeff.setZero();
  SurfaceMntCoeff.setZero();

  /*--- Loop over the Inlet -Outlet Markers  ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Boundary   = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    /*--- Obtain the origin for the moment computation for a particular marker ---*/

    if (Monitoring == YES) {
      for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
        Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
        Marker_Tag = config->GetMarker_All_TagBound(iMarker);
        if (Marker_Tag == Monitoring_Tag)
          Origin = config->GetRefOriginMoment(iMarker_Monitoring);
      }
    }

    if ((Boundary == INLET_FLOW) || (Boundary == OUTLET_FLOW) ||
        (Boundary == ACTDISK_INLET) || (Boundary == ACTDISK_OUTLET)||
        (Boundary == ENGINE_INFLOW) || (Boundary == ENGINE_EXHAUST)) {

      /*--- Forces initialization at each Marker ---*/

      MntCoeff.setZero(iMarker);

      su2double ForceMomentum[MAXNDIM] = {0.0}, MomentMomentum[MAXNDIM] = {0.0};
      su2double MomentX_Force[3] = {0.0}, MomentY_Force[3] = {0.0}, MomentZ_Force[3] = {0.0};

      /*--- Loop over the vertices to compute the forces ---*/

      for (iVertex = 0; iVertex < geometry->GetnVertex(iMarker); iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();

        /*--- Note that the pressure coefficient is computed at the
         halo cells (for visualization purposes), but not the forces ---*/

        if ( (geometry->nodes->GetDomain(iPoint)) && (Monitoring == YES) ) {

          Normal = geometry->vertex[iMarker][iVertex]->GetNormal();
          Coord = geometry->nodes->GetCoord(iPoint);
          Density   = nodes->GetDensity(iPoint);

          Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);

          MassFlow = 0.0;
          su2double Velocity[MAXNDIM] = {0.0}, MomentDist[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            Velocity[iDim]  = nodes->GetVelocity(iPoint,iDim);
            MomentDist[iDim] = Coord[iDim] - Origin[iDim];
            MassFlow -= Normal[iDim]*Velocity[iDim]*Density;
          }

          /*--- Axisymmetric simulations ---*/

          if (axisymmetric) AxiFactor = 2.0*PI_NUMBER*geometry->nodes->GetCoord(iPoint, 1);
          else AxiFactor = 1.0;

          /*--- Force computation, note the minus sign due to the
           orientation of the normal (outward) ---*/

          su2double Force[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            Force[iDim] = MassFlow * Velocity[iDim] * factor * AxiFactor;
            ForceMomentum[iDim] += Force[iDim];
          }

          /*--- Moment with respect to the reference axis ---*/

          if (iDim == 3) {
            MomentMomentum[0] += (Force[2]*MomentDist[1]-Force[1]*MomentDist[2])/RefLength;
            MomentX_Force[1]  += (-Force[1]*Coord[2]);
            MomentX_Force[2]  += (Force[2]*Coord[1]);

            MomentMomentum[1] += (Force[0]*MomentDist[2]-Force[2]*MomentDist[0])/RefLength;
            MomentY_Force[2]  += (-Force[2]*Coord[0]);
            MomentY_Force[0]  += (Force[0]*Coord[2]);
          }
          MomentMomentum[2] += (Force[1]*MomentDist[0]-Force[0]*MomentDist[1])/RefLength;
          MomentZ_Force[0]  += (-Force[0]*Coord[1]);
          MomentZ_Force[1]  += (Force[1]*Coord[0]);

        }

      }

      /*--- Project forces and store the non-dimensional coefficients ---*/

      if (Monitoring == YES) {

        if (nDim == 2) {
          MntCoeff.CD[iMarker]     =  ForceMomentum[0]*cos(Alpha) + ForceMomentum[1]*sin(Alpha);
          MntCoeff.CL[iMarker]     = -ForceMomentum[0]*sin(Alpha) + ForceMomentum[1]*cos(Alpha);
          MntCoeff.CEff[iMarker]   = MntCoeff.CL[iMarker] / (MntCoeff.CD[iMarker]+EPS);
          MntCoeff.CFx[iMarker]    = ForceMomentum[0];
          MntCoeff.CFy[iMarker]    = ForceMomentum[1];
          MntCoeff.CMz[iMarker]    = MomentMomentum[2];
          MntCoeff.CoPx[iMarker]   = MomentZ_Force[1];
          MntCoeff.CoPy[iMarker]   = -MomentZ_Force[0];
          MntCoeff.CT[iMarker]     = -MntCoeff.CFx[iMarker];
          MntCoeff.CQ[iMarker]     = -MntCoeff.CMz[iMarker];
          MntCoeff.CMerit[iMarker] = MntCoeff.CT[iMarker] / (MntCoeff.CQ[iMarker] + EPS);
        }
        if (nDim == 3) {
          MntCoeff.CD[iMarker]         =  ForceMomentum[0]*cos(Alpha)*cos(Beta) + ForceMomentum[1]*sin(Beta) + ForceMomentum[2]*sin(Alpha)*cos(Beta);
          MntCoeff.CL[iMarker]         = -ForceMomentum[0]*sin(Alpha) + ForceMomentum[2]*cos(Alpha);
          MntCoeff.CSF[iMarker]        = -ForceMomentum[0]*sin(Beta)*cos(Alpha) + ForceMomentum[1]*cos(Beta) - ForceMomentum[2]*sin(Beta)*sin(Alpha);
          MntCoeff.CEff[iMarker]       = MntCoeff.CL[iMarker] / (MntCoeff.CD[iMarker] + EPS);
          MntCoeff.CFx[iMarker]        = ForceMomentum[0];
          MntCoeff.CFy[iMarker]        = ForceMomentum[1];
          MntCoeff.CFz[iMarker]        = ForceMomentum[2];
          MntCoeff.CMx[iMarker]        = MomentMomentum[0];
          MntCoeff.CMy[iMarker]        = MomentMomentum[1];
          MntCoeff.CMz[iMarker]        = MomentMomentum[2];
          MntCoeff.CoPx[iMarker]       = -MomentY_Force[0];
          MntCoeff.CoPz[iMarker]       =  MomentY_Force[2];
          MntCoeff.CT[iMarker]         = -MntCoeff.CFz[iMarker];
          MntCoeff.CQ[iMarker]         = -MntCoeff.CMz[iMarker];
          MntCoeff.CMerit[iMarker]     = MntCoeff.CT[iMarker] / (MntCoeff.CQ[iMarker] + EPS);
        }

        AllBoundMntCoeff.CD           += MntCoeff.CD[iMarker];
        AllBoundMntCoeff.CL           += MntCoeff.CL[iMarker];
        AllBoundMntCoeff.CSF          += MntCoeff.CSF[iMarker];
        AllBoundMntCoeff.CEff          = AllBoundMntCoeff.CL / (AllBoundMntCoeff.CD + EPS);
        AllBoundMntCoeff.CFx          += MntCoeff.CFx[iMarker];
        AllBoundMntCoeff.CFy          += MntCoeff.CFy[iMarker];
        AllBoundMntCoeff.CFz          += MntCoeff.CFz[iMarker];
        AllBoundMntCoeff.CMx          += MntCoeff.CMx[iMarker];
        AllBoundMntCoeff.CMy          += MntCoeff.CMy[iMarker];
        AllBoundMntCoeff.CMx          += MntCoeff.CMz[iMarker];
        AllBoundMntCoeff.CoPx         += MntCoeff.CoPx[iMarker];
        AllBoundMntCoeff.CoPy         += MntCoeff.CoPy[iMarker];
        AllBoundMntCoeff.CoPz         += MntCoeff.CoPz[iMarker];
        AllBoundMntCoeff.CT           += MntCoeff.CT[iMarker];
        AllBoundMntCoeff.CQ           += MntCoeff.CQ[iMarker];
        AllBoundMntCoeff.CMerit       += AllBoundMntCoeff.CT / (AllBoundMntCoeff.CQ + EPS);

        /*--- Compute the coefficients per surface ---*/

        for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
          Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
          Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if (Marker_Tag == Monitoring_Tag) {
            SurfaceMntCoeff.CL[iMarker_Monitoring]      += MntCoeff.CL[iMarker];
            SurfaceMntCoeff.CD[iMarker_Monitoring]      += MntCoeff.CD[iMarker];
            SurfaceMntCoeff.CSF[iMarker_Monitoring]     += MntCoeff.CSF[iMarker];
            SurfaceMntCoeff.CEff[iMarker_Monitoring]     = MntCoeff.CL[iMarker] / (MntCoeff.CD[iMarker] + EPS);
            SurfaceMntCoeff.CFx[iMarker_Monitoring]     += MntCoeff.CFx[iMarker];
            SurfaceMntCoeff.CFy[iMarker_Monitoring]     += MntCoeff.CFy[iMarker];
            SurfaceMntCoeff.CFz[iMarker_Monitoring]     += MntCoeff.CFz[iMarker];
            SurfaceMntCoeff.CMx[iMarker_Monitoring]     += MntCoeff.CMx[iMarker];
            SurfaceMntCoeff.CMy[iMarker_Monitoring]     += MntCoeff.CMy[iMarker];
            SurfaceMntCoeff.CMz[iMarker_Monitoring]     += MntCoeff.CMz[iMarker];
          }
        }

      }

    }
  }

#ifdef HAVE_MPI

  /*--- Add AllBound information using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    auto Allreduce = [](su2double x) {
      su2double tmp = x; x = 0.0;
      SU2_MPI::Allreduce(&tmp, &x, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      return x;
    };

    AllBoundMntCoeff.CD = Allreduce(AllBoundMntCoeff.CD);
    AllBoundMntCoeff.CL = Allreduce(AllBoundMntCoeff.CL);
    AllBoundMntCoeff.CSF = Allreduce(AllBoundMntCoeff.CSF);
    AllBoundMntCoeff.CEff = AllBoundMntCoeff.CL / (AllBoundMntCoeff.CD + EPS);

    AllBoundMntCoeff.CFx = Allreduce(AllBoundMntCoeff.CFx);
    AllBoundMntCoeff.CFy = Allreduce(AllBoundMntCoeff.CFy);
    AllBoundMntCoeff.CFz = Allreduce(AllBoundMntCoeff.CFz);

    AllBoundMntCoeff.CMx = Allreduce(AllBoundMntCoeff.CMx);
    AllBoundMntCoeff.CMy = Allreduce(AllBoundMntCoeff.CMy);
    AllBoundMntCoeff.CMz = Allreduce(AllBoundMntCoeff.CMz);

    AllBoundMntCoeff.CoPx = Allreduce(AllBoundMntCoeff.CoPx);
    AllBoundMntCoeff.CoPy = Allreduce(AllBoundMntCoeff.CoPy);
    AllBoundMntCoeff.CoPz = Allreduce(AllBoundMntCoeff.CoPz);

    AllBoundMntCoeff.CT = Allreduce(AllBoundMntCoeff.CT);
    AllBoundMntCoeff.CQ = Allreduce(AllBoundMntCoeff.CQ);
    AllBoundMntCoeff.CMerit = AllBoundMntCoeff.CT / (AllBoundMntCoeff.CQ + EPS);

  }

  /*--- Add the forces on the surfaces using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    int nMarkerMon = config->GetnMarker_Monitoring();

    /*--- Use the same buffer for all reductions. We could avoid the copy back into
     *    the original variable by swaping pointers, but it is safer this way... ---*/

    su2double* buffer = new su2double [nMarkerMon];

    auto Allreduce_inplace = [buffer](int size, su2double* x) {
      SU2_MPI::Allreduce(x, buffer, size, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      for(int i=0; i<size; ++i) x[i] = buffer[i];
    };

    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CL);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CD);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CSF);

    for (iMarker_Monitoring = 0; iMarker_Monitoring < nMarkerMon; iMarker_Monitoring++)
      SurfaceMntCoeff.CEff[iMarker_Monitoring] = SurfaceMntCoeff.CL[iMarker_Monitoring] / (SurfaceMntCoeff.CD[iMarker_Monitoring] + EPS);

    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CFx);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CFy);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CFz);

    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CMx);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CMy);
    Allreduce_inplace(nMarkerMon, SurfaceMntCoeff.CMz);

    delete [] buffer;

  }

#endif

  /*--- Update the total coefficients (note that all the nodes have the same value) ---*/

  TotalCoeff.CD            += AllBoundMntCoeff.CD;
  TotalCoeff.CL            += AllBoundMntCoeff.CL;
  TotalCoeff.CSF           += AllBoundMntCoeff.CSF;
  TotalCoeff.CEff          = TotalCoeff.CL / (TotalCoeff.CD + EPS);
  TotalCoeff.CFx           += AllBoundMntCoeff.CFx;
  TotalCoeff.CFy           += AllBoundMntCoeff.CFy;
  TotalCoeff.CFz           += AllBoundMntCoeff.CFz;
  TotalCoeff.CMx           += AllBoundMntCoeff.CMx;
  TotalCoeff.CMy           += AllBoundMntCoeff.CMy;
  TotalCoeff.CMz           += AllBoundMntCoeff.CMz;
  TotalCoeff.CoPx          += AllBoundMntCoeff.CoPx;
  TotalCoeff.CoPy          += AllBoundMntCoeff.CoPy;
  TotalCoeff.CoPz          += AllBoundMntCoeff.CoPz;
  TotalCoeff.CT            += AllBoundMntCoeff.CT;
  TotalCoeff.CQ            += AllBoundMntCoeff.CQ;
  TotalCoeff.CMerit        = TotalCoeff.CT / (TotalCoeff.CQ + EPS);

  /*--- Update the total coefficients per surface (note that all the nodes have the same value)---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    SurfaceCoeff.CL[iMarker_Monitoring]         += SurfaceMntCoeff.CL[iMarker_Monitoring];
    SurfaceCoeff.CD[iMarker_Monitoring]         += SurfaceMntCoeff.CD[iMarker_Monitoring];
    SurfaceCoeff.CSF[iMarker_Monitoring]        += SurfaceMntCoeff.CSF[iMarker_Monitoring];
    SurfaceCoeff.CEff[iMarker_Monitoring]       += SurfaceMntCoeff.CL[iMarker_Monitoring] / (SurfaceMntCoeff.CD[iMarker_Monitoring] + EPS);
    SurfaceCoeff.CFx[iMarker_Monitoring]        += SurfaceMntCoeff.CFx[iMarker_Monitoring];
    SurfaceCoeff.CFy[iMarker_Monitoring]        += SurfaceMntCoeff.CFy[iMarker_Monitoring];
    SurfaceCoeff.CFz[iMarker_Monitoring]        += SurfaceMntCoeff.CFz[iMarker_Monitoring];
    SurfaceCoeff.CMx[iMarker_Monitoring]        += SurfaceMntCoeff.CMx[iMarker_Monitoring];
    SurfaceCoeff.CMy[iMarker_Monitoring]        += SurfaceMntCoeff.CMy[iMarker_Monitoring];
    SurfaceCoeff.CMz[iMarker_Monitoring]        += SurfaceMntCoeff.CMz[iMarker_Monitoring];
  }

}

template<class V, ENUM_REGIME FlowRegime>
void CFVMFlowSolverBase<V,FlowRegime>::Friction_Forces(const CGeometry* geometry, const CConfig* config) {

  /// TODO: Major cleanup needed.

  unsigned long iVertex, iPoint, iPointNormal;
  unsigned short Boundary, Monitoring, iMarker, iMarker_Monitoring, iDim, jDim;
  su2double Viscosity = 0.0, div_vel, WallDist[3] = {0.0},
  Area, WallShearStress, TauNormal, RefTemp, RefVel2 = 0.0, RefDensity = 0.0, GradTemperature, Density = 0.0, WallDistMod, FrictionVel,
  Mach2Vel, Mach_Motion, UnitNormal[3] = {0.0}, TauElem[3] = {0.0}, TauTangent[3] = {0.0},
  Tau[3][3] = {{0.0}}, Cp, thermal_conductivity, MaxNorm = 8.0,
  Grad_Vel[3][3] = {{0.0}}, Grad_Temp[3] = {0.0},
  delta[3][3] = {{1.0, 0.0, 0.0},{0.0,1.0,0.0},{0.0,0.0,1.0}};
  su2double AxiFactor;
  const su2double *Coord = nullptr, *Coord_Normal = nullptr, *Normal = nullptr;

  string Marker_Tag, Monitoring_Tag;

  su2double Alpha = config->GetAoA()*PI_NUMBER/180.0;
  su2double Beta = config->GetAoS()*PI_NUMBER/180.0;
  su2double RefArea = config->GetRefArea();
  su2double RefLength = config->GetRefLength();
  su2double RefHeatFlux = config->GetHeat_Flux_Ref();
  su2double Gas_Constant = config->GetGas_ConstantND();
  auto Origin = config->GetRefOriginMoment(0);

  su2double Prandtl_Lam = config->GetPrandtl_Lam();
  bool energy = config->GetEnergy_Equation();
  bool QCR = config->GetQCR();
  bool axisymmetric = config->GetAxisymmetric();

  /// TODO: Move these ifs to specialized functions.

  if (FlowRegime == COMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dynamic meshes, use the motion Mach number as a reference value
     for computing the force coefficients. Otherwise, use the freestream values,
     which is the standard convention. ---*/

    RefTemp = Temperature_Inf;
    RefDensity = Density_Inf;
    if (dynamic_grid) {
      Mach2Vel = sqrt(Gamma*Gas_Constant*RefTemp);
      Mach_Motion = config->GetMach_Motion();
      RefVel2 = (Mach_Motion*Mach2Vel)*(Mach_Motion*Mach2Vel);
    } else {
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
  }

  if (FlowRegime == INCOMPRESSIBLE) {
    /*--- Evaluate reference values for non-dimensionalization.
     For dimensional or non-dim based on initial values, use
     the far-field state (inf). For a custom non-dim based
     on user-provided reference values, use the ref values
     to compute the forces. ---*/

    if ((config->GetRef_Inc_NonDim() == DIMENSIONAL) ||
        (config->GetRef_Inc_NonDim() == INITIAL_VALUES)) {
      RefDensity  = Density_Inf;
      RefVel2 = 0.0;
      for (iDim = 0; iDim < nDim; iDim++)
        RefVel2  += Velocity_Inf[iDim]*Velocity_Inf[iDim];
    }
    else if (config->GetRef_Inc_NonDim() == REFERENCE_VALUES) {
      RefDensity = config->GetInc_Density_Ref();
      RefVel2    = config->GetInc_Velocity_Ref()*config->GetInc_Velocity_Ref();
    }
  }

  const su2double factor = 1.0 / (0.5*RefDensity*RefArea*RefVel2);

  /*--- Variables initialization ---*/

  AllBoundViscCoeff.setZero();
  SurfaceViscCoeff.setZero();

  AllBound_HF_Visc = 0.0;  AllBound_MaxHF_Visc = 0.0;

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    Surface_HF_Visc[iMarker_Monitoring]  = 0.0; Surface_MaxHF_Visc[iMarker_Monitoring]   = 0.0;
  }

  /*--- Loop over the Navier-Stokes markers ---*/

  for (iMarker = 0; iMarker < nMarker; iMarker++) {

    Boundary = config->GetMarker_All_KindBC(iMarker);
    Monitoring = config->GetMarker_All_Monitoring(iMarker);

    /*--- Obtain the origin for the moment computation for a particular marker ---*/

    if (Monitoring == YES) {
      for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
        Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
        Marker_Tag = config->GetMarker_All_TagBound(iMarker);
        if (Marker_Tag == Monitoring_Tag)
          Origin = config->GetRefOriginMoment(iMarker_Monitoring);
      }
    }

    if ((Boundary == HEAT_FLUX) || (Boundary == ISOTHERMAL) || (Boundary == CHT_WALL_INTERFACE)) {

      /*--- Forces initialization at each Marker ---*/

      ViscCoeff.setZero(iMarker);

      HF_Visc[iMarker] = 0.0;    MaxHF_Visc[iMarker] = 0.0;

      su2double ForceViscous[MAXNDIM] = {0.0}, MomentViscous[MAXNDIM] = {0.0};
      su2double MomentX_Force[MAXNDIM] = {0.0}, MomentY_Force[MAXNDIM] = {0.0}, MomentZ_Force[MAXNDIM] = {0.0};

      /*--- Loop over the vertices to compute the forces ---*/

      for (iVertex = 0; iVertex < geometry->nVertex[iMarker]; iVertex++) {

        iPoint = geometry->vertex[iMarker][iVertex]->GetNode();
        iPointNormal = geometry->vertex[iMarker][iVertex]->GetNormal_Neighbor();

        Coord = geometry->nodes->GetCoord(iPoint);
        Coord_Normal = geometry->nodes->GetCoord(iPointNormal);

        Normal = geometry->vertex[iMarker][iVertex]->GetNormal();

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Grad_Vel[iDim][jDim] = nodes->GetGradient_Primitive(iPoint,iDim+1, jDim);
          }

          /// TODO: Move the temperature index logic to a function.

          if (FlowRegime == COMPRESSIBLE)
            Grad_Temp[iDim] = nodes->GetGradient_Primitive(iPoint,0, iDim);

          if (FlowRegime == INCOMPRESSIBLE)
            Grad_Temp[iDim] = nodes->GetGradient_Primitive(iPoint,nDim+1, iDim);
        }

        Viscosity = nodes->GetLaminarViscosity(iPoint);
        Density = nodes->GetDensity(iPoint);

        Area = 0.0; for (iDim = 0; iDim < nDim; iDim++) Area += Normal[iDim]*Normal[iDim]; Area = sqrt(Area);

        for (iDim = 0; iDim < nDim; iDim++) {
          UnitNormal[iDim] = Normal[iDim]/Area;
        }

        /*--- Evaluate Tau ---*/

        div_vel = 0.0; for (iDim = 0; iDim < nDim; iDim++) div_vel += Grad_Vel[iDim][iDim];

        for (iDim = 0; iDim < nDim; iDim++) {
          for (jDim = 0 ; jDim < nDim; jDim++) {
            Tau[iDim][jDim] = Viscosity*(Grad_Vel[jDim][iDim] + Grad_Vel[iDim][jDim]) -
                              TWO3*Viscosity*div_vel*delta[iDim][jDim];
          }
        }

        /*--- If necessary evaluate the QCR contribution to Tau ---*/

        if (QCR) {
          su2double den_aux, c_cr1=0.3, O_ik, O_jk;
          unsigned short kDim;

          /*--- Denominator Antisymmetric normalized rotation tensor ---*/

          den_aux = 0.0;
          for (iDim = 0 ; iDim < nDim; iDim++)
            for (jDim = 0 ; jDim < nDim; jDim++)
              den_aux += Grad_Vel[iDim][jDim] * Grad_Vel[iDim][jDim];
          den_aux = sqrt(max(den_aux,1E-10));

          /*--- Adding the QCR contribution ---*/

          su2double tauQCR[MAXNDIM][MAXNDIM] = {{0.0}};

          for (iDim = 0 ; iDim < nDim; iDim++){
            for (jDim = 0 ; jDim < nDim; jDim++){
              for (kDim = 0 ; kDim < nDim; kDim++){
                O_ik = (Grad_Vel[iDim][kDim] - Grad_Vel[kDim][iDim])/ den_aux;
                O_jk = (Grad_Vel[jDim][kDim] - Grad_Vel[kDim][jDim])/ den_aux;
                tauQCR[iDim][jDim] += O_ik * Tau[jDim][kDim] + O_jk * Tau[iDim][kDim];
              }
            }
          }

          for (iDim = 0 ; iDim < nDim; iDim++)
            for (jDim = 0 ; jDim < nDim; jDim++)
              Tau[iDim][jDim] -= c_cr1 * tauQCR[iDim][jDim];
        }

        /*--- Project Tau in each surface element ---*/

        for (iDim = 0; iDim < nDim; iDim++) {
          TauElem[iDim] = 0.0;
          for (jDim = 0; jDim < nDim; jDim++) {
            TauElem[iDim] += Tau[iDim][jDim]*UnitNormal[jDim];
          }
        }

        /*--- Compute wall shear stress (using the stress tensor). Compute wall skin friction coefficient, and heat flux on the wall ---*/

        TauNormal = 0.0; for (iDim = 0; iDim < nDim; iDim++) TauNormal += TauElem[iDim] * UnitNormal[iDim];

        WallShearStress = 0.0;
        for (iDim = 0; iDim < nDim; iDim++) {
          TauTangent[iDim] = TauElem[iDim] - TauNormal * UnitNormal[iDim];
          CSkinFriction[iMarker][iDim][iVertex] = TauTangent[iDim] / (0.5*RefDensity*RefVel2);
          WallShearStress += TauTangent[iDim] * TauTangent[iDim];
        }
        WallShearStress = sqrt(WallShearStress);

        for (iDim = 0; iDim < nDim; iDim++) WallDist[iDim] = (Coord[iDim] - Coord_Normal[iDim]);
        WallDistMod = 0.0; for (iDim = 0; iDim < nDim; iDim++) WallDistMod += WallDist[iDim]*WallDist[iDim]; WallDistMod = sqrt(WallDistMod);

        /*--- Compute y+ and non-dimensional velocity ---*/

        FrictionVel = sqrt(fabs(WallShearStress)/Density);
        YPlus[iMarker][iVertex] = WallDistMod*FrictionVel/(Viscosity/Density);

        /*--- Compute total and maximum heat flux on the wall ---*/

        GradTemperature = 0.0;

        /// TODO: Move these ifs to specialized functions.

        if (FlowRegime == COMPRESSIBLE) {

          for (iDim = 0; iDim < nDim; iDim++)
            GradTemperature -= Grad_Temp[iDim]*UnitNormal[iDim];

          Cp = (Gamma / Gamma_Minus_One) * Gas_Constant;
          thermal_conductivity = Cp * Viscosity/Prandtl_Lam;
        }

        if (FlowRegime == INCOMPRESSIBLE) {

          if (energy)
            for (iDim = 0; iDim < nDim; iDim++)
              GradTemperature -= Grad_Temp[iDim]*UnitNormal[iDim];

          thermal_conductivity = nodes->GetThermalConductivity(iPoint);
        }

        HeatFlux[iMarker][iVertex] = -thermal_conductivity*GradTemperature*RefHeatFlux;


        /*--- Note that y+, and heat are computed at the
         halo cells (for visualization purposes), but not the forces ---*/

        if ((geometry->nodes->GetDomain(iPoint)) && (Monitoring == YES)) {

          /*--- Axisymmetric simulations ---*/

          if (axisymmetric) AxiFactor = 2.0*PI_NUMBER*geometry->nodes->GetCoord(iPoint, 1);
          else AxiFactor = 1.0;

          /*--- Force computation ---*/

          su2double Force[MAXNDIM] = {0.0}, MomentDist[MAXNDIM] = {0.0};
          for (iDim = 0; iDim < nDim; iDim++) {
            Force[iDim] = TauElem[iDim] * Area * factor * AxiFactor;
            ForceViscous[iDim] += Force[iDim];
            MomentDist[iDim] = Coord[iDim] - Origin[iDim];
          }

          /*--- Moment with respect to the reference axis ---*/

          if (iDim == 3) {
            MomentViscous[0] += (Force[2]*MomentDist[1] - Force[1]*MomentDist[2])/RefLength;
            MomentX_Force[1] += (-Force[1]*Coord[2]);
            MomentX_Force[2] += (Force[2]*Coord[1]);

            MomentViscous[1] += (Force[0]*MomentDist[2] - Force[2]*MomentDist[0])/RefLength;
            MomentY_Force[2] += (-Force[2]*Coord[0]);
            MomentY_Force[0] += (Force[0]*Coord[2]);
          }
          MomentViscous[2] += (Force[1]*MomentDist[0] - Force[0]*MomentDist[1])/RefLength;
          MomentZ_Force[0] += (-Force[0]*Coord[1]);
          MomentZ_Force[1] += (Force[1]*Coord[0]);

          HF_Visc[iMarker]          += HeatFlux[iMarker][iVertex]*Area;
          MaxHF_Visc[iMarker]       += pow(HeatFlux[iMarker][iVertex], MaxNorm);
        }
      }

      /*--- Project forces and store the non-dimensional coefficients ---*/

      if (Monitoring == YES) {
        if (nDim == 2) {
          ViscCoeff.CD[iMarker]          =  ForceViscous[0]*cos(Alpha) + ForceViscous[1]*sin(Alpha);
          ViscCoeff.CL[iMarker]          = -ForceViscous[0]*sin(Alpha) + ForceViscous[1]*cos(Alpha);
          ViscCoeff.CEff[iMarker]        = ViscCoeff.CL[iMarker] / (ViscCoeff.CD[iMarker]+EPS);
          ViscCoeff.CFx[iMarker]         = ForceViscous[0];
          ViscCoeff.CFy[iMarker]         = ForceViscous[1];
          ViscCoeff.CMz[iMarker]         = MomentViscous[2];
          ViscCoeff.CoPx[iMarker]        = MomentZ_Force[1];
          ViscCoeff.CoPy[iMarker]        = -MomentZ_Force[0];
          ViscCoeff.CT[iMarker]          = -ViscCoeff.CFx[iMarker];
          ViscCoeff.CQ[iMarker]          = -ViscCoeff.CMz[iMarker];
          ViscCoeff.CMerit[iMarker]      = ViscCoeff.CT[iMarker] / (ViscCoeff.CQ[iMarker]+EPS);
          MaxHF_Visc[iMarker]            = pow(MaxHF_Visc[iMarker], 1.0/MaxNorm);
        }
        if (nDim == 3) {
          ViscCoeff.CD[iMarker]          =  ForceViscous[0]*cos(Alpha)*cos(Beta) + ForceViscous[1]*sin(Beta) + ForceViscous[2]*sin(Alpha)*cos(Beta);
          ViscCoeff.CL[iMarker]          = -ForceViscous[0]*sin(Alpha) + ForceViscous[2]*cos(Alpha);
          ViscCoeff.CSF[iMarker]         = -ForceViscous[0]*sin(Beta)*cos(Alpha) + ForceViscous[1]*cos(Beta) - ForceViscous[2]*sin(Beta)*sin(Alpha);
          ViscCoeff.CEff[iMarker]        = ViscCoeff.CL[iMarker]/(ViscCoeff.CD[iMarker] + EPS);
          ViscCoeff.CFx[iMarker]         = ForceViscous[0];
          ViscCoeff.CFy[iMarker]         = ForceViscous[1];
          ViscCoeff.CFz[iMarker]         = ForceViscous[2];
          ViscCoeff.CMx[iMarker]         = MomentViscous[0];
          ViscCoeff.CMy[iMarker]         = MomentViscous[1];
          ViscCoeff.CMz[iMarker]         = MomentViscous[2];
          ViscCoeff.CoPx[iMarker]        = -MomentY_Force[0];
          ViscCoeff.CoPz[iMarker]        = MomentY_Force[2];
          ViscCoeff.CT[iMarker]          = -ViscCoeff.CFz[iMarker];
          ViscCoeff.CQ[iMarker]          = -ViscCoeff.CMz[iMarker];
          ViscCoeff.CMerit[iMarker]      = ViscCoeff.CT[iMarker] / (ViscCoeff.CQ[iMarker] + EPS);
          MaxHF_Visc[iMarker]            = pow(MaxHF_Visc[iMarker], 1.0/MaxNorm);
        }

        AllBoundViscCoeff.CD          += ViscCoeff.CD[iMarker];
        AllBoundViscCoeff.CL          += ViscCoeff.CL[iMarker];
        AllBoundViscCoeff.CSF         += ViscCoeff.CSF[iMarker];
        AllBoundViscCoeff.CFx         += ViscCoeff.CFx[iMarker];
        AllBoundViscCoeff.CFy         += ViscCoeff.CFy[iMarker];
        AllBoundViscCoeff.CFz         += ViscCoeff.CFz[iMarker];
        AllBoundViscCoeff.CMx         += ViscCoeff.CMx[iMarker];
        AllBoundViscCoeff.CMy         += ViscCoeff.CMy[iMarker];
        AllBoundViscCoeff.CMz         += ViscCoeff.CMz[iMarker];
        AllBoundViscCoeff.CoPx        += ViscCoeff.CoPx[iMarker];
        AllBoundViscCoeff.CoPy        += ViscCoeff.CoPy[iMarker];
        AllBoundViscCoeff.CoPz        += ViscCoeff.CoPz[iMarker];
        AllBoundViscCoeff.CT          += ViscCoeff.CT[iMarker];
        AllBoundViscCoeff.CQ          += ViscCoeff.CQ[iMarker];
        AllBound_HF_Visc              += HF_Visc[iMarker];
        AllBound_MaxHF_Visc           += pow(MaxHF_Visc[iMarker], MaxNorm);

        /*--- Compute the coefficients per surface ---*/

        for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
          Monitoring_Tag = config->GetMarker_Monitoring_TagBound(iMarker_Monitoring);
          Marker_Tag = config->GetMarker_All_TagBound(iMarker);
          if (Marker_Tag == Monitoring_Tag) {
            SurfaceViscCoeff.CL[iMarker_Monitoring]      += ViscCoeff.CL[iMarker];
            SurfaceViscCoeff.CD[iMarker_Monitoring]      += ViscCoeff.CD[iMarker];
            SurfaceViscCoeff.CSF[iMarker_Monitoring]     += ViscCoeff.CSF[iMarker];
            SurfaceViscCoeff.CEff[iMarker_Monitoring]    += ViscCoeff.CEff[iMarker];
            SurfaceViscCoeff.CFx[iMarker_Monitoring]     += ViscCoeff.CFx[iMarker];
            SurfaceViscCoeff.CFy[iMarker_Monitoring]     += ViscCoeff.CFy[iMarker];
            SurfaceViscCoeff.CFz[iMarker_Monitoring]     += ViscCoeff.CFz[iMarker];
            SurfaceViscCoeff.CMx[iMarker_Monitoring]     += ViscCoeff.CMx[iMarker];
            SurfaceViscCoeff.CMy[iMarker_Monitoring]     += ViscCoeff.CMy[iMarker];
            SurfaceViscCoeff.CMz[iMarker_Monitoring]     += ViscCoeff.CMz[iMarker];
            Surface_HF_Visc[iMarker_Monitoring]          += HF_Visc[iMarker];
            Surface_MaxHF_Visc[iMarker_Monitoring]       += pow(MaxHF_Visc[iMarker],MaxNorm);
          }
        }
      }
    }
  }

  /*--- Update some global coeffients ---*/

  AllBoundViscCoeff.CEff = AllBoundViscCoeff.CL / (AllBoundViscCoeff.CD + EPS);
  AllBoundViscCoeff.CMerit = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);
  AllBound_MaxHF_Visc = pow(AllBound_MaxHF_Visc, 1.0/MaxNorm);


#ifdef HAVE_MPI

  /*--- Add AllBound information using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    auto Allreduce = [](su2double x) {
      su2double tmp = x; x = 0.0;
      SU2_MPI::Allreduce(&tmp, &x, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      return x;
    };
    AllBoundViscCoeff.CD = Allreduce(AllBoundViscCoeff.CD);
    AllBoundViscCoeff.CL = Allreduce(AllBoundViscCoeff.CL);
    AllBoundViscCoeff.CSF = Allreduce(AllBoundViscCoeff.CSF);
    AllBoundViscCoeff.CEff = AllBoundViscCoeff.CL / (AllBoundViscCoeff.CD + EPS);

    AllBoundViscCoeff.CMx = Allreduce(AllBoundViscCoeff.CMx);
    AllBoundViscCoeff.CMy = Allreduce(AllBoundViscCoeff.CMy);
    AllBoundViscCoeff.CMz = Allreduce(AllBoundViscCoeff.CMz);

    AllBoundViscCoeff.CFx = Allreduce(AllBoundViscCoeff.CFx);
    AllBoundViscCoeff.CFy = Allreduce(AllBoundViscCoeff.CFy);
    AllBoundViscCoeff.CFz = Allreduce(AllBoundViscCoeff.CFz);

    AllBoundViscCoeff.CoPx = Allreduce(AllBoundViscCoeff.CoPx);
    AllBoundViscCoeff.CoPy = Allreduce(AllBoundViscCoeff.CoPy);
    AllBoundViscCoeff.CoPz = Allreduce(AllBoundViscCoeff.CoPz);

    AllBoundViscCoeff.CT = Allreduce(AllBoundViscCoeff.CT);
    AllBoundViscCoeff.CQ = Allreduce(AllBoundViscCoeff.CQ);
    AllBoundViscCoeff.CMerit = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);

    AllBound_HF_Visc = Allreduce(AllBound_HF_Visc);
    AllBound_MaxHF_Visc = pow(Allreduce(pow(AllBound_MaxHF_Visc, MaxNorm)), 1.0/MaxNorm);

  }

  /*--- Add the forces on the surfaces using all the nodes ---*/

  if (config->GetComm_Level() == COMM_FULL) {

    int nMarkerMon = config->GetnMarker_Monitoring();

    /*--- Use the same buffer for all reductions. We could avoid the copy back into
     *    the original variable by swaping pointers, but it is safer this way... ---*/

    su2double* buffer = new su2double [nMarkerMon];

    auto Allreduce_inplace = [buffer](int size, su2double* x) {
      SU2_MPI::Allreduce(x, buffer, size, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      for(int i=0; i<size; ++i) x[i] = buffer[i];
    };

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CL);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CD);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CSF);

    for (iMarker_Monitoring = 0; iMarker_Monitoring < nMarkerMon; iMarker_Monitoring++)
      SurfaceViscCoeff.CEff[iMarker_Monitoring] = SurfaceViscCoeff.CL[iMarker_Monitoring] /
                                                 (SurfaceViscCoeff.CD[iMarker_Monitoring] + EPS);

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFx);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFy);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CFz);

    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMx);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMy);
    Allreduce_inplace(nMarkerMon, SurfaceViscCoeff.CMz);

    Allreduce_inplace(nMarkerMon, Surface_HF_Visc);
    Allreduce_inplace(nMarkerMon, Surface_MaxHF_Visc);

    delete [] buffer;

  }

#endif

  /*--- Update the total coefficients (note that all the nodes have the same value)---*/

  TotalCoeff.CD          += AllBoundViscCoeff.CD;
  TotalCoeff.CL          += AllBoundViscCoeff.CL;
  TotalCoeff.CSF         += AllBoundViscCoeff.CSF;
  TotalCoeff.CEff         = TotalCoeff.CL / (TotalCoeff.CD + EPS);
  TotalCoeff.CFx         += AllBoundViscCoeff.CFx;
  TotalCoeff.CFy         += AllBoundViscCoeff.CFy;
  TotalCoeff.CFz         += AllBoundViscCoeff.CFz;
  TotalCoeff.CMx         += AllBoundViscCoeff.CMx;
  TotalCoeff.CMy         += AllBoundViscCoeff.CMy;
  TotalCoeff.CMz         += AllBoundViscCoeff.CMz;
  TotalCoeff.CoPx        += AllBoundViscCoeff.CoPx;
  TotalCoeff.CoPy        += AllBoundViscCoeff.CoPy;
  TotalCoeff.CoPz        += AllBoundViscCoeff.CoPz;
  TotalCoeff.CT          += AllBoundViscCoeff.CT;
  TotalCoeff.CQ          += AllBoundViscCoeff.CQ;
  TotalCoeff.CMerit       = AllBoundViscCoeff.CT / (AllBoundViscCoeff.CQ + EPS);
  Total_Heat         = AllBound_HF_Visc;
  Total_MaxHeat      = AllBound_MaxHF_Visc;

  /*--- Update the total coefficients per surface (note that all the nodes have the same value)---*/

  for (iMarker_Monitoring = 0; iMarker_Monitoring < config->GetnMarker_Monitoring(); iMarker_Monitoring++) {
    SurfaceCoeff.CL[iMarker_Monitoring]         += SurfaceViscCoeff.CL[iMarker_Monitoring];
    SurfaceCoeff.CD[iMarker_Monitoring]         += SurfaceViscCoeff.CD[iMarker_Monitoring];
    SurfaceCoeff.CSF[iMarker_Monitoring]        += SurfaceViscCoeff.CSF[iMarker_Monitoring];
    SurfaceCoeff.CEff[iMarker_Monitoring]        = SurfaceViscCoeff.CL[iMarker_Monitoring] / (SurfaceCoeff.CD[iMarker_Monitoring] + EPS);
    SurfaceCoeff.CFx[iMarker_Monitoring]        += SurfaceViscCoeff.CFx[iMarker_Monitoring];
    SurfaceCoeff.CFy[iMarker_Monitoring]        += SurfaceViscCoeff.CFy[iMarker_Monitoring];
    SurfaceCoeff.CFz[iMarker_Monitoring]        += SurfaceViscCoeff.CFz[iMarker_Monitoring];
    SurfaceCoeff.CMx[iMarker_Monitoring]        += SurfaceViscCoeff.CMx[iMarker_Monitoring];
    SurfaceCoeff.CMy[iMarker_Monitoring]        += SurfaceViscCoeff.CMy[iMarker_Monitoring];
    SurfaceCoeff.CMz[iMarker_Monitoring]        += SurfaceViscCoeff.CMz[iMarker_Monitoring];
  }

}