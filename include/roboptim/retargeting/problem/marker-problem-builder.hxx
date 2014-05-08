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
#ifndef ROBOPTIM_RETARGETING_PROBLEM_MARKER_PROBLEM_BUILDER_HXX
# define ROBOPTIM_RETARGETING_PROBLEM_MARKER_PROBLEM_BUILDER_HXX
# include <boost/make_shared.hpp>

# include <cnoid/BodyLoader>
# include <cnoid/BodyMotion>

# include <libmocap/marker-set-factory.hh>
# include <libmocap/marker-trajectory-factory.hh>

# include <roboptim/core/problem.hh>

# include <roboptim/trajectory/state-function.hh>
# include <roboptim/trajectory/vector-interpolation.hh>

# include <roboptim/retargeting/problem/marker-function-factory.hh>


namespace roboptim
{
  namespace retargeting
  {
    /// \brief Convert markers trajectories to a RobOptim trajectory.
    ///
    /// The trajectory output size matches the number of marker, the
    /// input size is time.
    ///
    /// The data from libmocap is copied frame per frame into the new
    /// object.
    template <typename T>
    boost::shared_ptr<T>
    convertToTrajectory (const libmocap::MarkerTrajectory& raw)
    {
      typename T::vector_t parameters
	(raw.numFrames () * raw.numMarkers () * 3);

      std::vector<std::vector<double> >::const_iterator it;
      std::ptrdiff_t nFrame = 0;

      for (it = raw.positions ().begin (); it != raw.positions ().end ();
	   nFrame = (++it) - raw.positions ().begin())
	{
	  Eigen::Map<const typename T::vector_t>
	    positions (&(*it)[0], raw.numMarkers () * 3);
	  parameters.segment
	    (nFrame * raw.numMarkers (), raw.numMarkers () * 3) = positions;
	}

      return
	boost::make_shared<T>
	(parameters, raw.numMarkers (), 1. / raw.dataRate ());
    }

    void
    buildDataFromOptions (MarkerFunctionData& data,
			  const MarkerProblemOptions& options)
    {
      cnoid::BodyLoader loader;

      data.markerSet =
	libmocap::MarkerSetFactory ().load (options.markerSet);
      data.markersTrajectory =
	libmocap::MarkerTrajectoryFactory ().load (options.markersTrajectory);
      data.robotModel = loader.load (options.robotModel);

      if (options.trajectoryType == "discrete")
	data.trajectory =
	  convertToTrajectory<VectorInterpolation> (data.markersTrajectory);
      else
	throw std::runtime_error ("invalid trajectory type");
    }


    template <typename T>
    MarkerProblemBuilder<T>::MarkerProblemBuilder
    (const MarkerProblemOptions& options)
      : options_ (options)
    {}

    template <typename T>
    MarkerProblemBuilder<T>::~MarkerProblemBuilder ()
    {}

    template <typename T>
    void
    MarkerProblemBuilder<T>::operator ()
      (boost::shared_ptr<T>& problem,
       MarkerFunctionData& data)
    {
      buildDataFromOptions (data, options_);

      std::size_t nConstraints =
	static_cast<std::size_t> (data.nFrames ()) - 2;

      MarkerFunctionFactory factory (data);

      data.cost =
	factory.buildFunction<DifferentiableFunction> (options_.cost);

      problem = boost::make_shared<T> (*data.cost);

      std::vector<std::string>::const_iterator it;
      for (it = options_.constraints.begin ();
	   it != options_.constraints.end (); ++it)
	{
	  Constraint<DifferentiableFunction> constraint =
	    factory.buildConstraint<DifferentiableFunction> (*it);

	  switch (constraint.type)
	    {
	    case Constraint<T>::CONSTRAINT_TYPE_ONCE:
	      {
		problem->addConstraint
		  (constraint.function,
		   constraint.intervals,
		   constraint.scales);
		break;
	      }
	    case Constraint<T>::CONSTRAINT_TYPE_PER_FRAME:
	      {
		for (std::size_t i = 0; i < nConstraints; ++i)
		  {
		    const Function::value_type t =
		      (static_cast<Function::value_type> (i) + 1.) /
		      (static_cast<Function::value_type> (nConstraints) + 1.);
		    assert (t > 0. && t < 1.);

		    boost::shared_ptr<DifferentiableFunction> f
		      (new roboptim::StateFunction<Trajectory<3> >
		       (*data.trajectory,
			constraint.function,
			t * tMax,
			static_cast<Function::size_type>
			(constraint.stateFunctionOrder)));
		    problem->addConstraint
		      (f, constraint.intervals, constraint.scales);
		  }
		break;
	      }
	    default:
	      assert (0 && "should never happen");
	    }
	}
      problem->startingPoint () = data.trajectory->parameters ();
    }
  } // end of namespace retargeting.
} // end of namespace roboptim.

#endif //! ROBOPTIM_RETARGETING_PROBLEM_MARKER_PROBLEM_BUILDER_HXX