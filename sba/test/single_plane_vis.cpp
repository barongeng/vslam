#include <ros/ros.h>
#include <sba/sba.h>
#include <sba/visualization.h>


// For random seed.
#include <time.h>

#include <visualization_msgs/Marker.h>

using namespace sba;
using namespace std;

const double PI = 3.141592;

int addPointAndProjection(SysSBA& sba, vector<Point, Eigen::aligned_allocator<Point> >& points, int ndi);
void calculateProj(SysSBA& sba, Point& point, int ndi, Vector3d& proj);

class Plane
{
  public:
    vector<Point, Eigen::aligned_allocator<Point> > points;
    Eigen::Vector3d normal;
    
    void rotate(const Eigen::Quaterniond& qrot)
    {
      Eigen::Matrix3d rotmat = qrot.toRotationMatrix();
      
      for (unsigned int i = 0; i < points.size(); i++)
      {
        points[i].start<3>() = rotmat*points[i].start<3>();
      }
      
      normal = rotmat*normal;
    }
    
    void rotate(double angle, double x, double y, double z)
    {
      Eigen::AngleAxis<double> angleaxis(angle, Vector3d(x, y, z));
      rotate(Eigen::Quaterniond(angleaxis));
    }
         
    void translate(const Eigen::Vector3d& trans)
    {
      for (unsigned int i = 0; i < points.size(); i++)
      {
        points[i].start<3>() += trans;
      }
    }
    
    void translate(double x, double y, double z)
    {
      Vector3d trans(x, y, z);
      translate(trans);
    }
    
    // Creates a plane with origin at (0,0,0) and opposite corner at (width, height, 0).
    void resize(double width, double height, int nptsx, int nptsy)
    {
      for (int ix = 0; ix < nptsx ; ix++)
      {
        for (int iy = 0; iy < nptsy ; iy++)
        {
          // Create a point on the plane in a grid.
          points.push_back(Point(width/nptsx*(ix+.5), -height/nptsy*(iy+.5), 0.0, 1.0));
        }
      }
      
      normal << 0, 0, -1;
    }
};


