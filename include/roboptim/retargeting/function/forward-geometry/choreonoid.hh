// Copyright (C) 2014 by Thomas Moulard, AIST, CNRS.
//
// This file is part of the roboptim.
//
// roboptim is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// roboptim is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with roboptim.  If not, see <http://www.gnu.org/licenses/>.

#ifndef ROBOPTIM_RETARGETING_FORWARD_GEOMETRY_CHOREONOID_HH
# define ROBOPTIM_RETARGETING_FORWARD_GEOMETRY_CHOREONOID_HH
# include <cmath>
# include <boost/array.hpp>
# include <boost/format.hpp>
# include <boost/make_shared.hpp>

# include <cnoid/Body>
# include <cnoid/JointPath>
# include <cnoid/Jacobian>

# include <roboptim/retargeting/choreonoid.hh>
# include <roboptim/retargeting/eigen-rigid-body.hh>
# include <roboptim/retargeting/function/forward-geometry.hh>
# include <roboptim/retargeting/utility.hh>

# include <roboptim/core/finite-difference-gradient.hh>


namespace roboptim
{
  namespace retargeting
  {
    /// \brief Compute forward geometry for a particular robot model.
    ///
    /// Robot model from choreonoid should be passed to the
    /// constructor and use for computation.  The analytical jacobian
    /// must also be retrieved and returned in the
    /// impl_gradient/impl_jacobian methods.
    template <typename T>
    class ForwardGeometryChoreonoid : public ForwardGeometry<T>
    {
    public:
      ROBOPTIM_DIFFERENTIABLE_FUNCTION_FWD_TYPEDEFS_ (ForwardGeometry<T>);

      explicit ForwardGeometryChoreonoid
      (cnoid::BodyPtr robot, int bodyId)
	: ForwardGeometry<T> (6 + robot->numJoints (), "choreonoid"),
	  robot_ (robot),
	  bodyId_ (bodyId),
	  jointPath_ (robot->rootLink ()),
	  angleAxis_ (),
	  J_ (),
	  dR_ (),
	  fd_ (boost::make_shared<fdFunction_t> (*this))
      {
	if (bodyId >= robot->numLinks () || robot->link (bodyId_) == 0)
	  {
	    boost::format fmt
	      ("failed to construct ForwardGeometryChoreonoid function:"
	       " invalid body id %d (robot contains %d bodies)");
	    fmt % bodyId % robot->numLinks ();
	    throw std::runtime_error (fmt.str ());
	  }

	jointPath_.setPath (robot->rootLink (), robot->link (bodyId_));
	J_.resize(6, jointPath_.numJoints ());
	dR_[0].setZero ();
	dR_[1].setZero ();
	dR_[2].setZero ();
      }

      explicit ForwardGeometryChoreonoid
      (cnoid::BodyPtr robot, const std::string& bodyName)
	: ForwardGeometry<T> (6 + robot->numJoints (), "choreonoid"),
	  robot_ (robot),
	  bodyId_ (0),
	  jointPath_ (robot->rootLink ()),
	  J_ (),
	  dR_ (),
	  fd_ (boost::make_shared<fdFunction_t> (*this))
      {
	cnoid::Link* link = robot->link (bodyName.c_str ());
	if (!link)
	  {
	    boost::format fmt
	      ("failed to construct ForwardGeometryChoreonoid function:"
	       " no body whose name is '%s' can be found");
	    fmt % bodyName;
	    throw std::runtime_error (fmt.str ());
	  }

	bodyId_ = link->jointId ();
	if (bodyId_ >= robot->numLinks () || robot->link (bodyId_) == 0)
	  {
	    boost::format fmt
	      ("failed to construct ForwardGeometryChoreonoid function:"
	       " invalid body id %d (robot contains %d bodies)");
	    fmt % bodyId_ % robot->numLinks ();
	    throw std::runtime_error (fmt.str ());
	  }

	jointPath_.setPath (robot->rootLink (), robot->link (bodyId_));
	J_.resize(6, jointPath_.numJoints ());
	dR_[0].setZero ();
	dR_[1].setZero ();
	dR_[2].setZero ();
      }


      virtual ~ForwardGeometryChoreonoid ()
      {}

    protected:
      void
      impl_compute
      (result_t& result, const argument_t& x)
	const
      {
	updateRobotConfiguration (robot_, x);
	robot_->calcForwardKinematics ();
	transformToVector
	  (result, jointPath_.endLink ()->position ());
      }


