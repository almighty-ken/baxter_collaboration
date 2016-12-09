#include "flatpack_furniture/artag_ctrl.h"

#include <tf/transform_datatypes.h>

using namespace std;
using namespace baxter_collaboration;
using namespace baxter_core_msgs;

ARTagCtrl::ARTagCtrl(std::string _name, std::string _limb, bool _no_robot) :
                     ArmCtrl(_name,_limb, _no_robot), ARucoClient(_name, _limb)
{
    setHomeConfiguration();

    elap_time = 0;

    setState(START);

    insertAction(ACTION_GET,       static_cast<f_action>(&ARTagCtrl::pickObject));
    insertAction(ACTION_PASS,      static_cast<f_action>(&ARTagCtrl::passObject));
    insertAction(ACTION_HAND_OVER, static_cast<f_action>(&ARTagCtrl::handOver));

    // Let's override the recover_release action:
    // removeAction("recover_"+string(ACTION_RELEASE));
    insertAction("recover_"+string(ACTION_RELEASE),
                 static_cast<f_action>(&ARTagCtrl::recoverRelease));

    insertAction("recover_"+string(ACTION_GET),
                  static_cast<f_action>(&ARTagCtrl::recoverGet));

    // Not implemented actions throw a ROS_ERROR and return always false:
    insertAction("recover_"+string(ACTION_PASS),      &ARTagCtrl::notImplemented);
    insertAction("recover_"+string(ACTION_HAND_OVER), &ARTagCtrl::notImplemented);

    printActionDB();

    insertObject(17, "left leg");
    insertObject(21, "top");
    insertObject(24, "central frame");
    insertObject(26, "right leg");

    printObjectDB();

    if (_no_robot) return;

    if (!callAction(ACTION_HOME)) setState(ERROR);

    // moveArm("up",0.2,"strict");
    // moveArm("down",0.2,"strict");
    // moveArm("right",0.2,"strict");
    // moveArm("left",0.2,"strict");
    // moveArm("forward",0.1,"strict");
    // moveArm("backward",0.2,"strict");
    // moveArm("forward",0.1,"strict");
}

bool ARTagCtrl::pickObject()
{
    if (!hoverAbovePool())          return false;
    ros::Duration(0.05).sleep();
    if (!pickARTag())               return false;
    if (!gripObject())              return false;
    if (!moveArm("up", 0.4))        return false;
    if (!homePoseStrict())          return false;

    return true;
}

bool ARTagCtrl::recoverRelease()
{
    if (getSubState() != ACTION_RELEASE) return false;
    if(!homePoseStrict())         return false;
    ros::Duration(0.05).sleep();
    if (!pickARTag())                    return false;
    if (!gripObject())                   return false;
    if (!moveArm("up", 0.2))             return false;
    if (!homePoseStrict())        return false;

    return true;
}

bool ARTagCtrl::recoverGet()
{
    if (!hoverAbovePool())          return false;
    if (!releaseObject())           return false;
    if (!homePoseStrict())   return false;

    return true;
}

bool ARTagCtrl::passObject()
{
    if (getSubState() != ACTION_GET)    return false;
    if (!moveObjectTowardHuman())       return false;
    ros::Duration(1.0).sleep();
    if (!waitForForceInteraction())     return false;
    if (!releaseObject())               return false;
    if (!homePoseStrict())       return false;

    return true;
}

bool ARTagCtrl::handOver()
{
    if (getSubState() != ACTION_GET)  return false;
    if (!prepare4HandOver())          return false;
    ros::Duration(0.2).sleep();
    if (!waitForOtherArm(30.0, true)) return false;
    ros::Duration(0.8).sleep();
    if (!releaseObject())             return false;
    if (!moveArm("up", 0.05))         return false;
    if (!homePoseStrict())     return false;
    setSubState("");

    return true;
}

bool ARTagCtrl::prepare4HandOver()
{
    return moveArm("right", 0.32, "loose", true);
}

bool ARTagCtrl::waitForOtherArm(double _wait_time, bool disable_coll_av)
{
    ros::Time _init = ros::Time::now();

    string other_limb = getLimb() == "right" ? "left" : "right";

    ROS_INFO("[%s] Waiting for %s arm", getLimb().c_str(), other_limb.c_str());

    ros::ServiceClient _c;
    string service_name = "/"+getName()+"/service_"+other_limb+"_to_"+getLimb();
    _c = _n.serviceClient<AskFeedback>(service_name);

    AskFeedback srv;
    srv.request.ask = HAND_OVER_READY;

    ros::Rate r(100);
    while(RobotInterface::ok())
    {
        if (disable_coll_av)      suppressCollisionAv();
        if (!_c.call(srv)) break;

        ROS_DEBUG("[%s] Received: %s ", getLimb().c_str(), srv.response.reply.c_str());

        if (srv.response.reply == HAND_OVER_DONE)
        {
            return true;
        }

        r.sleep();

        if ((ros::Time::now()-_init).toSec() > _wait_time)
        {
            ROS_ERROR("[%s] No feedback from other arm has been received in %gs!",
                                                    getLimb().c_str(), _wait_time);
            return false;
        }
    }

    return false;
}

