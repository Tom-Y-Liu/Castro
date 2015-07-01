     subroutine ca_estdt(u,u_l1,u_l2,u_l3,u_h1,u_h2,u_h3,lo,hi,dx,dt)

     use network, only : nspec, naux
     use eos_module
     use eos_type_module
     use meth_params_module, only : NVAR, URHO, UMX, UMY, UMZ, UEINT, UESGS, UTEMP, UFS, UFX, &
                                    allow_negative_energy
     use bl_constants_module

     implicit none

     integer          :: u_l1,u_l2,u_l3,u_h1,u_h2,u_h3
     integer          :: lo(3), hi(3)
     double precision :: u(u_l1:u_h1,u_l2:u_h2,u_l3:u_h3,NVAR)
     double precision :: cs(u_l1:u_h1,u_l2:u_h2,u_l3:u_h3)
     double precision :: e(u_l1:u_h1,u_l2:u_h2,u_l3:u_h3)
     double precision :: dx(3), dt

     double precision :: rhoInv,ux,uy,uz,c,dt1,dt2,dt3
     double precision :: sqrtK,grid_scl,dt4
     integer          :: i,j,k
     integer          :: pt_index(3)

     type (eos_t), allocatable :: eos_state(:)
     integer :: nx(3), eos_state_len(1), n

     ! Compute sound speed

     nx = hi - lo + 1
     eos_state_len(1) = nx(1) * nx(2) * nx(3)
     allocate(eos_state(eos_state_len(1)))

     eos_state(:) % rho = reshape(u(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),URHO ), eos_state_len)
     eos_state(:) % T   = reshape(u(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),UTEMP), eos_state_len)
     eos_state(:) % e   = reshape(u(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),UEINT), eos_state_len) / eos_state(:) % rho

     do n = 1, nspec
        eos_state(:) % xn(n)  = reshape(u(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),UFS+n-1), eos_state_len) / eos_state(:) % rho
     enddo
     do n = 1, naux
        eos_state(:) % aux(n) = reshape(u(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),UFX+n-1), eos_state_len) / eos_state(:) % rho
     enddo

     call eos(eos_input_re, eos_state, state_len = eos_state_len(1))

     cs(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3)) = reshape(eos_state(:) % cs, nx)
     e(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3))  = reshape(eos_state(:) % e , nx)

     grid_scl = (dx(1)*dx(2)*dx(3))**THIRD

     ! Translate to primitive variables, compute sound speed (call eos)
     do k = lo(3),hi(3)
         do j = lo(2),hi(2)
            do i = lo(1),hi(1)

               rhoInv = ONE / u(i,j,k,URHO)

               ux = u(i,j,k,UMX) * rhoInv
               uy = u(i,j,k,UMY) * rhoInv
               uz = u(i,j,k,UMZ) * rhoInv

               if (UESGS .gt. -1) &
                  sqrtK = dsqrt( rhoInv*u(i,j,k,UESGS) )

               ! Protect against negative e
               if (e(i,j,k) .gt. ZERO .or. allow_negative_energy .eq. 1) then
                  c = cs(i,j,k)
               else
                  c = ZERO
               end if

               dt1 = dx(1)/(c + abs(ux))
               dt2 = dx(2)/(c + abs(uy))
               dt3 = dx(3)/(c + abs(uz))
               dt = min(dt,dt1,dt2,dt3)

               ! Now let's check the diffusion terms for the SGS equations
               if (UESGS .gt. -1) then

                  ! First for the term in the momentum equation
                  ! This is actually dx^2 / ( 6 nu_sgs )
                  ! Actually redundant as it takes the same form as below with different coeff
                  ! dt4 = grid_scl / ( 0.42d0 * sqrtK )

                  ! Now for the term in the K equation itself
                  ! nu_sgs is 0.65
                  ! That gives us 0.65*6 = 3.9
                  ! Using 4.2 to be conservative (Mach1-256 broke during testing with 3.9)
                  !               dt4 = grid_scl / ( 3.9d0 * sqrtK )
                  dt4 = grid_scl / ( 4.2d0 * sqrtK )
                  dt = min(dt,dt4)

               end if

            enddo
         enddo
     enddo

     deallocate(eos_state)

     end subroutine ca_estdt