      /// \brief Gradient computation.
      ///
      /// The jacobian is as follow:
      ///
      /// ------------------------------------------------------------------
      /// | function id || tx | ty | tz | rx | ry | rz | dof0 | ... | dofN |
      /// ------------------------------------------------------------------
      /// | 0 (tx)      || 1  | 0  | 0  |              |                   |
      /// | 1 (ty)      || 0  | 1  | 0  |      [1]     |     Choreonoid    |
      /// | 2 (tz)      || 0  | 0  | 1  |              |                   |
      /// | 3 (rx)      || 0  | 0  | 0  |              |                   |
      /// | 4 (ry)      || 0  | 0  | 0  |      [2]     |     Choreonoid    |
      /// | 5 (rz)      || 0  | 0  | 0  |              |                   |
      /// ------------------------------------------------------------------
      ///
      /// I.e. influence of configuration change on the body position.
      ///
      /// In our case, we only compute one line of this array.
      ///
      /// Formula [1] and [2] are available in "On the Dynamics
      /// Modeling of Free-Floating-Base Articulated Mechanisms and
      /// Applications to Humanoid Whole-Body Dynamics and Control",
      /// K. Bouyarmane, A. Kheddar.
      ///
      ///
      /// \param gradient Gradient
      /// \param x configuration (free floating then DOF values)
      /// \param functionId jacobian line to be computed
      void
      impl_gradient2 (gradient_t& gradient,
		     const argument_t& x,
		     size_type functionId)
	const
      {
	// Set the robot configuration.
	updateRobotConfiguration (robot_, x);

	// Update positions.
	robot_->calcForwardKinematics ();

	// Free floating (columns 0 to 5).

	// columns 0 to 2 (translation)
	gradient.template segment<3> (0).setZero ();
	if (functionId < 3.)
	  gradient[functionId] = 1.;

	// columns 3 to 6 (rotation)
	const typename cnoid::Position::LinearPart& R0 =
	  jointPath_.baseLink ()->T ().linear ();
	const typename cnoid::Position::LinearPart& R =
	  jointPath_.endLink ()->T ().linear ();
	const typename cnoid::Position::TranslationPart& t0 =
	  jointPath_.baseLink ()->T ().translation ();
	const typename cnoid::Position::TranslationPart& tk =
	  jointPath_.endLink ()->T ().translation ();

	updateDR (x.template segment<3> (3));

	Eigen::Matrix<value_type, 3, 3> J_global =
	  R0.col (2) * R0.col (1).transpose () * dR_[0] +
	  R0.col (1) * R0.col (0).transpose () * dR_[2] +
	  R0.col (0) * R0.col (2).transpose () * dR_[1];

	if (functionId < 3)
	  {
	    Eigen::Matrix<value_type, 3, 1> p_;
	    Eigen::Matrix<value_type, 3, 1> p = tk + R * p_ - t0;
	    Eigen::Matrix<value_type, 3, 3> hatp;
	    hat (hatp, p);

	    Eigen::Matrix<value_type, 3, 3> Jt = -hatp * J_global;
	    gradient.template segment<3> (3) = Jt.row (functionId);
	  }
	else
	  {
	    Eigen::Matrix<value_type, 3, 3> Jr = R0.transpose () * J_global;
	    gradient.template segment<3> (3) = Jr.row (functionId - 3);
	  }

	// DOF (all columns > 5).

	// First we compute the jacobian for the current joint path.
	//jointPath_.calcJacobian (J_);
	cnoid::setJacobian<0x3f, 0, 0>
	  (this->jointPath_, this->jointPath_.endLink (), J_);

	for (std::size_t jacobianId = 0; jacobianId < jointPath_.numJoints (); ++jacobianId)
	  {
	    J_.template block <3, 1> (3, jacobianId) =
	      R0.transpose() * J_.template block <3, 1> (3, jacobianId);
	  }

	// And we replace at the right position. The jacobian of the
	// joint path is incomplete so we have to copy the values to
	// the right location manually. All the joints which are not
	// in the joint path will not have any effect.
	std::size_t jointId = 0;
	for (std::size_t jacobianId = 0; jacobianId < jointPath_.numJoints (); ++jacobianId)
	  {
	    jointId = jointPath_.joint (jacobianId)->index () + 6 - 1;
	    ROBOPTIM_RETARGETING_ASSERT (jointId < gradient.size ());
	    ROBOPTIM_RETARGETING_ASSERT (jointId >= 6);

	    gradient[jointId] = J_ (functionId, jacobianId);
	  }
      }

