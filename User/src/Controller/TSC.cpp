//
// Created by nimapng on 7/2/21.
//

#include "Controller/TSC.h"
#include "Timer.h"

#ifdef FIXED_BASE
bool fixedBase = true;
#else
bool fixedBase = false;
#endif
using namespace TSC;

TSC_IMPL::TSC_IMPL(string urdf_file, string srdf) : _robot(urdf_file, srdf, fixedBase), _iter(0) {
    Vec qpos(_robot.nq()), qvel(_robot.nv());
    qpos = _robot.homeConfigurations();
    qvel.setZero();
    _robot.computeAllData(qpos, qvel);

    mt_waist = new SE3MotionTask(_robot, "torso");
    mt_waist->Kp().diagonal() << 0, 0, 0, 500, 500, 500;
    mt_waist->Kd() = 2 * mt_waist->Kp().cwiseSqrt();
    mt_waist->weightMatrix() = 1000 * mt_waist->weightMatrix();
    mt_waist->SE3Ref() = _robot.frame_pose("torso");

    com = new CoMMotionTask(_robot, "com");
    com->weightMatrix() = 500 * com->weightMatrix();
    com->Kp() = 500 * Mat3::Identity();
    com->Kd() = 2 * com->Kp().cwiseSqrt();
    com->posRef() = _robot.CoM_pos();
    com->velRef().setZero();
    com->accRef().setZero();

    rt = new RegularizationTask(_robot, "RegularizationTask");
    rt->qaccWeight().diagonal().fill(1e-3);
    rt->forceWeight().diagonal().fill(1e-5);

    jointsNominalTask = new JointsNominalTask(_robot, "JointsNominalTask");
    jointsNominalTask->weightMatrix().setIdentity();
    jointsNominalTask->Kp().setIdentity();
    jointsNominalTask->Kp() = 100 * jointsNominalTask->Kp();
    jointsNominalTask->Kd().setIdentity();
    jointsNominalTask->Kd() = 0.5 * jointsNominalTask->Kd();
    jointsNominalTask->norminalPosition() = _robot.homeConfigurations().tail(_robot.na());

    angularMomentumTask = new AngularMomentumTask(_robot, "AngularMomentumTask");
    angularMomentumTask->weightMatrix().diagonal().fill(10);
    angularMomentumTask->Kp().diagonal().fill(100);
    angularMomentumTask->ref().setZero();
    angularMomentumTask->ref_dot().setZero();

    rf = new SE3MotionTask(_robot, "right_toe_roll");
    rf->Kp().diagonal() << 100, 100, 100, 500, 500, 500;
    rf->Kd() = 2 * rf->Kp().cwiseSqrt();
    rf->weightMatrix().diagonal() << 500, 500, 500, 1000, 1000, 1000;
    rf->SE3Ref() = _robot.frame_pose("right_toe_roll");
    lf = new SE3MotionTask(_robot, "left_toe_roll");
    lf->Kp() = 500 * Mat6::Identity();
    lf->Kd() = 2 * rf->Kp().cwiseSqrt();
    lf->weightMatrix() = 1000 * mt_waist->weightMatrix();
    lf->SE3Ref() = _robot.frame_pose("left_toe_roll");

    cpcstr = new ContactPointsConstraints(_robot, "cpcstr");
    cfcstr = new ContactForceConstraints(_robot, "cfcstr");
    closedChainsConstraints = new ClosedChainsConstraints(_robot, "ClosedChainsConstraints");
    actuatorLimit = new ActuatorLimit(_robot, "ActuatorLimit");

    qaccBound = new QaccBound(_robot, "QaccBound");
    qaccBound->lb().fill(-100);
    qaccBound->ub().fill(100);

    tsc = new TaskSpaceControl(_robot);
    if (!_robot.isFixedBase()) {
        tsc->addTask(mt_waist);
        tsc->addLinearConstraint(cpcstr);
        tsc->addLinearConstraint(cfcstr);
    } else {
        tsc->addTask(lf);
        tsc->addTask(rf);
    }
    tsc->addLinearConstraint(closedChainsConstraints);
    tsc->addTask(com);
    tsc->addTask(rt);
    tsc->addTask(jointsNominalTask);
    tsc->addTask(angularMomentumTask);
    tsc->addLinearConstraint(actuatorLimit);
    tsc->addLinearConstraint(qaccBound);

    contact_virtual_link.emplace_back("contact1");
    contact_virtual_link.emplace_back("contact2");
    contact_virtual_link.emplace_back("contact3");
    contact_virtual_link.emplace_back("contact4");
    contact_virtual_link.emplace_back("contact5");
    contact_virtual_link.emplace_back("contact6");
    contact_virtual_link.emplace_back("contact7");
    contact_virtual_link.emplace_back("contact8");
    _robot.setContactVirtualLink(contact_virtual_link);

    link_pairs.push_back(pair<string, string>("cp_left_achillies_rod", "cp_left_heel_spring"));
    link_pairs.push_back(pair<string, string>("cp_right_achillies_rod", "cp_right_heel_spring"));
    link_pairs.push_back(pair<string, string>("cp_left_toe_A_rod", "cp_left_toe_roll_A"));
    link_pairs.push_back(pair<string, string>("cp_left_toe_B_rod", "cp_left_toe_roll_B"));
    link_pairs.push_back(pair<string, string>("cp_right_toe_A_rod", "cp_right_toe_roll_A"));
    link_pairs.push_back(pair<string, string>("cp_right_toe_B_rod", "cp_right_toe_roll_B"));
    _robot.setConnectVirtualLink(link_pairs);

    /*spring_joints.push_back(pair<string, RealNum>("left_shin_joint", 600.0));
    spring_joints.push_back(pair<string, RealNum>("left_heel_spring_joint", 437.5));
    spring_joints.push_back(pair<string, RealNum>("right_shin_joint", 600.0));
    spring_joints.push_back(pair<string, RealNum>("right_heel_spring_joint", 437.5));
    _robot.setSpringJoints(spring_joints);*/

    cout << "tau limit: " << _robot.actuatorsEffortLimit().transpose() << endl;
}

