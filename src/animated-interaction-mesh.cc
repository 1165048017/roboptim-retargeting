#include <algorithm>
#include <stdexcept>
#include <fstream>

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/make_shared.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/graphviz.hpp>

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Triangulation_3.h>

#include <log4cxx/logger.h>

#include <yaml-cpp/iterator.h>
#include <yaml-cpp/yaml.h>

#include "roboptim/retargeting/animated-interaction-mesh.hh"

#include "yaml-helper.hh"

// Remove trace logging in release.
#ifdef NDEBUG
# undef LOG4CXX_TRACE
# define LOG4CXX_TRACE(logger, msg)
#endif //!NDEBUG

namespace roboptim
{
  namespace retargeting
  {
    log4cxx::LoggerPtr AnimatedInteractionMesh::logger
    (log4cxx::Logger::getLogger("roboptim.retargeting.AnimatedInteractionMesh"));

    AnimatedInteractionMesh::AnimatedInteractionMesh ()
      :  framerate_ (),
	 numFrames_ (),
	 numVertices_ (),
	 state_ (),
	 graph_ ()
    {}

    AnimatedInteractionMesh::~AnimatedInteractionMesh ()
    {}

    AnimatedInteractionMesh::vertex_iterator_t
    AnimatedInteractionMesh::getVertexFromLabel (const std::string& label) const
    {
      vertex_iterator_t vertexIt;
      vertex_iterator_t vertexEnd;
      boost::tie (vertexIt, vertexEnd) = boost::vertices (graph ());
      for (; vertexIt != vertexEnd; ++vertexIt)
	if (graph ()[*vertexIt].label == label)
	  return vertexIt;
      return vertexIt;
    }

    AnimatedInteractionMesh::vertex_iterator_t
    AnimatedInteractionMesh::getVertexFromPosition (unsigned frameId,
						    double x,
						    double y,
						    double z) const
    {
      vertex_iterator_t vertexIt;
      vertex_iterator_t vertexEnd;
      boost::tie (vertexIt, vertexEnd) = boost::vertices (graph ());
      for (; vertexIt != vertexEnd; ++vertexIt)
	if (graph ()[*vertexIt].positions[frameId][0] == x
	    && graph ()[*vertexIt].positions[frameId][1] == y
	    && graph ()[*vertexIt].positions[frameId][2] == z)
	  return vertexIt;
      return vertexIt;
    }

    void
    AnimatedInteractionMesh::loadEdgesFromYaml
    (const YAML::Node& node,
     AnimatedInteractionMeshShPtr_t animatedMesh)
    {
      vertex_iterator_t vertexBegin;
      vertex_iterator_t vertexEnd;
      boost::tie (vertexBegin, vertexEnd) =
	boost::vertices (animatedMesh->graph ());

      for(YAML::Iterator it = node.begin (); it != node.end (); ++it)
	{
	  std::string startMarker, endMarker;
	  double scale = 0.;

	  (*it)[0] >> startMarker;
	  (*it)[1] >> endMarker;
	  (*it)[2] >> scale;

	  vertex_iterator_t itStartMarker =
	     animatedMesh->getVertexFromLabel (startMarker);
	  vertex_iterator_t itEndMarker =
	    animatedMesh->getVertexFromLabel (endMarker);
	  if (itStartMarker == vertexEnd)
	    {
	      LOG4CXX_WARN
		(logger,
		 boost::format("unknown marker '%1%' in character file")
		 % startMarker);
	      continue;
	    }
	  if (itEndMarker == vertexEnd)
	    {
	      LOG4CXX_WARN
		(logger,
		 boost::format("unknown marker '%1%' in character file")
		 % endMarker);
	      continue;
	    }
	  if (itStartMarker == itEndMarker)
	    {
	      LOG4CXX_WARN
		(logger,
		 "source and target vertex are the same, ignoring");
	      continue;
	    }

	  edge_descriptor_t edge;
	  bool ok;
	  boost::tie (edge, ok) =
	    boost::add_edge (*itStartMarker, *itEndMarker,
			     animatedMesh->graph ());
	  if (!ok)
	    LOG4CXX_WARN (logger, "failed to add edge");
	  animatedMesh->graph ()[edge].scale = scale;
	}
    }

