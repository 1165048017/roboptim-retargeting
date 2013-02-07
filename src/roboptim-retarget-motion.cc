#include <iostream>
#include <string>

#include <boost/format.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/program_options.hpp>

#include <log4cxx/logger.h>
#include <log4cxx/xml/domconfigurator.h>

#include <roboptim/retargeting/retarget.hh>

#include <roboptim/retargeting/config.hh>
#include "directories.hh"

void help (const boost::program_options::options_description& desc)
{
  std::cout
    << "usage: "
    << "roboptim-retarget-motion -t TRAJECTORY_FILE -r ROBOT_FILE\n\n"
    << desc << std::endl;
}

void version ()
{
  std::cout << "roboptim-retarget-motion v "
	    << ROBOPTIM_RETARGETING_VERSION << std::endl;
}

int main (int argc, char** argv)
{
  namespace po = boost::program_options;

  // Initialize logging system.
  std::string log4cxxConfigurationFile = PKG_SHARE_DIR;
  log4cxxConfigurationFile += "/log4cxx.xml";
  log4cxx::xml::DOMConfigurator::configure (log4cxxConfigurationFile);

  log4cxx::LoggerPtr logger
    (log4cxx::Logger::getLogger
     ("roboptim.retargeting.roboptim-retarget-motion"));

  // Parse options.
  std::string trajectoryFile;
  std::string characterFile;
  std::string verbosityLevel;
  bool enableBoneLength;
  bool enablePosition;
  bool enableCollision;
  bool enableTorque;
  std::string solverName;

  po::options_description general ("General options");
  general.add_options ()
    ("help,h", "produce help message")
    ("version,v", "print version string")
    ("trajectory,t", po::value<std::string> (&trajectoryFile),
     "markers trajectory (YAML)")
    ("robot,r", po::value<std::string> (&characterFile),
     "robot description (YAML)")
    ("solver,s", po::value<std::string> (&solverName)->default_value ("ipopt"),
     "solver name (ipopt, cfsqp)")
    ("verbosity-level,l",
     po::value<std::string> (&verbosityLevel),
     "Verbosity level (WARN, DEBUG, TRACE)")
    ;

  po::options_description optim ("Optimization problem options");
  optim.add_options ()
    ("enable-bone-length,B",
     po::value<bool> (&enableBoneLength)->default_value (true),
     "enable bone-length constraints")
    ("enable-position,P",
     po::value<bool> (&enablePosition)->default_value (true),
     "enable positional constraints")
    ("enable-collision,C",
     po::value<bool> (&enableCollision)->default_value (true),
     "enable collision constraints")
    ("enable-torque,T",
     po::value<bool> (&enableTorque)->default_value (true),
     "enable torque constraints")
    ;

  po::options_description desc ("Allowed options");
  desc.add (general).add (optim);

  po::variables_map vm;
  try
    {
      po::store(po::parse_command_line (argc, argv, desc), vm);
      po::notify (vm);
    }
  catch (po::error& e)
    {
      std::cout << e.what () << std::endl;
      help (desc);
      return 5;
    }

  // Setup log from command line options.
  if (!verbosityLevel.empty ())
    {
      log4cxx::LoggerPtr rootLogger (log4cxx::Logger::getRootLogger ());
      log4cxx::LevelPtr level = log4cxx::Level::toLevel (verbosityLevel);
      if (level)
	rootLogger->setLevel (level);
      else
	LOG4CXX_ERROR (logger, "invalid logging level");
    }

  // Dispatch particular working modes.
  if (vm.count ("help"))
    {
      help (desc);
      return 0;
    }

  if (vm.count ("version"))
    {
      version ();
      return 0;
    }

  // Check options.
  if (trajectoryFile.empty () && characterFile.empty ())
    {
      std::cout << "trajectory and robot files are missing" << std::endl;
      help (desc);
      return 1;
    }
  if (trajectoryFile.empty () && !characterFile.empty ())
    {
      std::cout << "trajectory file is missing" << std::endl;
      help (desc);
      return 1;
    }
  if (!trajectoryFile.empty () && characterFile.empty ())
    {
      std::cout << "robot file is missing" << std::endl;
      help (desc);
      return 1;
    }

  // Resolve files.
  std::string dataDir (PKG_SHARE_DIR);
  dataDir += "/data";

  std::string trajectoryFilePath (trajectoryFile);
  std::string characterFilePath (characterFile);

  if (!std::ifstream (trajectoryFilePath.c_str ()))
    {
      trajectoryFilePath = dataDir + "/" + trajectoryFilePath;
      if (!std::ifstream (trajectoryFilePath.c_str ()))
	{
	  std::cerr << "trajectory file does not exist" << std::endl;
	  return 2;
	}
    }

  if (!std::ifstream (characterFilePath.c_str ()))
    {
      characterFilePath = dataDir + "/" + characterFilePath;
      if (!std::ifstream (characterFilePath.c_str ()))
	{
	  std::cerr << "robot file does not exist" << std::endl;
	  return 2;
	}
    }

#ifndef NDEBUG
  LOG4CXX_WARN
    (logger,
     "you are running debug mode, optimization process will be *VERY* slow");
#endif //!NDEBUG

  LOG4CXX_INFO (logger, "loading optimization problem...");

  // Retarget motion.
  roboptim::retargeting::Retarget retarget
    (trajectoryFilePath,
     characterFilePath,
     enableBoneLength,
     enablePosition,
     enableCollision,
     enableTorque,
     solverName);
  retarget.animatedMesh ()->writeGraphvizGraphs ("/tmp");

  LOG4CXX_DEBUG (logger,
		"Problem:\n" << *retarget.problem ());

  LOG4CXX_INFO (logger, "solving optimization problem...");
  retarget.solve ();
  LOG4CXX_INFO (logger, "done");

  // Check if the minimization has succeed.
  if (retarget.result ().which () ==
      roboptim::retargeting::Retarget::solver_t::SOLVER_ERROR)
    {
      std::cout << "No solution has been found. Failing..."
                << std::endl
                << boost::get<roboptim::SolverError>
	(retarget.result ()).what ()
                << std::endl;
      return 10;
    }

  // Get the result.
  Eigen::VectorXd x;

  LOG4CXX_INFO (logger, "a solution has been found!");

  if (retarget.result ().which () ==
      roboptim::retargeting::Retarget::solver_t::SOLVER_VALUE_WARNINGS)
    {
      const roboptim::Result& result =
	boost::get<roboptim::ResultWithWarnings> (retarget.result ());
      x = result.x;
      LOG4CXX_WARN (logger, "solver warnings: " << result);
    }
  else
    {
      const roboptim::Result& result =
	boost::get<roboptim::Result> (retarget.result ());
      x = result.x;
      LOG4CXX_WARN (logger, "result: " << result);
    }

  const std::string filename ("/tmp/result.yaml");

  retarget.animatedMesh ()->state () =x;
  retarget.animatedMesh ()->computeVertexWeights ();
  retarget.animatedMesh ()->writeTrajectory (filename);
  LOG4CXX_INFO (logger, "trajectory written to: " << filename);

  LOG4CXX_INFO (logger, "program succeeded, exiting");
}