bool ARTagCtrl::pickARTag()
{
    ROS_INFO("[%s] Start Picking up tag..", getLimb().c_str());

    if (!is_ir_ok())
    {
        ROS_ERROR("No callback from the IR sensor! Stopping.");
        return false;
    }

    if (!waitForARucoData()) return false;

    geometry_msgs::Quaternion q;

    double x = getMarkerPos().x;
    double y = getMarkerPos().y + 0.04;
    double z =       getPos().z;

    ROS_DEBUG("Going to: %g %g %g", x, y, z);
    if (getObjectID() == 24)
    {
        // If we have to hand_over, let's pre-orient the end effector
        // such that further movements are easier
        q = computeHOorientation();

        if (!goToPose(x, y, z, q.x,q.y,q.z,q.w,"loose"))
        {
            return false;
        }
    }
    else
    {
        if (!goToPose(x, y, z, POOL_ORI_L,"loose"))
        {
            return false;
        }
    }

    if (!waitForARucoData()) return false;

    ros::Time start_time = ros::Time::now();
    double z_start       =       getPos().z;
    int cnt_ik_fail      =                0;

    ros::Rate r(100);
    while(RobotInterface::ok())
    {
        double new_elap_time = (ros::Time::now() - start_time).toSec();

        double x = getMarkerPos().x;
        double y = getMarkerPos().y;
        double z = z_start - ARM_SPEED * new_elap_time;

        ROS_DEBUG("Time %g Going to: %g %g %g", new_elap_time, x, y, z);

        bool res=false;

        if (getObjectID() == 24)
        {
            // q   = computeHOorientation();
            res = goToPoseNoCheck(x,y,z,q.x,q.y,q.z,q.w);
        }
        else
        {
            res = goToPoseNoCheck(x,y,z,POOL_ORI_L);
        }

        if (res == true)
        {
            cnt_ik_fail = 0;
            if (new_elap_time - elap_time > 0.02)
            {
                ROS_WARN("\t\t\t\t\tTime elapsed: %g", new_elap_time - elap_time);
            }
            elap_time = new_elap_time;

            if(hasCollided("strict"))
            {
                ROS_DEBUG("Collision!");
                return true;
            }

            r.sleep();
        }
        else
        {
            cnt_ik_fail++;
        }

        if (cnt_ik_fail == 10)
        {
            return false;
        }
    }

    return false;
}

geometry_msgs::Quaternion ARTagCtrl::computeHOorientation()
{
    // Get the rotation matrix for the marker, as retrieved from ARuco
    tf::Quaternion mrk_q;
    tf::quaternionMsgToTF(getMarkerOri(), mrk_q);
    tf::Matrix3x3 mrk_rot(mrk_q);

    // printf("Marker Orientation\n");
    // for (int j = 0; j < 3; ++j)
    // {
    //     printf("%g\t%g\t%g\n", mrk_rot[j][0], mrk_rot[j][1], mrk_rot[j][2]);
    // }

    // Compute the transform matrix between the marker's orientation
    // and the end-effector's orientation
    tf::Matrix3x3 mrk2ee;
    mrk2ee[0][0] =  1;  mrk2ee[0][1] =  0;   mrk2ee[0][2] =  0;
    mrk2ee[1][0] =  0;  mrk2ee[1][1] =  0;   mrk2ee[1][2] = -1;
    mrk2ee[2][0] =  0;  mrk2ee[2][1] =  1;   mrk2ee[2][2] =  0;

    // printf("Rotation\n");
    // for (int j = 0; j < 3; ++j)
    // {
    //     printf("%g\t%g\t%g\n", mrk2ee[j][0], mrk2ee[j][1], mrk2ee[j][2]);
    // }

    // Compute the final end-effector orientation, and convert it to a msg
    mrk2ee = mrk_rot * mrk2ee;

    tf::Quaternion ee_q;
    mrk2ee.getRotation(ee_q);
    geometry_msgs::Quaternion ee_q_msg;
    tf::quaternionTFToMsg(ee_q,ee_q_msg);

    // printf("Desired Orientation\n");
    // for (int j = 0; j < 3; ++j)
    // {
    //     printf("%g\t%g\t%g\n", mrk2ee[j][0], mrk2ee[j][1], mrk2ee[j][2]);
    // }

    return ee_q_msg;
}

void ARTagCtrl::setHomeConfiguration()
{
    setHomeConf(0.1967, -0.8702, -1.0531,  1.5578,
                         0.6516,  1.2464, -0.1787);
}

bool ARTagCtrl::hoverAbovePool()
{
    ROS_INFO("[%s] Hovering above pool..", getLimb().c_str());
    return goToPose(POOL_POS_L, POOL_ORI_L);
}

bool ARTagCtrl::moveObjectTowardHuman()
{
    ROS_INFO("[%s] Moving object toward human..", getLimb().c_str());
    return goToPose(0.80, 0.26, 0.32, HORIZONTAL_ORI_L);
}

void ARTagCtrl::setObjectID(int _obj)
{
    ArmCtrl::setObjectID(_obj);
    ARucoClient::setMarkerID(_obj);
}

ARTagCtrl::~ARTagCtrl()
{

}
