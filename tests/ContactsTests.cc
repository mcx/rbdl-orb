#include <UnitTest++.h>

#include <iostream>

#include "rbdl/Logging.h"

#include "rbdl/Model.h"
#include "rbdl/Constraints.h"
#include "rbdl/Dynamics.h"
#include "rbdl/Kinematics.h"

#include "Fixtures.h"
#include "Human36Fixture.h"

using namespace std;
using namespace RigidBodyDynamics;
using namespace RigidBodyDynamics::Math;

const double TEST_PREC = 1.0e-11;

struct FixedBase6DoF9DoF {
  FixedBase6DoF9DoF () {
    ClearLogOutput();
    model = new Model;

    model->gravity = Vector3d  (0., -9.81, 0.);

    /* 3 DoF (rot.) joint at base
     * 3 DoF (rot.) joint child origin
     *
     *          X Contact point (ref child)
     *          |
     *    Base  |
     *   / body |
     *  O-------*
     *           \
     *             Child body
     */

    // base body (3 DoF)
    base = Body (
        1.,
        Vector3d (0.5, 0., 0.),
        Vector3d (1., 1., 1.)
        );
    joint_rotzyx = Joint (
        SpatialVector (0., 0., 1., 0., 0., 0.),
        SpatialVector (0., 1., 0., 0., 0., 0.),
        SpatialVector (1., 0., 0., 0., 0., 0.)
        );
    base_id = model->AddBody (0, Xtrans (Vector3d (0., 0., 0.)), joint_rotzyx, base);

    // child body 1 (3 DoF)
    child = Body (
        1.,
        Vector3d (0., 0.5, 0.),
        Vector3d (1., 1., 1.)
        );
    child_id = model->AddBody (base_id, Xtrans (Vector3d (0., 0., 0.)), joint_rotzyx, child);

    // child body (3 DoF)
    child_2 = Body (
        1.,
        Vector3d (0., 0.5, 0.),
        Vector3d (1., 1., 1.)
        );
    child_2_id = model->AddBody (child_id, Xtrans (Vector3d (0., 0., 0.)), joint_rotzyx, child_2);

    Q = VectorNd::Constant (model->mBodies.size() - 1, 0.);
    QDot = VectorNd::Constant (model->mBodies.size() - 1, 0.);
    QDDot = VectorNd::Constant (model->mBodies.size() - 1, 0.);
    Tau = VectorNd::Constant (model->mBodies.size() - 1, 0.);

    contact_body_id = child_id;
    contact_point = Vector3d  (0.5, 0.5, 0.);
    contact_normal = Vector3d  (0., 1., 0.);

    ClearLogOutput();
  }
  
  ~FixedBase6DoF9DoF () {
    delete model;
  }
  Model *model;

  unsigned int base_id, child_id, child_2_id;

  Body base, child, child_2;

  Joint joint_rotzyx;

  VectorNd Q;
  VectorNd QDot;
  VectorNd QDDot;
  VectorNd Tau;

  unsigned int contact_body_id;
  Vector3d contact_point;
  Vector3d contact_normal;
  ConstraintSet constraint_set;
};