      virtual void impl_jacobian2 (jacobian_t& J, const argument_t& x)
	const
      {
	// Set the robot configuration.
	updateRobotConfiguration (robot_, x);

	// Update positions.
	robot_->calcForwardKinematics ();

	// Free floating (columns 0 to 5).

	// columns 3 to 6 (rotation)
	const typename cnoid::Position::LinearPart& R0 =
	  jointPath_.baseLink ()->T ().linear ();
	const typename cnoid::Position::LinearPart& R =
	  jointPath_.endLink ()->T ().linear ();
	const typename cnoid::Position::TranslationPart& t0 =
	  jointPath_.baseLink ()->T ().translation ();
	const typename cnoid::Position::TranslationPart& tk =
	  jointPath_.endLink ()->T ().translation ();

	updateDR (x.template segment<3> (3));

	Eigen::Matrix<value_type, 3, 3> J_global =
	  R0.col (2) * R0.col (1).transpose () * dR_[0] +
	  R0.col (1) * R0.col (0).transpose () * dR_[2] +
	  R0.col (0) * R0.col (2).transpose () * dR_[1];

	Eigen::Matrix<value_type, 3, 1> p_;
	Eigen::Matrix<value_type, 3, 1> p = tk + R * p_ - t0;
	Eigen::Matrix<value_type, 3, 3> hatp;
	hat (hatp, p);

	J.template block<3, 3> (0, 0).setIdentity ();
	J.template block<3, 3> (3, 0).setZero ();

	J.template block<3, 3> (0, 3) = -hatp * J_global;
	J.template block<3, 3> (3, 3) = R0.transpose () * J_global;

	// DOF (all columns > 5).

	// First we compute the jacobian for the current joint path.
	//jointPath_.calcJacobian (J_);
	cnoid::setJacobian<0x3f, 0, 0>
	  (this->jointPath_, this->jointPath_.endLink (), J_);

	for (int jacobianId = 0; jacobianId < jointPath_.numJoints (); ++jacobianId)
	  {
	    J_.template block <3, 1> (3, jacobianId) =
	      R0.transpose() * J_.template block <3, 1> (3, jacobianId);
	  }

	// And we replace at the right position. The jacobian of the
	// joint path is incomplete so we have to copy the values to
	// the right location manually. All the joints which are not
	// in the joint path will not have any effect.
	int jointId = 0;
	for (int jacobianId = 0; jacobianId < jointPath_.numJoints (); ++jacobianId)
	  {
	    jointId = jointPath_.joint (jacobianId)->index () + 6 - 1;
	    ROBOPTIM_RETARGETING_ASSERT (jointId < J.cols ());
	    ROBOPTIM_RETARGETING_ASSERT (jointId >= 6);

	    J.col (jointId) = J_.col (jacobianId);
	  }

      }

      // temporary version using finite differences gradient.
      void
      impl_gradient (gradient_t& gradient,
		     const argument_t& x,
		     size_type functionId)
	const
      {
	fd_->gradient (gradient, x, functionId);
      }

      // See doc/sympy/euler-angles.py
      template <typename Derived>
      void updateDR (const typename Eigen::MatrixBase<Derived>& x) const
      {
	value_type cr, sr, cp, sp, cy, sy;
	sincos (x[0], &sr, &cr);
	sincos (x[1], &sp, &cp);
	sincos (x[2], &sy, &cy);

	dR_[0] <<
	  0.,  -cy * sp,  -sy * cp,
	  0.,  -sy * sp,  cy * cp,
	  0.,  -cp,       0.;

	dR_[1] <<
	  cy * sp * cr + sy * sr,
	  cy * cp * sr,
	  -sy * sp * sr - cy * cr,

	  sy * sp * cr - cy * sr,
	  sy * cp * sr,
	  cy * sp * sr - sy * cr,

	  cp * cr, -sp * sr, 0.;

	dR_[2] <<
	  -cy * sp * sr + sy * cr,
	  cy * cp * cr,
	  -sy * sp * cr + cy * sr,

	  - sy * sp * sr - cy * cr,
	  sy * cp * cr,
	  cy * sp * cr + sy * sr,

	  -cp * sr, -sp * cr, 0.;
      }

    private:
      cnoid::BodyPtr robot_;
      int bodyId_;

      mutable cnoid::JointPath jointPath_;
      /// \brief Root link position
      mutable cnoid::Position rootLinkPosition_;
      /// \brief Temporary variable used for angle conversions.
      mutable Eigen::AngleAxis<value_type> angleAxis_;

      /// \brief Jacobian computed by Choreonoid (buffer).
      mutable cnoid::MatrixXd J_;

      /// \brief Variation of the derivation w.r.t parameters.
      ///
      /// Jacobian of the function which associates to the rotation
      /// parameters the rotation matrix.
      ///
      /// Jacobian value is expressed at R0 the rotation of the base
      /// link (i.e. waist).
      ///
      /// dR_[0] is:
      /// \f$\frac{\partial R_{0}}{\partial \theta}(\theta)\f$
      ///
      /// \f$R_0\f$ is the rotation matrix first column.
      mutable boost::array<Eigen::Matrix<value_type, 3, 3>, 3> dR_;

      // Temporary - finite difference gradient.
      typedef roboptim::GenericFiniteDifferenceGradient<
	T, finiteDifferenceGradientPolicies::Simple<T> >
      fdFunction_t;
      boost::shared_ptr<fdFunction_t> fd_;

    public:
      EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    };
  } // end of namespace retargeting.
} // end of namespace roboptim.

#endif //! ROBOPTIM_RETARGETING_FORWARD_GEOMETRY_CHOREONOID_HH