void setupSBA(SysSBA &sba)
{
    // Create camera parameters.
    frame_common::CamParams cam_params;
    cam_params.fx = 430; // Focal length in x
    cam_params.fy = 430; // Focal length in y
    cam_params.cx = 320; // X position of principal point
    cam_params.cy = 240; // Y position of principal point
    cam_params.tx = -30; // Baseline (no baseline since this is monocular)

    // Create a plane containing a wall of points.
    Plane plane0;
    plane0.resize(3, 2, 10, 5);
    plane0.rotate(PI/4.0, 1, 0, 0);
    plane0.translate(0.0, 0.0, 7.0);
    
    Plane plane1 = plane0;
    
    // Create nodes and add them to the system.
    unsigned int nnodes = 2; // Number of nodes.
    double path_length = 2; // Length of the path the nodes traverse.

    // Set the random seed.
    unsigned short seed = (unsigned short)time(NULL);
    seed48(&seed);
    
    for (int i = 0; i < nnodes; i++)
    { 
      // Translate in the x direction over the node path.
      Vector4d trans(i/(nnodes-1.0)*path_length, 0, 0, 1);
            
#if 0
      if (i >= 0)
	    {
	      // perturb a little
	      double tnoise = 0.5;	// meters
	      trans.x() += tnoise*(drand48()-0.5);
	      trans.y() += tnoise*(drand48()-0.5);
	      trans.z() += tnoise*(drand48()-0.5);
	    }
#endif

      // Don't rotate.
      Quaterniond rot(1, 0, 0, 0);
#if 0
      if (i > 0)
	    {
	      // perturb a little
	      double qnoise = 0.1;	// meters
	      rot.x() += qnoise*(drand48()-0.5);
	      rot.y() += qnoise*(drand48()-0.5);
	      rot.z() += qnoise*(drand48()-0.5);
	    }
#endif
      rot.normalize();
      
      // Add a new node to the system.
      sba.addNode(trans, rot, cam_params, false);
    }
    
    Vector3d imagenormal(0, 0, 1);
    
    Matrix3d covar0;
    covar0 << sqrt(imagenormal(0)), 0, 0, 0, sqrt(imagenormal(1)), 0, 0, 0, sqrt(imagenormal(2));
    Matrix3d covar;
    
    Quaterniond rotation;
    Matrix3d rotmat;
    
    // Project points into nodes.
    addPointAndProjection(sba, plane0.points, 0);
    addPointAndProjection(sba, plane1.points, 1);
    
    int offset = plane0.points.size();
    
    // Add point-plane matches
    for (int i = 0; i < sba.tracks.size(); i++)
    {
      Vector3d proj;
      Vector3d imagenormal(0, 0, 1);
      
      Matrix3d covar0;
      covar0 << sqrt(imagenormal(0)), 0, 0,
                0, sqrt(imagenormal(1)), 0, 
                0, 0, sqrt(imagenormal(2));
      Matrix3d covar;
      
      Quaterniond rotation;
      Matrix3d rotmat;
      int ndi, pointindex;
      Vector3d normal;
      Point pt;
      
      if (i >= offset)
      {
        ndi = 1;
        pointindex = i - offset;
        normal = plane1.normal;
        pt = plane0.points[i-offset];
      }
      else
      {
        ndi = 0;
        pointindex = i + offset;
        normal = plane0.normal;
        pt = plane1.points[i];
      }
      
      // Forward      
      calculateProj(sba, pt, ndi, proj);
      
      printf("Proj: node: %d proj point: %d (%f %f %f) original pt: %d (%f %f %f)\n", 
        ndi, i, sba.tracks[i].point.x(), sba.tracks[i].point.y(), sba.tracks[i].point.z(), 
        pointindex, sba.tracks[pointindex].point.x(), sba.tracks[pointindex].point.y(), sba.tracks[pointindex].point.z());
      
      
      rotation.setFromTwoVectors(imagenormal, normal);
      rotation.normalize();
      rotmat = rotation.toRotationMatrix();
      covar = rotmat.transpose()*covar0*rotmat;
        
      if (isnan(rotmat(0)))
      { covar = covar0; }
      
      sba.addStereoProj(ndi, pointindex, proj);
      sba.setProjCovariance(ndi, pointindex, covar);
    }
    
    // Add noise to node position.
    
    double transscale = 2.0;
    double rotscale = 0.2;
    
    // Don't actually add noise to the first node, since it's fixed.
    for (int i = 1; i < sba.nodes.size(); i++)
    {
      Vector4d temptrans = sba.nodes[i].trans;
      Quaterniond tempqrot = sba.nodes[i].qrot;
      
      // Add error to both translation and rotation.
      temptrans.x() += transscale*(drand48() - 0.5);
      temptrans.y() += transscale*(drand48() - 0.5);
      temptrans.z() += transscale*(drand48() - 0.5);
      tempqrot.x() += rotscale*(drand48() - 0.5);
      tempqrot.y() += rotscale*(drand48() - 0.5);
      tempqrot.z() += rotscale*(drand48() - 0.5);
      tempqrot.normalize();
      
      sba.nodes[i].trans = temptrans;
      sba.nodes[i].qrot = tempqrot;
      
      // These methods should be called to update the node.
      sba.nodes[i].normRot();
      sba.nodes[i].setTransform();
      sba.nodes[i].setProjection();
      sba.nodes[i].setDr(true);
    }
}