TEST( TestExtendedConstraintFunctionsContact ){
  //Make a simple system for which we know the constraint forces
  //by construction and use this to test the newly added generic
  //functions to compute contraint forces, position errors, velocity errors
  //and Baumgarte forces

  Model model;
  model.gravity = Vector3d  (0., -9.81, 0.);
  Body boxBody (1., Vector3d (0., 0., 0.), Matrix3dIdentity);
  unsigned int boxId = model.AddBody (0, SpatialTransform(),
      Joint (
        SpatialVector (0., 0., 0., 1., 0., 0.),
        SpatialVector (0., 0., 0., 0., 1., 0.),
        SpatialVector (0., 0., 1., 0., 0., 0.)),
      boxBody);

  ConstraintSet cs;
  cs.AddContactConstraint(boxId,Vector3d(-0.5,0,0),Vector3d(0.,1.,0.),
                          "LeftCorner");
  cs.AddContactConstraint(boxId,Vector3d(-0.5,0,0),Vector3d(1.,0.,0.));
  cs.AddContactConstraint(boxId,Vector3d( 0.5,0,0),Vector3d(0.,1.,0.),
                          "RightCorner");
  cs.Bind(model);

  VectorNd qInit  =  VectorNd::Zero(model.dof_count);
  qInit[2] = M_PI/3.0;
  VectorNd qdInit = VectorNd::Zero(model.dof_count);
  VectorNd tau    = VectorNd::Zero(model.dof_count);

  VectorNd q =  VectorNd::Zero(model.dof_count);
  VectorNd qd = VectorNd::Zero(model.dof_count);
  VectorNd qdd = VectorNd::Zero(model.dof_count);

  VectorNd weights = VectorNd::Ones(model.dof_count);
  CalcAssemblyQ(model,qInit,cs,q,weights);
  CalcAssemblyQDot(model,q,qdInit,cs,qd,weights);

  ForwardDynamicsConstraintsDirect(model,q,qd,tau,cs,qdd);

  std::vector< unsigned int > bodyIds;
  std::vector< SpatialTransform > bodyFrames;
  std::vector< SpatialVector > constraintForces;

  VectorNd posErrors, velErrors, bgForces;

  unsigned int gIdxLeft = cs.getGroupIndexByName("LeftCorner");
  unsigned int gIdxRight = cs.getGroupIndexByName("RightCorner");

  // New functions to test
  //    calcForces
  //    calcPositionError
  //    calcVelocityError
  //    calcBaumgarteStabilizationForces
  //    isBaumgarteStabilizationEnabled
  //    getBaumgarteStabilizationCoefficients

  cs.calcForces(gIdxLeft,model,q,qd,bodyIds,bodyFrames,constraintForces);

  //ContactConstraints occur between a point on a body and the ground
  //The body always appears in the 0 index when calcForces is called
  //while the ground appears in the 1 index
  unsigned int idxBody = 0;
  unsigned int idxGround=1;

  CHECK(bodyIds[idxBody]==boxId); //First body is always the model body ContactConstraint
  CHECK(bodyIds[idxGround]==0);     //Second body is always ground for a ContactConstraint

  //Frames associated with the contacting body
  Vector3d r = Vector3d(-0.5,0.,0.);
  CHECK_ARRAY_CLOSE(bodyFrames[idxBody].r,r,3,TEST_PREC);
  Matrix3d eye = Matrix3dIdentity;
  for(unsigned int i=0; i<3;++i){
    for(unsigned int j=0; j<3;++j){
      CHECK_CLOSE(bodyFrames[idxBody].E(i,j),eye(i,j),TEST_PREC);
    }
  }

  //Frame associated with base frame
  r.setZero();

  CHECK_ARRAY_CLOSE(bodyFrames[idxGround].r,r,3,TEST_PREC);
  for(unsigned int i=0; i<3;++i){
    for(unsigned int j=0; j<3;++j){
      CHECK_CLOSE(bodyFrames[idxGround].E(i,j),eye(i,j),TEST_PREC);
    }
  }

  double fbody = 9.81*1.0*0.5*cos(q[2]);
  double fground= -9.81*1.0*0.5;
  unsigned int idxFy = 4;
  CHECK_CLOSE(constraintForces[idxBody  ][idxFy], fbody,TEST_PREC);
  CHECK_CLOSE(constraintForces[idxGround][idxFy], fground,TEST_PREC);


  VectorNd qErr = q;
  qErr[0] += 1.0;
  VectorNd posErrUpd;
  cs.calcPositionError(gIdxLeft,model,qErr,posErrUpd,true);
  CHECK_CLOSE(posErrUpd[0], 0.0,TEST_PREC);
  CHECK_CLOSE(posErrUpd[1], 0.0,TEST_PREC);

  VectorNd qdErr = qd;
  qdErr[0] += 1.0;
  VectorNd velErrUpd;
  cs.calcVelocityError(gIdxLeft,model,q,qdErr,velErrUpd,true);
  CHECK_CLOSE(velErrUpd[0],0.,TEST_PREC);
  CHECK_CLOSE(velErrUpd[1],1.0,TEST_PREC);

  Vector2d bgParams;
  cs.getBaumgarteStabilizationCoefficients(gIdxLeft,bgParams);
  CHECK_CLOSE(bgParams[0],10.,TEST_PREC);
  CHECK_CLOSE(bgParams[1],10.,TEST_PREC);

  bool bgEnabled = cs.isBaumgarteStabilizationEnabled(gIdxLeft);
  CHECK(bgEnabled == false);

  cs.calcBaumgarteStabilizationForces(gIdxLeft,model,posErrUpd,velErrUpd,
                                      bgForces);
  double bgForcesX = -2*bgParams[0]*velErrUpd[1];
  CHECK_CLOSE(bgForces[1], bgForcesX, TEST_PREC);


  //Test calcForces but using the resolveAllInBaseFrame option
  cs.calcForces(gIdxLeft,model,q,qd,bodyIds,bodyFrames,constraintForces,true,true);

  CHECK(bodyIds[idxBody]  ==0); //First body is always the model body ContactConstraint
  CHECK(bodyIds[idxGround]==0);     //Second body is always ground for a ContactConstraint

  //Frames associated with the contacting body
  Matrix3d rotZ45 = rotz(q[2]);
  r = rotZ45.transpose()*Vector3d(-0.5,0.,0.);
  CHECK_ARRAY_CLOSE(bodyFrames[idxBody].r,r,3,TEST_PREC);

  for(unsigned int i=0; i<3;++i){
    for(unsigned int j=0; j<3;++j){
      CHECK_CLOSE(bodyFrames[idxBody].E(i,j),eye(i,j),TEST_PREC);
    }
  }

  //Frame associated with base frame
  r.setZero();
  CHECK_ARRAY_CLOSE(bodyFrames[idxGround].r,r,3,TEST_PREC);
  for(unsigned int i=0; i<3;++i){
    for(unsigned int j=0; j<3;++j){
      CHECK_CLOSE(bodyFrames[idxGround].E(i,j),eye(i,j),TEST_PREC);
    }
  }

  fbody = 9.81*1.0*0.5;
  fground= -9.81*1.0*0.5;
  idxFy = 4;
  CHECK_CLOSE(constraintForces[idxBody  ][idxFy], fbody,TEST_PREC);
  CHECK_CLOSE(constraintForces[idxGround][idxFy], fground,TEST_PREC);


}



