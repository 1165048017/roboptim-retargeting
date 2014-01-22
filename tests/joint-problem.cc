// Copyright (C) 2013 by Thomas Moulard, AIST, CNRS, INRIA.
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


#define BOOST_TEST_MODULE joint_problem

#include <fstream>

#include <boost/test/unit_test.hpp>
#include <boost/test/output_test_stream.hpp>

#include <roboptim/core/visualization/gnuplot.hh>
#include <roboptim/core/visualization/gnuplot-commands.hh>
#include <roboptim/core/visualization/gnuplot-function.hh>

#include "roboptim/retargeting/function/choreonoid-body-trajectory.hh"
#include "roboptim/retargeting/problem/joint.hh"

#include <cnoid/BodyLoader>
#include <cnoid/BodyMotion>
#include <cnoid/BodyIMesh>

#include <yaml-cpp/yaml.h>

#include "tests-config.h"

using namespace roboptim;
using namespace roboptim::visualization;
using namespace roboptim::retargeting;

using boost::test_tools::output_test_stream;

void writeBodyMotion (const std::string& filename,
		      boost::shared_ptr<VectorInterpolation > result)
{
  typedef Eigen::Quaternion<VectorInterpolation::value_type> quaternion_t;

  std::ofstream fout (filename.c_str ());
  if (!fout.good ())
    throw std::runtime_error ("bad stream");

  int numFrames =
    result->parameters ().size () / result->outputSize ();
  double dt = result->length () / numFrames;
  int nDofs = result->outputSize ();

  YAML::Emitter out;
  out
    << YAML::Comment("Generated by roboptim-retargeting")
    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "BodyMotion"
    << YAML::Key << "components"
    << YAML::Value
    << YAML::BeginSeq

    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "MultiValueSeq"
    << YAML::Key << "content"
    << YAML::Value << "JointPosition"
    << YAML::Key << "frameRate"
    << YAML::Value << 1. / dt
    << YAML::Key << "numFrames"
    << YAML::Value << numFrames
    << YAML::Key << "numParts"
    << YAML::Value << nDofs - 6
    << YAML::Key << "frames"
    << YAML::Value

    << YAML::BeginSeq;

  for (int frameId = 0; frameId < numFrames; ++frameId)
    {
      VectorInterpolation::vector_t oneFrame = (*result) (frameId * dt);

      out << YAML::Flow << YAML::BeginSeq;
      for (int dofId = 6; dofId < result->outputSize (); ++dofId)
	out << oneFrame[dofId];
      out << YAML::EndSeq;
    }
  out << YAML::EndSeq;
  out << YAML::EndMap;

  out
    << YAML::BeginMap
    << YAML::Key << "type"
    << YAML::Value << "MultiSE3Seq"
    << YAML::Key << "content"
    << YAML::Value << "LinkPosition"
    << YAML::Key << "frameRate"
    << YAML::Value << 1. / dt
    << YAML::Key << "numFrames"
    << YAML::Value << numFrames
    << YAML::Key << "numParts"
    << YAML::Value << 1
    << YAML::Key << "format"
    << YAML::Value << "XYZQWQXQYQZ"
    << YAML::Key << "frames"
    << YAML::Value

    << YAML::BeginSeq;
  for (int frameId = 0; frameId < numFrames; ++frameId)
    {
      VectorInterpolation::vector_t oneFrame = (*result) (frameId * dt);

      // The free floating position (7 parameters) is considered as
      // one part.
      out << YAML::Flow << YAML::BeginSeq << YAML::BeginSeq;
      for (int dofId = 0; dofId < 3; ++dofId)
	out << oneFrame[dofId];

      VectorInterpolation::value_type norm = oneFrame.segment (3, 3).norm ();

      quaternion_t quaternion;
      quaternion.setIdentity ();

      if (norm >= 1e-10)
	quaternion = Eigen::AngleAxisd
	  (norm, oneFrame.segment (3, 3).normalized ());

      out
	<< quaternion.w ()
	<< quaternion.x () << quaternion.y () << quaternion.z ();

      out << YAML::EndSeq << YAML::EndSeq;
    }
  out << YAML::EndSeq;

  out << YAML::EndMap;

  fout << out.c_str ();
}