int addPointAndProjection(SysSBA& sba, vector<Point, Eigen::aligned_allocator<Point> >& points, int ndi)
{
    // Define dimensions of the image.
    int maxx = 640;
    int maxy = 480;

    // Project points into nodes.
    for (int i = 0; i < points.size(); i++)
    {
      double pointnoise = 1.0;
  
      // Add points into the system, and add noise.
      // Add up to .5 points of noise.
      Vector4d temppoint = points[i];
      temppoint.x() += pointnoise*(drand48() - 0.5);
      temppoint.y() += pointnoise*(drand48() - 0.5);
      temppoint.z() += pointnoise*(drand48() - 0.5);
      int index = sba.addPoint(temppoint);
    
      Vector3d proj;
      calculateProj(sba, points[i], ndi, proj);
      
      // If valid (within the range of the image size), add the stereo 
      // projection to SBA.
      //if (proj.x() > 0 && proj.x() < maxx && proj.y() > 0 && proj.y() < maxy)
      //{
        sba.addStereoProj(ndi, index, proj);
      //}
    }
    
    
    return sba.tracks.size() - points.size();
}

void calculateProj(SysSBA& sba, Point& point, int ndi, Vector3d& proj)
{
    Vector2d proj2d;
    Vector3d pc, baseline;
    // Project the point into the node's image coordinate system.
    sba.nodes[ndi].setProjection();
    sba.nodes[ndi].project2im(proj2d, point);
    
    // Camera coords for right camera
    baseline << sba.nodes[ndi].baseline, 0, 0;
    pc = sba.nodes[ndi].Kcam * (sba.nodes[ndi].w2n*point - baseline); 
    proj.start<2>() = proj2d;
    proj(2) = pc(0)/pc(2);
}

void processSBA(ros::NodeHandle node)
{
    // Create publisher topics.
    ros::Publisher cam_marker_pub = node.advertise<visualization_msgs::Marker>("/sba/cameras", 1);
    ros::Publisher point_marker_pub = node.advertise<visualization_msgs::Marker>("/sba/points", 1);
    ros::spinOnce();
    
    //ROS_INFO("Sleeping for 2 seconds to publish topics...");
    ros::Duration(0.2).sleep();
    
    // Create an empty SBA system.
    SysSBA sba;
    
    setupSBA(sba);
    
    // Provide some information about the data read in.
    unsigned int projs = 0;
    // For debugging.
    for (int i = 0; i < (int)sba.tracks.size(); i++)
    {
      projs += sba.tracks[i].projections.size();
    }
    ROS_INFO("SBA Nodes: %d, Points: %d, Projections: %d", (int)sba.nodes.size(),
      (int)sba.tracks.size(), projs);
        
    //ROS_INFO("Sleeping for 5 seconds to publish pre-SBA markers.");
    //ros::Duration(5.0).sleep();
        
    // Perform SBA with 10 iterations, an initial lambda step-size of 1e-3, 
    // and using CSPARSE.
    sba.doSBA(20, 1e-4, SBA_SPARSE_CHOLESKY);
    
    int npts = sba.tracks.size();

    ROS_INFO("Bad projs (> 10 pix): %d, Cost without: %f", 
        (int)sba.countBad(10.0), sqrt(sba.calcCost(10.0)/npts));
    ROS_INFO("Bad projs (> 5 pix): %d, Cost without: %f", 
        (int)sba.countBad(5.0), sqrt(sba.calcCost(5.0)/npts));
    ROS_INFO("Bad projs (> 2 pix): %d, Cost without: %f", 
        (int)sba.countBad(2.0), sqrt(sba.calcCost(2.0)/npts));
    
    ROS_INFO("Cameras (nodes): %d, Points: %d",
        (int)sba.nodes.size(), (int)sba.tracks.size());
        
    // Publish markers
    drawGraph(sba, cam_marker_pub, point_marker_pub);
    ros::spinOnce();
    //ROS_INFO("Sleeping for 2 seconds to publish post-SBA markers.");
    ros::Duration(0.2).sleep();
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "sba_system_setup");
    
    ros::NodeHandle node;
    
    processSBA(node);
    ros::spinOnce();

    return 0;
}