    AnimatedInteractionMeshShPtr_t
    AnimatedInteractionMesh::loadAnimatedMesh
    (const std::string& trajectoryFile,
     const std::string& characterFile)
    {
      LOG4CXX_INFO
	(logger,
	 "loading animated mesh from files: "
	 << trajectoryFile << " (trajectory) "
	 << characterFile << " (character)");

      AnimatedInteractionMeshShPtr_t animatedMesh =
	boost::make_shared<AnimatedInteractionMesh> ();

      // Parse trajectory file.
      {
	std::ifstream fin (trajectoryFile.c_str ());
	if (!fin.good ())
	  throw std::runtime_error ("bad stream");
	YAML::Parser parser (fin);

	YAML::Node doc;

	if (!parser.GetNextDocument (doc))
	  throw std::runtime_error ("empty document");

	checkNodeType (doc, YAML::NodeType::Map);

	std::string type;
	doc["type"] >> type;
	if (type != "MultiVector3Seq")
	  throw std::runtime_error ("bad content");
	// content
	doc["frameRate"] >> animatedMesh->framerate_;

	doc["numFrames"] >> animatedMesh->numFrames_;

	// Compute number of vertices.
	animatedMesh->numVertices_ = (unsigned int)doc["partLabels"].size ();

	// Resize state vector.
	animatedMesh->state_.resize
	  (animatedMesh->numFrames_ * animatedMesh->numVertices_ * 3);
	animatedMesh->state_.setZero ();

	// Add one vertex per label.
	for (YAML::Iterator it = doc["partLabels"].begin ();
	     it != doc["partLabels"].end (); ++it)
	  {
	    std::string label;
	    *it >> label;

	    vertex_descriptor_t
	      vertex = boost::add_vertex (animatedMesh->graph ());

	    animatedMesh->graph ()[vertex].label = label;

	    for (unsigned frameId = 0;
		 frameId < animatedMesh->numFrames_; ++frameId)
	      {
		Eigen::VectorXd::Index offset =
		  frameId * animatedMesh->numVertices_ * 3 + vertex * 3;
		animatedMesh->graph ()[vertex].positions.push_back
		  (Vertex::position_t (animatedMesh->state_, offset, 3));
	      }
	  }

	unsigned frameId = 0;
	for (YAML::Iterator it = doc["frames"].begin ();
	     it != doc["frames"].end (); ++it)
	  {
	    const YAML::Node& node = *it;
	    checkNodeType (node, YAML::NodeType::Sequence);

	    if (frameId >= animatedMesh->numFrames_)
	      throw std::runtime_error
		("announced number of frames do not match data");

	    // Iterate over vertices.
	    vertex_descriptor_t vertex = 0;
	    for(YAML::Iterator it = node.begin (); it != node.end (); ++it)
	      {
		const YAML::Node& vertexNode = *it;

		checkNodeType (vertexNode, YAML::NodeType::Sequence);

		if (vertex >=
		    animatedMesh->numVertices_)
		  throw std::runtime_error
		    ("announced number of vertices do not match data");

		double x = 0., y = 0., z = 0.;
		vertexNode[0] >> x;
		vertexNode[1] >> y;
		vertexNode[2] >> z;

		animatedMesh->graph ()[vertex].positions[frameId][0] = x;
		animatedMesh->graph ()[vertex].positions[frameId][1] = y;
		animatedMesh->graph ()[vertex].positions[frameId][2] = z;
		++vertex;
	      }
	    ++frameId;
	  }

	if (parser.GetNextDocument(doc))
	  LOG4CXX_WARN (logger, "ignoring multiple documents in YAML file");
      }

      // Parse character file.
      {
	std::ifstream fin (characterFile.c_str ());
	if (!fin.good ())
	  throw std::runtime_error ("bad stream");
	YAML::Parser parser (fin);

	YAML::Node doc;

	if (!parser.GetNextDocument (doc))
	  throw std::runtime_error ("empty document");

	checkNodeType (doc, YAML::NodeType::Map);

	// Load edges.
	loadEdgesFromYaml (doc["edges"], animatedMesh);
	loadEdgesFromYaml (doc["extraMarkerEdges"], animatedMesh);
      }

      animatedMesh->computeVertexWeights ();
      return animatedMesh;
    }

    void
    AnimatedInteractionMesh::writeGraphvizGraphs (std::ostream& out,
						  unsigned i)
    {
      boost::write_graphviz
	(out, graph (),
	 InteractionMeshGraphVertexWriter<graph_t> (graph (), i),
	 InteractionMeshGraphEdgeWriter<graph_t> (graph (), i));

    }