// 
// ForwardDynamicsConstraintsDirect 
// 
TEST ( TestForwardDynamicsConstraintsDirectSimple ) {
  Model model;
  model.gravity = Vector3d  (0., -9.81, 0.);
  Body base_body (1., Vector3d (0., 0., 0.), Vector3d (1., 1., 1.));
  unsigned int base_body_id = model.AddBody (0, SpatialTransform(), 
      Joint (
        SpatialVector (0., 0., 0., 1., 0., 0.),
        SpatialVector (0., 0., 0., 0., 1., 0.),
        SpatialVector (0., 0., 0., 0., 0., 1.),
        SpatialVector (0., 0., 1., 0., 0., 0.),
        SpatialVector (0., 1., 0., 0., 0., 0.),
        SpatialVector (1., 0., 0., 0., 0., 0.)
        ),
      base_body);

  VectorNd Q = VectorNd::Constant ((size_t) model.dof_count, 0.);
  VectorNd QDot = VectorNd::Constant ((size_t) model.dof_count, 0.);
  VectorNd QDDot = VectorNd::Constant  ((size_t) model.dof_count, 0.);
  VectorNd Tau = VectorNd::Constant ((size_t) model.dof_count, 0.);

  Q[1] = 1.;
  QDot[0] = 1.;
  QDot[3] = -1.;

  unsigned int contact_body_id = base_body_id;
  Vector3d contact_point ( 0., -1., 0.);

  ConstraintSet constraint_set;


  unsigned int id=11;
  unsigned int autoId =
      constraint_set.AddContactConstraint(contact_body_id, contact_point,
                                     Vector3d (1., 0., 0.),
                                      "ground_xyz",id);
  constraint_set.AddContactConstraint (contact_body_id, contact_point,
                                       Vector3d (0., 1., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point,
                                       Vector3d (0., 0., 1.));

  constraint_set.Bind (model);

  unsigned int index = constraint_set.getGroupIndexByName("ground_xyz");
  CHECK(index==0);
  index = constraint_set.getGroupIndexById(id);
  CHECK(index==0);
  index = constraint_set.getGroupIndexByAssignedId(autoId);
  CHECK(index==0);

  const char* conNameBack = constraint_set.getGroupName(index);
  CHECK(std::strcmp(conNameBack,"ground_xyz")==0);
  unsigned userId = constraint_set.getGroupId(index);
  CHECK(userId == id);

  ClearLogOutput();

//  cout << constraint_set.acceleration.transpose() << endl;
  ForwardDynamicsConstraintsDirect (model, Q, QDot, Tau, constraint_set, QDDot);

//  cout << "A = " << endl << constraint_set.A << endl << endl;
//  cout << "H = " << endl << constraint_set.H << endl << endl;
//  cout << "b = " << endl << constraint_set.b << endl << endl;
//  cout << "x = " << endl << constraint_set.x << endl << endl;
//  cout << constraint_set.b << endl;
//  cout << "QDDot = " << QDDot.transpose() << endl;

  Vector3d point_acceleration = CalcPointAcceleration (model, Q, QDot, QDDot, contact_body_id, contact_point);

  CHECK_ARRAY_CLOSE (
      Vector3d (0., 0., 0.).data(),
      point_acceleration.data(),
      3,
      TEST_PREC
      );

  // cout << "LagrangianSimple Logoutput Start" << endl;
  // cout << LogOutput.str() << endl;
  // cout << "LagrangianSimple Logoutput End" << endl;
}

TEST ( TestForwardDynamicsConstraintsDirectMoving ) {
  Model model;
  model.gravity = Vector3d  (0., -9.81, 0.);
  Body base_body (1., Vector3d (0., 0., 0.), Vector3d (1., 1., 1.));
  unsigned int base_body_id = model.AddBody (0, SpatialTransform(), 
      Joint (
        SpatialVector (0., 0., 0., 1., 0., 0.),
        SpatialVector (0., 0., 0., 0., 1., 0.),
        SpatialVector (0., 0., 0., 0., 0., 1.),
        SpatialVector (0., 0., 1., 0., 0., 0.),
        SpatialVector (0., 1., 0., 0., 0., 0.),
        SpatialVector (1., 0., 0., 0., 0., 0.)
        ),
      base_body);


  VectorNd Q = VectorNd::Constant ((size_t) model.dof_count, 0.);
  VectorNd QDot = VectorNd::Constant ((size_t) model.dof_count, 0.);
  VectorNd QDDot = VectorNd::Constant  ((size_t) model.dof_count, 0.);
  VectorNd Tau = VectorNd::Constant ((size_t) model.dof_count, 0.);

  Q[0] = 0.1;
  Q[1] = 0.2;
  Q[2] = 0.3;
  Q[3] = 0.4;
  Q[4] = 0.5;
  Q[5] = 0.6;
  QDot[0] = 1.1;
  QDot[1] = 1.2;
  QDot[2] = 1.3;
  QDot[3] = -1.4;
  QDot[4] = -1.5;
  QDot[5] = -1.6;

  unsigned int contact_body_id = base_body_id;
  Vector3d contact_point ( 0., -1., 0.);

  ConstraintSet constraint_set;

  constraint_set.AddContactConstraint(contact_body_id, contact_point,
                                      Vector3d (1., 0., 0.),
                                      "ground_xyz");
  constraint_set.AddContactConstraint (contact_body_id, contact_point,
                                       Vector3d (0., 1., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point,
                                       Vector3d (0., 0., 1.));

  constraint_set.Bind (model);

  ClearLogOutput();

  ForwardDynamicsConstraintsDirect (model, Q, QDot, Tau, constraint_set, QDDot);

  Vector3d point_acceleration = CalcPointAcceleration (model, Q, QDot, QDDot,
                                                       contact_body_id,
                                                       contact_point);

  CHECK_ARRAY_CLOSE (
      Vector3d (0., 0., 0.).data(),
      point_acceleration.data(),
      3,
      TEST_PREC
      );

  // cout << "LagrangianSimple Logoutput Start" << endl;
  // cout << LogOutput.str() << endl;
  // cout << "LagrangianSimple Logoutput End" << endl;
}

// 
// ForwardDynamicsContacts
// 
TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsSingleContact) {
  contact_normal.set (0., 1., 0.);
  constraint_set.AddContactConstraint (contact_body_id, contact_point, contact_normal);
  ConstraintSet constraint_set_lagrangian = constraint_set.Copy();

  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Vector3d point_accel_lagrangian, point_accel_contacts;
  
  ClearLogOutput();

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();
  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
  ClearLogOutput();
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);
//  cout << LogOutput.str() << endl;
  ClearLogOutput();

  point_accel_lagrangian = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point, true);
  point_accel_contacts = CalcPointAcceleration (*model, Q, QDot, QDDot_contacts, contact_body_id, contact_point, true);

  CHECK_CLOSE (constraint_set_lagrangian.force[0], constraint_set.force[0], TEST_PREC);
  CHECK_CLOSE (contact_normal.dot(point_accel_lagrangian), contact_normal.dot(point_accel_contacts), TEST_PREC);
  CHECK_ARRAY_CLOSE (point_accel_lagrangian.data(), point_accel_contacts.data(), 3, TEST_PREC);
  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts.data(), QDDot_lagrangian.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsSingleContactRotated) {
  Q[0] = 0.6;
  Q[3] =   M_PI * 0.6;
  Q[4] = 0.1;

  contact_normal.set (0., 1., 0.);
  
  constraint_set.AddContactConstraint (contact_body_id, contact_point, contact_normal);
  ConstraintSet constraint_set_lagrangian = constraint_set.Copy();
  
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Vector3d point_accel_lagrangian, point_accel_contacts, point_accel_contacts_opt;
  
  ClearLogOutput();

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts_opt = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();
  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts_opt);

  point_accel_lagrangian = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point, true);
  point_accel_contacts_opt = CalcPointAcceleration (*model, Q, QDot, QDDot_contacts_opt, contact_body_id, contact_point, true);

  CHECK_CLOSE (constraint_set_lagrangian.force[0], constraint_set.force[0], TEST_PREC);
  CHECK_CLOSE (contact_normal.dot(point_accel_lagrangian), contact_normal.dot(point_accel_contacts_opt), TEST_PREC);
  CHECK_ARRAY_CLOSE (point_accel_lagrangian.data(), point_accel_contacts_opt.data(), 3, TEST_PREC);
  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts_opt.data(), QDDot_lagrangian.size(), TEST_PREC);
}

