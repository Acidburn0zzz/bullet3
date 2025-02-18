/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2015 Google Inc. http://bulletphysics.org

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
#include "InverseDynamicsExample.h"

#include "BulletDynamics/Featherstone/btMultiBodyLinkCollider.h"
#include "Bullet3Common/b3FileUtils.h"
#include "BulletDynamics/Featherstone/btMultiBodyJointMotor.h"
#include "BulletDynamics/Featherstone/btMultiBodyDynamicsWorld.h"
#include "../CommonInterfaces/CommonParameterInterface.h"
#include "../Utils/b3ResourcePath.h"
#include "../Importers/ImportURDFDemo/BulletUrdfImporter.h"
#include "../Importers/ImportURDFDemo/URDF2Bullet.h"
#include "../Importers/ImportURDFDemo/MyMultiBodyCreator.h"
#include "../CommonInterfaces/CommonMultiBodyBase.h"

#include "btBulletDynamicsCommon.h"

#include "LinearMath/btVector3.h"
#include "LinearMath/btAlignedObjectArray.h"

#include "../CommonInterfaces/CommonRigidBodyBase.h"

#include "BulletInverseDynamics/IDConfig.hpp"
#include "../Extras/InverseDynamics/btMultiBodyTreeCreator.hpp"

#include "../RenderingExamples/TimeSeriesCanvas.h"

#include <vector>

// the UI interface makes it easier to use static variables & free functions
// as parameters and callbacks
static btScalar kp =10*10;
static btScalar kd = 2*10;
static bool useInverseModel = true;
static std::vector<btScalar> qd;
static std::vector<std::string> qd_name;
static std::vector<std::string> q_name;

void toggleUseInverseModel(int buttonId, bool buttonState, void* userPointer){
    useInverseModel=!useInverseModel;
    // todo(thomas) is there a way to get a toggle button with changing text?
    b3Printf("switched inverse model %s", useInverseModel?"on":"off");
}

class InverseDynamicsExample : public CommonMultiBodyBase
{
    btInverseDynamicsExampleOptions m_option;
    btMultiBody* m_multiBody;
    btInverseDynamics::MultiBodyTree *m_inverseModel;
    TimeSeriesCanvas* m_timeSeriesCanvas;
public:
    InverseDynamicsExample(struct GUIHelperInterface* helper, btInverseDynamicsExampleOptions option);
    virtual ~InverseDynamicsExample();

    virtual void initPhysics();
    virtual void stepSimulation(float deltaTime);

    void setFileName(const char* urdfFileName);

    virtual void resetCamera()
    {
        float dist = 3.5;
        float pitch = -136;
        float yaw = 28;
        float targetPos[3]={0.47,0,-0.64};
        m_guiHelper->resetCamera(dist,pitch,yaw,targetPos[0],targetPos[1],targetPos[2]);
    }
};

InverseDynamicsExample::InverseDynamicsExample(struct GUIHelperInterface* helper, btInverseDynamicsExampleOptions option)
    :CommonMultiBodyBase(helper),
    m_option(option),
    m_multiBody(0),
    m_inverseModel(0),
    m_timeSeriesCanvas(0)
{
}

InverseDynamicsExample::~InverseDynamicsExample()
{
    delete m_multiBody;
    delete m_inverseModel;
    delete m_timeSeriesCanvas;
}

//todo(erwincoumans) Quick hack, reference to InvertedPendulumPDControl implementation. Will create a separate header/source file for this.
btMultiBody* createInvertedPendulumMultiBody(btMultiBodyDynamicsWorld* world, GUIHelperInterface* guiHelper, const btTransform& baseWorldTrans);

