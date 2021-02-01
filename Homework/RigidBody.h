#pragma once

#include <Egg/Common.h>
#include <Egg/Math/Math.h>

#include "PxHelper.h"

using namespace Egg::Math;
using namespace physx;

namespace GG
{
	GG_CLASS(RigidBody)

		Float4x4 modelMatrix;
		Float3 position;

	public:

		int index;
		PxRigidDynamic* actor;

		RigidBody(int index, PxPhysics* gPhysics, PxScene* gScene, PxTransform pose, bool kinematic = false)
			: index{ index }
		{
			actor = gPhysics->createRigidDynamic(pose);
			actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, kinematic);
			gScene->addActor(*actor);
		}

		void AddShape(PxShape* shape) { actor->attachShape(*shape); }

		void Update(float dt)
		{
			PxTransform m = actor->getGlobalPose();

			float angle;
			Float3 axis;
			toRadiansAndUnitAxis(m.q, angle, ~axis);
			
			position = ~m.p;
			modelMatrix = Float4x4::Rotation(axis, angle) * Float4x4::Translation(position);
		}

		Float3   GetPosition()    { return position; }
		Float4x4 GetModelMatrix() { return modelMatrix; }
		Float4x4 GetModelMatrixInverse() { return modelMatrix.Invert(); }

	GG_ENDCLASS
}