    void
    AnimatedInteractionMesh::writeGraphvizGraphs (const std::string& path)
    {
      for (unsigned i = 0; i < numFrames (); ++i)
	{
	  std::ofstream graphvizFile
	    ((boost::format ("%1%/graph_%2%.dot")
	      % path % i).str().c_str ());
	  writeGraphvizGraphs (graphvizFile, i);
	}
    }

    AnimatedInteractionMeshShPtr_t
    AnimatedInteractionMesh::makeFromOptimizationVariables
    (const Eigen::Matrix<double, Eigen::Dynamic, 1>& x,
     AnimatedInteractionMeshShPtr_t previousAnimatedMesh)
    {
      AnimatedInteractionMeshShPtr_t animatedMesh =
	boost::make_shared<AnimatedInteractionMesh> ();

      animatedMesh->framerate_ = previousAnimatedMesh->framerate_;
      animatedMesh->numVertices_ = previousAnimatedMesh->numVertices_;
      animatedMesh->numFrames_ = previousAnimatedMesh->numFrames_;

      // This is what changes.
      animatedMesh->state_ = x;

      boost::copy_graph (previousAnimatedMesh->graph (),
			 animatedMesh->graph_);

      // Update positions using optimization vector.
      vertex_iterator_t vertexIt;
      vertex_iterator_t vertexEnd;
      boost::tie (vertexIt, vertexEnd) = boost::vertices
	(animatedMesh->graph ());
      unsigned vertexId = 0;
      for (; vertexIt != vertexEnd; ++vertexIt, ++vertexId)
	for (unsigned frameId = 0;
	     frameId < animatedMesh->numFrames_; ++frameId)
	  {
	    Eigen::VectorXd::Index offset =
	      frameId * animatedMesh->numVertices_ * 3 + vertexId * 3;
	    assert (offset <= animatedMesh->state ().size () - 3);

	    // use placement new to map to the new state
	    new (&animatedMesh->graph ()[*vertexIt].positions[frameId])
	      Vertex::position_t (animatedMesh->state_, offset, 3);
	  }

      // Update weights.
      animatedMesh->computeVertexWeights ();

      return animatedMesh;
    }

    void
    AnimatedInteractionMesh::writeTrajectory (const std::string& filename)
    {
      YAML::Emitter out;
      vertex_iterator_t vertexIt;
      vertex_iterator_t vertexEnd;

      out
	<< YAML::Comment("Marker motion data format version 1.0")
	<< YAML::BeginMap
	<< YAML::Key << "type" << YAML::Value << "MultiVector3Seq"
	<< YAML::Key << "content" << YAML::Value << "MarkerMotion"
	<< YAML::Key << "frameRate" << YAML::Value << framerate_
	<< YAML::Key << "numFrames" << YAML::Value << numFrames_
	<< YAML::Key << "partLabels" << YAML::Value
	<< YAML::BeginSeq
	;
      boost::tie (vertexIt, vertexEnd) = boost::vertices (graph ());
      for (; vertexIt != vertexEnd; ++vertexIt)
	out << graph ()[*vertexIt].label;
      out
	<< YAML::EndSeq
	<< YAML::Key << "numParts" << YAML::Value << numVertices_
	<< YAML::Key << "frames" << YAML::Value
	<< YAML::BeginSeq
	;
      for (unsigned frameId = 0; frameId < numFrames_; ++frameId)
	{
	  out << YAML::BeginSeq;
	  Eigen::VectorXd x = makeOptimizationVectorOneFrame (frameId);
	  for (unsigned i = 0; i < x.size (); ++i)
	    out << x[i];
	  out << YAML::EndSeq;
	}
      out
	<< YAML::EndSeq
	<< YAML::EndMap
	;

      std::ofstream file (filename.c_str ());
      file << out.c_str ();
    }