BOOST_AUTO_TEST_CASE (simple)
{
  // Configure log4cxx
  configureLog4cxx ();

  //FIXME: we should embed the copy.
  std::string modelFilePath
    ("/home/moulard/HRP4C-release/HRP4Cg2.yaml");

  // Loading robot.
  cnoid::BodyLoader loader;
  cnoid::BodyPtr robot = loader.load (modelFilePath);
  if (!robot)
    throw std::runtime_error ("failed to load model");

  // Loading the motion.
  cnoid::BodyMotionPtr bodyMotion = boost::make_shared<cnoid::BodyMotion> ();

  //FIXME: we should embed the copy.
  bodyMotion->loadStandardYAMLformat ("/home/moulard/29_07-hrp4c-initial-short.yaml");

  // Body Interaction Mesh
  cnoid::BodyIMeshPtr mesh = boost::make_shared<cnoid::BodyIMesh> ();
  if (!mesh->addBody (robot, bodyMotion))
    throw std::runtime_error ("failed to add body to body interaction mesh");
  if (!mesh->initialize ())
        throw std::runtime_error ("failed to initialize body interaction mesh");

  bool enableFreeze = false;
  bool enableVelocity = false;
  bool enablePosition = false;
  bool enableCollision = false;
  bool enableTorque = false;
  bool enableZmp = false;
  std::string solverName = "cfsqp";
  std::size_t oneFrameFullSize = 6 + bodyMotion->jointPosSeq ()->numParts ();
  std::vector<bool> enabledDofs (oneFrameFullSize, true);

  // Disable useless dofs.
  enabledDofs[6 + 17] = false; // NECK_Y
  enabledDofs[6 + 18] = false; // NECK_R
  enabledDofs[6 + 19] = false; // NECK_P
  enabledDofs[6 + 20] = false; // EYEBROW_P
  enabledDofs[6 + 21] = false; // EYELID_P
  enabledDofs[6 + 22] = false; // EYE_P
  enabledDofs[6 + 23] = false; // EYE_Y
  enabledDofs[6 + 24] = false; // MOUTH_P
  enabledDofs[6 + 25] = false; // LOWERLIP_P
  enabledDofs[6 + 26] = false; // UPPERLIP_P
  enabledDofs[6 + 27] = false; // CHEEK_P
  enabledDofs[6 + 28] = false; // FIXME - PROBLEMATIC DOF disabled velocity too large R_SHOULDER_P
  enabledDofs[6 + 34] = false; // R_HAND_J0
  enabledDofs[6 + 35] = false; // R_HAND_J1
  enabledDofs[6 + 42] = false; // L_HAND_J0
  enabledDofs[6 + 43] = false; // L_HAND_J1

  typedef problem::Joint::size_type size_type;
  typedef problem::Joint::solver_t solver_t;

  problem::JointShPtr_t jointProblem =
    roboptim::retargeting::problem::Joint::
    buildVectorInterpolationBasedOptimizationProblem
    (robot, bodyMotion, mesh, enableFreeze, enableVelocity,
     enablePosition, enableCollision,
     enableTorque, enableZmp, solverName, enabledDofs);

  writeBodyMotion
    ("/tmp/initial.yaml",
     boost::make_shared<ChoreonoidBodyTrajectory>
     (bodyMotion, true));

  jointProblem->solve ();

  // Rebuild final trajectory.
  if (jointProblem->result ().which () == solver_t::SOLVER_ERROR)
    {
      std::cerr << "error" << std::endl;
      roboptim::SolverError error =
  	boost::get<roboptim::SolverError> (jointProblem->result ());
      std::cerr << "Result:\n" << error << std::endl;
      return;
    }

  boost::shared_ptr<VectorInterpolation >
    finalTrajectoryFct;

  Function::vector_t finalX (oneFrameFullSize * bodyMotion->numFrames ());
  finalX.setZero ();
  Function::vector_t finalXReduced;

  if (jointProblem->result ().which () == solver_t::SOLVER_VALUE_WARNINGS)
    {
      std::cerr << "warnings" << std::endl;
      roboptim::ResultWithWarnings result =
  	boost::get<roboptim::ResultWithWarnings> (jointProblem->result ());
      std::cerr << "Result:\n" << result << std::endl;
      finalXReduced = result.x;
    }

  if (jointProblem->result ().which () == solver_t::SOLVER_VALUE)
    {
      std::cerr << "ok" << std::endl;
      roboptim::Result result =
	boost::get<roboptim::Result> (jointProblem->result ());
      std::cerr << "Result:\n" << result << std::endl;
      finalXReduced = result.x;
    }

  // Re-expend trajectory.
  std::size_t nEnabledDofs =
    std::count (enabledDofs.begin (), enabledDofs.end (), true);
  for (std::size_t frameId = 0; frameId < bodyMotion->numFrames (); ++frameId)
    {
      std::size_t jointIdReduced = 0;
      for (std::size_t jointId = 0; jointId < oneFrameFullSize; ++jointId)
	{
	  if (enabledDofs[jointId])
	    finalX[frameId * oneFrameFullSize + jointId] =
	      finalXReduced[frameId * (oneFrameFullSize - nEnabledDofs) + jointIdReduced++];
	  else // FIXME: if we disable free floating THIS WILL NOT WORK
	    finalX[frameId * oneFrameFullSize + jointId] =
	      bodyMotion->jointPosSeq ()->frame (0)[jointId];
	}
    }

  finalTrajectoryFct =
    vectorInterpolation
    (finalX, oneFrameFullSize,
     jointProblem->dt ());

  assert (!!finalTrajectoryFct);

  // Display initial and final trajectory.
  using namespace roboptim::visualization::gnuplot;
  Gnuplot gnuplot = Gnuplot::make_interactive_gnuplot ();
  std::cout
    << (gnuplot
  	<< plot (*finalTrajectoryFct, jointProblem->interval ())
  	);

  writeBodyMotion ("/tmp/result.yaml", finalTrajectoryFct);
}
