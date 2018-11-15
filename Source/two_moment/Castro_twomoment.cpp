#include "Castro.H"
#include "AMReX_ParmParse.H"
#include "TwoMoment_F.H"
#include <AMReX_MultiFabUtil.H>
#include <AMReX_MultiFabUtil_F.H>

using std::string;
using namespace amrex;

void
Castro::read_thornado_params ()
{
  ParmParse pp("thornado");

  pp.query("eL"    , thornado_eL);
  pp.query("eR"    , thornado_eR);
  pp.query("ndimse", thornado_ndimse);
  pp.query("zoome" , thornado_zoome);
  pp.query("swE"   , thornado_swE);
}

int
Castro::init_thornado()
{
    int nDimsX   = BL_SPACEDIM;
    int nDimsE   = thornado_ndimse; // number of energy groups in thornado
    int nSpecies = THORNADO_NSPECIES;

    Real zoomE    = thornado_zoome;  // geometric zoom factor for energy groups

    int swE = thornado_swE;
    Real eL = thornado_eL;
    Real eR = thornado_eR;

    // Note these are in units of MeV
    amrex::Print() << "*****Calling InitThornado with eL and eR = " << thornado_eL << " " << thornado_eR << std::endl;
    InitThornado(&nDimsX, &nDimsE, &swE, &eL, &eR, &zoomE, &nSpecies);

    int ncomp_thornado;

    ca_get_rad_ncomp(&ncomp_thornado);

    return ncomp_thornado;
}

void
Castro::init_thornado_data()
{
    MultiFab& Fluid_new = get_new_data(State_Type);
    MultiFab&  Thor_new = get_new_data(Thornado_Type);

    int my_ngrow = 2;  // two fluid ghost cells

    const Real* dx = geom.CellSize();

// *************************************************************
    Vector<Real> grid_lo(3);
    Vector<Real> grid_hi(3);

    int * boxlen = new int[3];
    const Real* prob_lo   = geom.ProbLo();

    int swX[3];
    swX[0] = my_ngrow;
    swX[1] = my_ngrow;
#if (BL_SPACEDIM > 2)
    swX[2] = my_ngrow;
#else
    swX[2] = 0;
#endif

    int nr =  Thor_new.nComp();
    const Real  cur_time = state[Thornado_Type].curTime();

    // For now we will not allowing logical tiling
    for (MFIter mfi(Fluid_new, false); mfi.isValid(); ++mfi) 
    {
        Box bx = mfi.validbox();

        grid_lo[0] = prob_lo[0] +  bx.smallEnd(0)  * dx[0] / 100.0; // Factor of 100 because Thornado uses m, not cm
        grid_hi[0] = prob_lo[0] + (bx.bigEnd(0)+1) * dx[0] / 100.0;
        boxlen[0] = bx.length(0);

        grid_lo[1] = prob_lo[1] +  bx.smallEnd(1)  * dx[1] / 100.0;
        grid_hi[1] = prob_lo[1] + (bx.bigEnd(1)+1) * dx[1] / 100.0;
        boxlen[1] = bx.length(1);

#if (BL_SPACEDIM > 2)
        grid_lo[2] = prob_lo[2] +  bx.smallEnd(2)  * dx[2] / 100.0;
        grid_hi[2] = prob_lo[2] + (bx.bigEnd(2)+1) * dx[2] / 100.0;
        boxlen[2] = bx.length(2);
#else
        grid_lo[2] = 0.;
        grid_hi[2] = 1.;
        boxlen[2]  = 1;
#endif

        InitThornado_Patch(boxlen, swX, grid_lo.dataPtr(), grid_hi.dataPtr());

        RealBox gridloc = RealBox(grids[mfi.index()],geom.CellSize(),geom.ProbLo());
        const int* lo      = bx.loVect();
        const int* hi      = bx.hiVect();
  
        ca_init_thornado_data
	  (level, cur_time, lo, hi,
           nr, BL_TO_FORTRAN(Thor_new[mfi]), 
               BL_TO_FORTRAN(Fluid_new[mfi]), 
           dx, gridloc.lo(), gridloc.hi());

        FreeThornado_Patch();
    }
    delete boxlen;
}