// 
// Similiar to the previous test, this test compares the results of 
//   - ForwardDynamicsConstraintsDirect
//   - ForwardDynamcsContactsOpt
// for the example model in FixedBase6DoF and a moving state (i.e. a
// nonzero QDot)
// 
TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsSingleContactRotatedMoving) {
  Q[0] = 0.6;
  Q[3] =   M_PI * 0.6;
  Q[4] = 0.1;

  QDot[0] = -0.3;
  QDot[1] = 0.1;
  QDot[2] = -0.5;
  QDot[3] = 0.8;

  contact_normal.set (0., 1., 0.);
  constraint_set.AddContactConstraint (contact_body_id, contact_point, contact_normal);
  ConstraintSet constraint_set_lagrangian = constraint_set.Copy();
  
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Vector3d point_accel_lagrangian, point_accel_contacts;
  
  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();
  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
//  cout << LogOutput.str() << endl;
  ClearLogOutput();
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);
//  cout << LogOutput.str() << endl;

  point_accel_lagrangian = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point, true);
  point_accel_contacts = CalcPointAcceleration (*model, Q, QDot, QDDot_contacts, contact_body_id, contact_point, true);

  // check whether FDContactsLagrangian and FDContactsOld match
  CHECK_CLOSE (constraint_set_lagrangian.force[0], constraint_set.force[0], TEST_PREC);

  CHECK_CLOSE (contact_normal.dot(point_accel_lagrangian), contact_normal.dot(point_accel_contacts), TEST_PREC);
  CHECK_ARRAY_CLOSE (point_accel_lagrangian.data(), point_accel_contacts.data(), 3, TEST_PREC);
  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts.data(), QDDot_lagrangian.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsOptDoubleContact) {
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, Vector3d (1., 0., 0.), contact_normal);
  constraint_set.AddContactConstraint (contact_body_id, Vector3d (0., 1., 0.), contact_normal);
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Vector3d point_accel_lagrangian, point_accel_contacts;
  
  ClearLogOutput();

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();

  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);

  point_accel_lagrangian = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point, true);
  point_accel_contacts = CalcPointAcceleration (*model, Q, QDot, QDDot_contacts, contact_body_id, contact_point, true);

  // check whether FDContactsLagrangian and FDContacts match
  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  // check whether the point accelerations match
  CHECK_ARRAY_CLOSE (point_accel_lagrangian.data(), point_accel_contacts.data(), 3, TEST_PREC);

  // check whether the generalized accelerations match
  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts.data(), QDDot_lagrangian.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsOptDoubleContactRepeated) {
  // makes sure that all variables in the constraint set gets reset
  // properly when making repeated calls to ForwardDynamicsContacts.
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, Vector3d (1., 0., 0.), contact_normal);
  constraint_set.AddContactConstraint (contact_body_id, Vector3d (0., 1., 0.), contact_normal);
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Vector3d point_accel_lagrangian, point_accel_contacts;
  
  ClearLogOutput();

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();

  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
  // Call ForwardDynamicsContacts multiple times such that old values might
  // be re-used and thus cause erroneus values.
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);

  point_accel_lagrangian = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point, true);
  point_accel_contacts = CalcPointAcceleration (*model, Q, QDot, QDDot_contacts, contact_body_id, contact_point, true);

  // check whether FDContactsLagrangian and FDContacts match
  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  // check whether the point accelerations match
  CHECK_ARRAY_CLOSE (point_accel_lagrangian.data(), point_accel_contacts.data(), 3, TEST_PREC);

  // check whether the generalized accelerations match
  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts.data(), QDDot_lagrangian.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF, ForwardDynamicsContactsOptMultipleContact) {
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (1., 0., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (0., 1., 0.));
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);

  // we rotate the joints so that we have full mobility at the contact
  // point:
  //
  //  O       X (contact point)
  //   \     /
  //    \   /
  //     \ /
  //      *      
  //

  Q[0] = M_PI * 0.25;
  Q[1] = 0.2;
  Q[3] = M_PI * 0.5;

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  VectorNd QDDot_contacts = VectorNd::Constant (model->mBodies.size() - 1, 0.);
  
  ClearLogOutput();
  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot_contacts);