    void
    AnimatedInteractionMesh::computeVertexWeights ()
    {
      edge_iterator_t edgeIt;
      edge_iterator_t edgeEnd;

      for (unsigned frameId = 0; frameId < numFrames_; ++frameId)
	{
	  double weightSum = 0.;
	  boost::tie (edgeIt, edgeEnd) = boost::edges (graph ());
	  for (; edgeIt != edgeEnd; ++edgeIt)
	    {
	      Edge& edge = graph ()[*edgeIt];
	      const Vertex& source =
		graph ()[boost::source (*edgeIt, graph ())];
	      const Vertex& target =
		graph ()[boost::target (*edgeIt, graph ())];

	      LOG4CXX_TRACE(logger,
			    "--- edge ---\n"
			    << "source position: "
			    << source.positions[frameId][0] << " "
			    << source.positions[frameId][1] << " "
			    << source.positions[frameId][2] << "\n"
			    << "target position: "
			    << target.positions[frameId][0] << " "
			    << target.positions[frameId][1] << " "
			    << target.positions[frameId][2]);

	      if (edge.weight.size () != numFrames_)
		edge.weight.resize (numFrames_);

	      edge.weight[frameId] =
		(source.positions[frameId] - target.positions[frameId]
		 ).squaredNorm ();
	      if (edge.weight[frameId] == 0.)
		edge.weight[frameId] = 1.;
	      else
		edge.weight[frameId] = 1. / edge.weight[frameId];
	      weightSum += edge.weight[frameId];
	    }

	  // Normalize weights.
	  if (weightSum > 0.)
	    {
	      boost::tie (edgeIt, edgeEnd) = boost::edges (graph ());
	      for (; edgeIt != edgeEnd; ++edgeIt)
		graph ()[*edgeIt].weight[frameId] /= weightSum;
	    }
	}
    }

    void
    AnimatedInteractionMesh::recomputeCachedData ()
    {
      numVertices_ = (unsigned int)boost::num_vertices (graph ());
      numFrames_ = 0;
      if (!numVertices_)
	return;
      vertex_descriptor_t v = 0;
      numFrames_ = (unsigned int)graph ()[v].positions.size ();
      computeVertexWeights ();
    }

    void
    AnimatedInteractionMesh::computeInteractionMeshes ()
    {
      interactionMeshes_.resize (numFrames ());

      for (unsigned i = 0; i < numFrames (); ++i)
	computeInteractionMesh (i);
    }

    void
    AnimatedInteractionMesh::computeInteractionMesh (unsigned frameId)
    {
      typedef CGAL::Exact_predicates_inexact_constructions_kernel K;

      typedef CGAL::Triangulation_3<K>      Triangulation;

      typedef Triangulation::Cell_handle    Cell_handle;
      typedef Triangulation::Vertex_handle  Vertex_handle;
      typedef Triangulation::Locate_type    Locate_type;
      typedef Triangulation::Facet_circulator Facet_circulator;
      typedef Triangulation::Point          Point;
      typedef Triangulation::Triangle       Triangle;

      std::vector<Point>  points (numVertices ());
      vertex_iterator_t vertexIt;
      vertex_iterator_t vertexEnd;
      boost::tie (vertexIt, vertexEnd) = boost::vertices (graph ());
      for (; vertexIt != vertexEnd; ++vertexIt)
	points.push_back
	  (Point
	   (graph ()[*vertexIt].positions[frameId][0],
	    graph ()[*vertexIt].positions[frameId][1],
	    graph ()[*vertexIt].positions[frameId][2]));

      Triangulation triangulation (points.begin (), points.end());

      Triangulation::Finite_edges_iterator it =
	triangulation.finite_edges_begin ();
      Facet_circulator facetsEnd;
      for (; it != triangulation.finite_edges_end (); ++it)
	{
	  Facet_circulator facets = facetsEnd =
	    triangulation.incident_facets (*it);

	  do
	    {
	      Triangle triangle = triangulation.triangle (*facets);
	      for (unsigned i = 0; i < 3; ++i)
		{
		  vertex_descriptor_t source =
		    *getVertexFromPosition
		    (frameId,
		     triangle[i][0],
		     triangle[i][1],
		     triangle[i][2]);

		  // it is working because operator[i] access to i % 3
		  vertex_descriptor_t target =
		    *getVertexFromPosition
		    (
		     frameId,
		     triangle[i + 1][0],
		     triangle[i + 1][1],
		     triangle[i + 1][2]);
		  edge_descriptor_t edge;
		  bool ok;
		  boost::tie (edge, ok) =
		    boost::add_edge
		    (source, target, interactionMeshes_[frameId]);
		}
	      ++facets;
	    }
	  while (facets != facetsEnd);
	}
    }

  } // end of namespace retargeting.
} // end of namespace roboptim.