void
Castro::average_down_thornado_data(const MultiFab& S_fine, MultiFab& S_crse, int ncomp, 
                                   const IntVect& ratio)
{
        AMREX_ASSERT(S_crse.nComp() == S_fine.nComp());
        AMREX_ASSERT((S_crse.is_cell_centered() && S_fine.is_cell_centered()));

        bool is_cell_centered = S_crse.is_cell_centered();

        const int* ratioV = ratio.getVect();

        int n_rad_dof   = THORNADO_RAD_NDOF;
        int n_species   = THORNADO_NSPECIES;
        int n_moments   = THORNADO_NMOMENTS;
        int n_energy_bins = thornado_ndimse;
        
        //
        // Coarsen() the fine stuff on processors owning the fine data.
        //
        BoxArray crse_S_fine_BA = S_fine.boxArray(); crse_S_fine_BA.coarsen(ratio);
        
        if (crse_S_fine_BA == S_crse.boxArray() and S_fine.DistributionMap() == S_crse.DistributionMap())
        {
#ifdef _OPENMP
#pragma omp parallel
#endif
            for (MFIter mfi(S_crse,true); mfi.isValid(); ++mfi)
            {
                //  NOTE: The tilebox is defined at the coarse level.
                const Box& tbx = mfi.tilebox();

                const int* cblo = tbx.loVect();
                const int* cbhi = tbx.hiVect();

                ca_dg_coarsen(AMREX_ARLIM_ANYD(cblo), AMREX_ARLIM_ANYD(cbhi),
                              BL_TO_FORTRAN_ANYD(S_fine[mfi]),
                              BL_TO_FORTRAN_ANYD(S_crse[mfi]),
                              AMREX_ARLIM_ANYD(ratioV),
                              &ncomp, &n_rad_dof, &n_species, &n_moments, &n_energy_bins);
            }
        }
        else
        {
            MultiFab crse_S_fine(crse_S_fine_BA, S_fine.DistributionMap(), ncomp, 0, MFInfo(), FArrayBoxFactory());

#ifdef _OPENMP
#pragma omp parallel
#endif
            for (MFIter mfi(crse_S_fine,true); mfi.isValid(); ++mfi)
            {
                //  NOTE: The tilebox is defined at the coarse level.
                const Box& tbx = mfi.tilebox();

                const int* cblo = tbx.loVect();
                const int* cbhi = tbx.hiVect();

                ca_dg_coarsen(AMREX_ARLIM_ANYD(cblo), AMREX_ARLIM_ANYD(cbhi),
                              BL_TO_FORTRAN_ANYD(S_fine[mfi]),
                              BL_TO_FORTRAN_ANYD(crse_S_fine[mfi]),
                              AMREX_ARLIM_ANYD(ratioV),
                              &ncomp, &n_rad_dof, &n_species, &n_moments, &n_energy_bins);

            }
            
            S_crse.copy(crse_S_fine,0,0,ncomp);
        }
}