//  cout << LogOutput.str() << endl;

  Vector3d point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot, contact_body_id, contact_point);
//  cout << "point_accel_c = " << point_accel_c.transpose() << endl;

//  cout << "Lagrangian contact force " << contact_data_lagrangian[0].force << ", " << contact_data_lagrangian[1].force << endl;

  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot_contacts.data(), QDDot_lagrangian.size(), TEST_PREC);

  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF9DoF, ForwardDynamicsContactsOptMultipleContactsMultipleBodiesMoving) {
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (1., 0., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (0., 1., 0.));
  constraint_set.AddContactConstraint (child_2_id, contact_point, Vector3d (0., 1., 0.));
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);
  
  Q[0] = 0.1;
  Q[1] = -0.1;
  Q[2] = 0.1;
  Q[3] = -0.1;
  Q[4] = -0.1;
  Q[5] = 0.1;

  QDot[0] =  1.; 
  QDot[1] = -1.;
  QDot[2] =  1; 
  QDot[3] = -1.5; 
  QDot[4] =  1.5; 
  QDot[5] = -1.5; 

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);

  ClearLogOutput();
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot);
//  cout << LogOutput.str() << endl;

  Vector3d point_accel_c, point_accel_2_c;

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot, child_2_id, contact_point);