TSC_IMPL::~TSC_IMPL() {
    delete mt_waist;
    delete com;
    delete rt;
    delete jointsNominalTask;
    delete angularMomentumTask;
    delete cpcstr;
    delete cfcstr;
    delete actuatorLimit;
    delete qaccBound;
    delete tsc;
    delete closedChainsConstraints;
}

void TSC_IMPL::setContactMask(const VecInt &mask) {
    _robot.setContactMask(mask);
    _mask = mask;
}

void TSC_IMPL::setContactVirtualLink(vector<string> &contact_virtual_link) {
    _robot.setContactVirtualLink(contact_virtual_link);
}

void TSC_IMPL::solve(ConstVecRef qpos, ConstVecRef qvel, const VecInt &mask) {
    _robot.computeAllData(qpos, qvel, mask);

    Mat T, T_dot;
    Vec par;
    T.resize(link_pairs.size() * 6, link_pairs.size());
    T.setZero();
    T_dot = T;
    for (int i = 0; i < link_pairs.size(); i++) {
        auto p = _robot.frame_pose(link_pairs[i].first).translation();
        auto s = _robot.frame_pose(link_pairs[i].second).translation();
        T.block<3, 1>(i * 6, i) = p - s;
        par = _robot.connectPointRelativeJacobia().middleRows(i * 6, 6) * qvel;
        T_dot.block<3, 1>(i * 6, i) = par.head(3);
    }

    _robot.setConstraintForceSubspace(T, T_dot);
    _robot.computeClosedChainTerm();

    if (_robot.isFixedBase()) {
        rf->SE3Ref().translation()(2) = -0.839273 + 0.10 * sin(0.004 * _iter);
        lf->SE3Ref().translation()(2) = -0.839273 - 0.10 * sin(0.004 * _iter);
    } else {
        com->posRef()(2) = 0.892442 + 0.05 * sin(0.004 * _iter);
    }
    tsc->solve();
//    tsc->saveAllData("data.txt");
}

void TSC_IMPL::run(const Reference &ref, const RobotState &state) {
    Vec qpos, qdot;
    if (_robot.isFixedBase()) {
        qpos = state.jointsState.qpos;
        qdot = state.jointsState.qvel;
    } else {
        qpos.resize(state.jointsState.qpos.size() + 7);
        Vec quat(4);
        quat << state.floatingBaseState.quat.x(),
                state.floatingBaseState.quat.y(),
                state.floatingBaseState.quat.z(),
                state.floatingBaseState.quat.w();
        qpos << state.floatingBaseState.pos,
                quat,
                state.jointsState.qpos;
        qdot.resize(state.jointsState.qvel.size() + 6);

        qdot << state.floatingBaseState.quat.toRotationMatrix().transpose() * state.floatingBaseState.vel,
                state.floatingBaseState.quat.toRotationMatrix().transpose() *
                state.floatingBaseState.omega, state.jointsState.qvel;
    }

    Timer timer;
    solve(qpos, qdot, _mask);
    cout << "time cost: " << timer.getMs() << " ms"<< endl;

    _jointsCmd.tau_ff = getOptimalTorque();
    /*auto tau = getOptimalTorque();
    _jointsCmd.tau_ff << tau.head(4), tau.segment(5, 2), tau.segment(9, 8), tau.segment(18, 2), tau.tail(4);*/
//    cout << "optimal qacc: " << getOptimalQacc().transpose() << endl;
//    cout << "optimal force: " << getOptimalContactForce().transpose() << endl;
//    cout << "optimal torque: " << _jointsCmd.tau_ff.transpose() << endl;
//    cout << "jointSpringForce:" << _robot.jointsSpringForce().transpose() << endl;
    _iter++;
}

const JointsCmd &TSC_IMPL::jointsCmd() {
    return _jointsCmd;
}
