#include <roboptim/core/finite-difference-gradient.hh>
#include "roboptim/retargeting/collision.hh"

namespace roboptim
{
  namespace retargeting
  {
    Collision::Collision
    () throw ()
      : roboptim::GenericDifferentiableFunction<EigenMatrixSparse>
	(1,
	 1, "")
    {}

    Collision::~Collision () throw ()
    {}

    void
    Collision::impl_compute
    (result_t&, const argument_t&)
      const throw ()
    {
    }

    void
    Collision::impl_gradient
    (gradient_t& gradient,
     const argument_t& x,
     size_type i)
      const throw ()
    {
      roboptim::GenericFiniteDifferenceGradient<
	EigenMatrixSparse,
	finiteDifferenceGradientPolicies::Simple<EigenMatrixSparse> >
	fdg (*this);

      fdg.gradient (gradient, x, i);
    }

  } // end of namespace retargeting.
} // end of namespace roboptim.