//  cout << "point_accel_c = " << point_accel_c.transpose() << endl;

  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
//  cout << "Lagrangian contact force " << contact_data_lagrangian[0].force << ", " << contact_data_lagrangian[1].force << ", " << contact_data_lagrangian[2].force << endl;

  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, child_2_id, contact_point);

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot.data(), QDDot.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF9DoF, ForwardDynamicsContactsOptMultipleContactsMultipleBodiesMovingAlternate) {
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (1., 0., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (0., 1., 0.));
  constraint_set.AddContactConstraint (child_2_id, contact_point, Vector3d (0., 1., 0.));
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);

  Q[0] = 0.1;
  Q[1] = -0.3;
  Q[2] = 0.15;
  Q[3] = -0.21;
  Q[4] = -0.81;
  Q[5] = 0.11;
  Q[6] = 0.31;
  Q[7] = -0.91;
  Q[8] = 0.61;

  QDot[0] =  1.3; 
  QDot[1] = -1.7;
  QDot[2] =  3; 
  QDot[3] = -2.5; 
  QDot[4] =  1.5; 
  QDot[5] = -5.5; 
  QDot[6] =  2.5; 
  QDot[7] = -1.5; 
  QDot[8] = -3.5; 

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->mBodies.size() - 1, 0.);

  ClearLogOutput();
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot);
//  cout << LogOutput.str() << endl;

  Vector3d point_accel_c, point_accel_2_c;

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot, child_2_id, contact_point);

