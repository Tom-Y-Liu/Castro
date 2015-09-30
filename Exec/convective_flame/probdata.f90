module probdata_module

  double precision, save :: pert_factor, dens_base, pres_base, y_pert_center
  double precision, save :: pert_width
  double precision, save :: cutoff_density

  double precision, save :: thermal_conductivity

  logical,          save :: do_isentropic

  integer,          save :: boundary_type

  double precision, save :: frac

  logical         , save :: zero_vels
end module probdata_module