void InverseDynamicsExample::initPhysics()
{
    //roboticists like Z up
    int upAxis = 2;
    m_guiHelper->setUpAxis(upAxis);

    createEmptyDynamicsWorld();
    btVector3 gravity(0,0,0);
    gravity[upAxis]=-9.8;
    m_dynamicsWorld->setGravity(gravity);

    m_guiHelper->createPhysicsDebugDrawer(m_dynamicsWorld);

    {
        SliderParams slider("Kp",&kp);
        slider.m_minVal=0;
        slider.m_maxVal=2000;
        m_guiHelper->getParameterInterface()->registerSliderFloatParameter(slider);
    }
    {
        SliderParams slider("Kd",&kd);
        slider.m_minVal=0;
        slider.m_maxVal=50;
        m_guiHelper->getParameterInterface()->registerSliderFloatParameter(slider);
    }

    if (m_option == BT_ID_PROGRAMMATICALLY)
    {
        ButtonParams button("toggle inverse model",0,true);
        button.m_callback = toggleUseInverseModel;
        m_guiHelper->getParameterInterface()->registerButtonParameter(button);
    }

   
    switch (m_option)
    {
    case BT_ID_LOAD_URDF:
        {
            BulletURDFImporter u2b(m_guiHelper);
            bool loadOk =  u2b.loadURDF("kuka_lwr/kuka.urdf");
            if (loadOk)
            {
                    int rootLinkIndex = u2b.getRootLinkIndex();
                    b3Printf("urdf root link index = %d\n",rootLinkIndex);
                    MyMultiBodyCreator creation(m_guiHelper);
                    btTransform identityTrans;
                    identityTrans.setIdentity();
                    ConvertURDF2Bullet(u2b,creation, identityTrans,m_dynamicsWorld,true,u2b.getPathPrefix());
                    m_multiBody = creation.getBulletMultiBody();
                    if (m_multiBody)
                    {
                        //kuka without joint control/constraints will gain energy explode soon due to timestep/integrator
                        //temporarily set some extreme damping factors until we have some joint control or constraints
                        m_multiBody->setAngularDamping(0*0.99);
                        m_multiBody->setLinearDamping(0*0.99);
                        b3Printf("Root link name = %s",u2b.getLinkName(u2b.getRootLinkIndex()).c_str());
                    }
            }
            break;
        }
    case BT_ID_PROGRAMMATICALLY:
        {
            btTransform baseWorldTrans;
            baseWorldTrans.setIdentity();
            m_multiBody = createInvertedPendulumMultiBody(m_dynamicsWorld, m_guiHelper, baseWorldTrans);
            break;
        }
    default:
        {
            b3Error("Unknown option in InverseDynamicsExample::initPhysics");
            b3Assert(0);
        }
    };


    if(m_multiBody) {
        {
            m_timeSeriesCanvas = new TimeSeriesCanvas(m_guiHelper->getAppInterface()->m_2dCanvasInterface,512,230, "q time series");
            m_timeSeriesCanvas ->setupTimeSeries(3,100, 0);
           
        }
        
        // construct inverse model
        btInverseDynamics::btMultiBodyTreeCreator id_creator;
        if (-1 == id_creator.createFromBtMultiBody(m_multiBody, false)) {
            b3Error("error creating tree\n");
        } else {
            m_inverseModel = btInverseDynamics::CreateMultiBodyTree(id_creator);
        }
        // add joint target controls
        qd.resize(m_multiBody->getNumDofs());
        qd_name.resize(m_multiBody->getNumDofs());
       	q_name.resize(m_multiBody->getNumDofs()); 
	for(std::size_t dof=0;dof<qd.size();dof++) {
            qd[dof] = 0;
            char tmp[25];
            sprintf(tmp,"q_desired[%zu]",dof);
            qd_name[dof] = tmp;
            SliderParams slider(qd_name[dof].c_str(),&qd[dof]);
            slider.m_minVal=-3.14;
            slider.m_maxVal=3.14;
         sprintf(tmp,"q[%zu]",dof); 
		q_name[dof] = tmp;   
		m_guiHelper->getParameterInterface()->registerSliderFloatParameter(slider);
		m_timeSeriesCanvas->addDataSource(q_name[dof].c_str(), 255,0,0);
         }
        
        
    }
    
    

}


void InverseDynamicsExample::stepSimulation(float deltaTime)
{
    if(m_multiBody) {
        const int num_dofs=m_multiBody->getNumDofs();
        btInverseDynamics::vecx nu(num_dofs), qdot(num_dofs), q(num_dofs),joint_force(num_dofs);
        btInverseDynamics::vecx pd_control(num_dofs);

        // compute joint forces from one of two control laws:
        // 1) "computed torque" control, which gives perfect, decoupled,
        //    linear second order error dynamics per dof in case of a
        //    perfect model and (and negligible time discretization effects)
        // 2) decoupled PD control per joint, without a model
        for(int dof=0;dof<num_dofs;dof++) {
            q(dof) = m_multiBody->getJointPos(dof);
            qdot(dof)= m_multiBody->getJointVel(dof);

            const btScalar qd_dot=0;
            const btScalar qd_ddot=0;
            m_timeSeriesCanvas->insertDataAtCurrentTime(q[dof],dof,true);
            
            // pd_control is either desired joint torque for pd control,
            // or the feedback contribution to nu
            pd_control(dof) = kd*(qd_dot-qdot(dof)) + kp*(qd[dof]-q(dof));
            // nu is the desired joint acceleration for computed torque control
            nu(dof) = qd_ddot + pd_control(dof);

        }
        // calculate joint forces corresponding to desired accelerations nu
        if(-1 != m_inverseModel->calculateInverseDynamics(q,qdot,nu,&joint_force)) {
            for(int dof=0;dof<num_dofs;dof++) {
                if(useInverseModel) {
                    //joint_force(dof) += damping*dot_q(dof);
                    // use inverse model: apply joint force corresponding to
                    // desired acceleration nu
                    m_multiBody->addJointTorque(dof,joint_force(dof));
                } else {
                    // no model: just apply PD control law
                    m_multiBody->addJointTorque(dof,pd_control(dof));
                }
            }
        }
    }

    if (m_timeSeriesCanvas)
        m_timeSeriesCanvas->nextTick();

    //todo: joint damping for btMultiBody, tune parameters
    
    // step the simulation
    if (m_dynamicsWorld)
    {
        // todo(thomas) check that this is correct:
        // want to advance by 10ms, with 1ms timesteps
        m_dynamicsWorld->stepSimulation(1e-3,0);//,1e-3);
    }
}

CommonExampleInterface*    InverseDynamicsExampleCreateFunc(CommonExampleOptions& options)
{
    return new InverseDynamicsExample(options.m_guiHelper, btInverseDynamicsExampleOptions(options.m_option));
}