//  cout << "point_accel_c = " << point_accel_c.transpose() << endl;

  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
//  cout << "Lagrangian contact force " << contact_data_lagrangian[0].force << ", " << contact_data_lagrangian[1].force << ", " << contact_data_lagrangian[2].force << endl;

  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, child_2_id, contact_point);

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot.data(), QDDot.size(), TEST_PREC);
}

TEST_FIXTURE (FixedBase6DoF12DoFFloatingBase, ForwardDynamicsContactsMultipleContactsFloatingBase) {
  ConstraintSet constraint_set_lagrangian;

  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (1., 0., 0.));
  constraint_set.AddContactConstraint (contact_body_id, contact_point, Vector3d (0., 1., 0.));
  constraint_set.AddContactConstraint (child_2_id, contact_point, Vector3d (0., 1., 0.));
  
  constraint_set_lagrangian = constraint_set.Copy();
  constraint_set_lagrangian.Bind (*model);
  constraint_set.Bind (*model);

  VectorNd QDDot_lagrangian = VectorNd::Constant (model->dof_count, 0.);

  Q[0] = 0.1;
  Q[1] = -0.3;
  Q[2] = 0.15;
  Q[3] = -0.21;
  Q[4] = -0.81;
  Q[5] = 0.11;
  Q[6] = 0.31;
  Q[7] = -0.91;
  Q[8] = 0.61;

  QDot[0] =  1.3; 
  QDot[1] = -1.7;
  QDot[2] =  3; 
  QDot[3] = -2.5; 
  QDot[4] =  1.5; 
  QDot[5] = -5.5; 
  QDot[6] =  2.5; 
  QDot[7] = -1.5; 
  QDot[8] = -3.5; 

  ClearLogOutput();
  ForwardDynamicsContactsKokkevis (*model, Q, QDot, Tau, constraint_set, QDDot);
//  cout << LogOutput.str() << endl;

  Vector3d point_accel_c, point_accel_2_c;

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot, child_2_id, contact_point);

//  cout << "point_accel_c = " << point_accel_c.transpose() << endl;

  ClearLogOutput();
  ForwardDynamicsConstraintsDirect (*model, Q, QDot, Tau, constraint_set_lagrangian, QDDot_lagrangian);