void
Castro::create_thornado_source(Real dt)
{

    MultiFab& S_new = get_new_data(State_Type);

    MultiFab& U_R_old = get_old_data(Thornado_Type);
    MultiFab& U_R_new = get_new_data(Thornado_Type);

    // Need to copy old into new because we haven't define U_R_new yet 
    MultiFab::Copy(U_R_new,U_R_old,0,0,U_R_new.nComp(),U_R_new.nGrow());

    // The StateData Thornado_Fluid_Source_Type will hold the entire contribution
    //   for this time step
    MultiFab& dS_old = get_old_data(Thornado_Fluid_Source_Type);
    MultiFab& dS_new = get_new_data(Thornado_Fluid_Source_Type);
    dS_old.setVal(0.);
    dS_new.setVal(0.);

    MultiFab& dR_old = get_old_data(Thornado_Rad_Source_Type);
    MultiFab& dR_new = get_new_data(Thornado_Rad_Source_Type);
    dR_old.setVal(0.);
    dR_new.setVal(0.);

    // The temporary MultiFabs will hold the contribution for each substep
    MultiFab dS(grids, dmap,  dS_new.nComp(),  dS_new.nGrow());
    MultiFab dR(grids, dmap, U_R_new.nComp(), U_R_new.nGrow());

    // We only actually compute the source at level 0; otherwise we interpolate from 
    //    the coarser level
    if (level == 0)
    {
       int my_ngrow = 2;  // two fluid ghost cells

       // This fills the ghost cells of the fluid MultiFab which we will pass into Thornado
       MultiFab S_border(grids, dmap, NUM_STATE, my_ngrow+1);
       const Real  prev_time = state[State_Type].prevTime();
       AmrLevel::FillPatch(*this, S_border, my_ngrow+1, prev_time, State_Type, 0, NUM_STATE);

       // This fills the ghost cells of the radiation MultiFab which we will pass into Thornado
       MultiFab R_border(grids, dmap, U_R_old.nComp(), my_ngrow);
       AmrLevel::FillPatch(*this, R_border, my_ngrow, prev_time, Thornado_Type, 0, U_R_old.nComp());

       const Real* dx = geom.CellSize();

       // int n_sub = GetNSteps(dt); // From thornado
       Real dt_CGS;
       compute_thornado_timestep(dx, dt_CGS );
       std::cout << "DT " << dt << " " << dt_CGS << std::endl;
       int n_sub = 1; // THIS IS JUST A HACK TO MAKE IT COMPILE 

       int n_fluid_dof = THORNADO_FLUID_NDOF;
       int n_moments   = THORNADO_NMOMENTS;

       const Real* prob_lo   = geom.ProbLo();

       Real dt_sub = dt / n_sub;
       int * boxlen = new int[3];

       Vector<Real> grid_lo(3);
       Vector<Real> grid_hi(3);

       int swX[3];
       swX[0] = my_ngrow;
       swX[1] = my_ngrow;
#if (BL_SPACEDIM > 2)
       swX[2] = my_ngrow;
#else
       swX[2] = 0;
#endif

       for (int i = 0; i < n_sub; i++)
       {

          // Make sure to zero dS and dR here since we don't want to 
          //    re-add terms from the last iteration
          dS.setVal(0.);
          dR.setVal(0.);

          // For now we will not allowing logical tiling
          for (MFIter mfi(S_border, false); mfi.isValid(); ++mfi) 
          {
              Box bx = mfi.validbox();

              grid_lo[0] = prob_lo[0] +  bx.smallEnd(0)  * dx[0] / 100.0; // Factor of 100 because Thornado uses m, not cm
              grid_hi[0] = prob_lo[0] + (bx.bigEnd(0)+1) * dx[0] / 100.0;
              boxlen[0] = bx.length(0);
   
              grid_lo[1] = prob_lo[1] +  bx.smallEnd(1)  * dx[1] / 100.0;
              grid_hi[1] = prob_lo[1] + (bx.bigEnd(1)+1) * dx[1] / 100.0;
              boxlen[1] = bx.length(1);

#if (BL_SPACEDIM > 2)
              grid_lo[2] = prob_lo[2] +  bx.smallEnd(2)  * dx[2] / 100.0;
              grid_hi[2] = prob_lo[2] + (bx.bigEnd(2)+1) * dx[2] / 100.0;
              boxlen[2] = bx.length(2);
#else
              grid_lo[2] = 0.;
              grid_hi[2] = 1.;
              boxlen[2]  = 1;
#endif
              InitThornado_Patch(boxlen, swX, grid_lo.dataPtr(), grid_hi.dataPtr());

              call_to_thornado(BL_TO_FORTRAN_BOX(bx), &dt_sub,
                               BL_TO_FORTRAN_FAB(S_border[mfi]),
                               BL_TO_FORTRAN_FAB(dS[mfi]),
                               BL_TO_FORTRAN_FAB(R_border[mfi]),
                               BL_TO_FORTRAN_FAB(dR[mfi]), 
                               &n_fluid_dof, &n_moments, &my_ngrow);
   
              FreeThornado_Patch();
          }

          // Add the source term to all components even though there should
          //     only be non-zero source terms for (Rho, Xmom, Ymom, Zmom, RhoE, UFX)
          MultiFab::Add( S_new, dS, Density, 0, S_new.nComp(), 0);
          MultiFab::Add(dS_new, dS, Density, 0, S_new.nComp(), 0);

          MultiFab::Add(U_R_new, dR, 0, 0, U_R_new.nComp(), 0);
          MultiFab::Add( dR_new, dR, 0, 0, U_R_new.nComp(), 0);

          // Fill the ghost cells before taking the next dt_sub 
          S_border.FillBoundary();
       }

       delete boxlen;

    // if (level > 0) then interpolate dS and dR
    } else {
       const Real  cur_time = state[Thornado_Type].curTime();

       int ng_to_fill = 0;

       AmrLevel::FillCoarsePatch(dS, 0, cur_time, Thornado_Fluid_Source_Type, 0, NUM_STATE, ng_to_fill);
       MultiFab::Add( S_new, dS, Density, 0, S_new.nComp(), 0);
       MultiFab::Add(dS_new, dS, Density, 0, S_new.nComp(), 0);

       AmrLevel::FillCoarsePatch(dR, 0, cur_time, Thornado_Rad_Source_Type, 0, dR_new.nComp(), ng_to_fill);
       MultiFab::Add(U_R_new, dR, 0, 0, U_R_new.nComp(), 0);
       MultiFab::Add( dR_new, dR, 0, 0, U_R_new.nComp(), 0);
    }

    // Copy dS_new into dS_old so that we can interpolate in time correctly 
    MultiFab::Copy(dS_old,dS_new,0,0,dS_new.nComp(),dS_new.nGrow());

    // Copy dR_new into dR_old so that we can interpolate in time correctly 
    MultiFab::Copy(dR_old,dR_new,0,0,dR_new.nComp(),dR_new.nGrow());
}