//  cout << "Lagrangian contact force " << contact_data_lagrangian[0].force << ", " << contact_data_lagrangian[1].force << ", " << contact_data_lagrangian[2].force << endl;
//  cout << LogOutput.str() << endl;

  CHECK_ARRAY_CLOSE (
      constraint_set_lagrangian.force.data(),
      constraint_set.force.data(),
      constraint_set.size(), TEST_PREC
      );

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  point_accel_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, contact_body_id, contact_point);
  point_accel_2_c = CalcPointAcceleration (*model, Q, QDot, QDDot_lagrangian, child_2_id, contact_point);

  CHECK_CLOSE (0., point_accel_c[0], TEST_PREC);
  CHECK_CLOSE (0., point_accel_c[1], TEST_PREC);
  CHECK_CLOSE (0., point_accel_2_c[1], TEST_PREC);

  CHECK_ARRAY_CLOSE (QDDot_lagrangian.data(), QDDot.data(), QDDot.size(), TEST_PREC);
}

TEST_FIXTURE (Human36, ForwardDynamicsContactsFixedBody) {
  VectorNd qddot_lagrangian (VectorNd::Zero(qddot.size()));
  VectorNd qddot_sparse (VectorNd::Zero(qddot.size()));

  randomizeStates();

  ConstraintSet constraint_upper_trunk;
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyUpperTrunk], Vector3d (1.1, 2.2, 3.3), Vector3d (1., 0., 0.));
  constraint_upper_trunk.Bind (*model_3dof);

  ForwardDynamicsConstraintsDirect (*model_3dof, q, qdot, tau, constraint_upper_trunk, qddot_lagrangian);
  ForwardDynamicsConstraintsRangeSpaceSparse (*model_3dof, q, qdot, tau, constraint_upper_trunk, qddot_sparse);
  ForwardDynamicsContactsKokkevis (*model_3dof, q, qdot, tau, constraint_upper_trunk, qddot);

  CHECK_ARRAY_CLOSE (qddot_lagrangian.data(), qddot.data(), qddot_lagrangian.size(), TEST_PREC * qddot_lagrangian.norm() * 10.);
  CHECK_ARRAY_CLOSE (qddot_lagrangian.data(), qddot_sparse.data(), qddot_lagrangian.size(), TEST_PREC * qddot_lagrangian.norm() * 10.);
}

TEST_FIXTURE (Human36, ForwardDynamicsContactsImpulses) {
  VectorNd qddot_lagrangian (VectorNd::Zero(qddot.size()));

  for (int i = 0; i < q.size(); i++) {
    q[i] = 0.5 * M_PI * static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
    qdot[i] = 0.5 * M_PI * static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
    tau[i] = 0.5 * M_PI * static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
    qddot_3dof[i] = 0.5 * M_PI * static_cast<double>(rand()) / static_cast<double>(RAND_MAX);
  }

  Vector3d heel_point (-0.03, 0., -0.03);

  ConstraintSet constraint_upper_trunk;
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootLeft], heel_point, Vector3d (1., 0., 0.));
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootLeft], heel_point, Vector3d (0., 1., 0.));
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootLeft], heel_point, Vector3d (0., 0., 1.));
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootRight], heel_point, Vector3d (1., 0., 0.));
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootRight], heel_point, Vector3d (0., 1., 0.));
  constraint_upper_trunk.AddContactConstraint (body_id_3dof[BodyFootRight], heel_point, Vector3d (0., 0., 1.));
  constraint_upper_trunk.Bind (*model_3dof);

  VectorNd qdotplus (VectorNd::Zero (qdot.size()));

  ComputeConstraintImpulsesDirect (*model_3dof, q, qdot, constraint_upper_trunk, qdotplus);  

  Vector3d heel_left_velocity = CalcPointVelocity (*model_3dof, q, qdotplus, body_id_3dof[BodyFootLeft], heel_point);
  Vector3d heel_right_velocity = CalcPointVelocity (*model_3dof, q, qdotplus, body_id_3dof[BodyFootRight], heel_point);

  CHECK_ARRAY_CLOSE (Vector3d(0., 0., 0.).data(), heel_left_velocity.data(), 3, TEST_PREC);
  CHECK_ARRAY_CLOSE (Vector3d(0., 0., 0.).data(), heel_right_velocity.data(), 3, TEST_PREC);
}
