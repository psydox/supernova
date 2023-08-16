//
// (c) 2023 Eduardo Doria.
//

#include "PhysicsSystem.h"
#include "Scene.h"
#include "util/Angle.h"

#include "box2d.h"


using namespace Supernova;


PhysicsSystem::PhysicsSystem(Scene* scene): SubSystem(scene){
	signature.set(scene->getComponentType<Transform>());

	this->scene = scene;

    this->world2D = NULL;
}

PhysicsSystem::~PhysicsSystem(){
    if (world2D){
        delete world2D;
        world2D = NULL;
    }
}

void PhysicsSystem::createBody2D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (!signature.test(scene->getComponentType<Body2DComponent>())){
        scene->addComponent<Body2DComponent>(entity, {});
    }
}

void PhysicsSystem::addRectangleShape2D(Entity entity, float width, float height){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].shape = new b2PolygonShape();
            body->shapes[body->numShapes].type = CollisionShape2DType::POLYGON;

            ((b2PolygonShape*)body->shapes[body->numShapes].shape)->SetAsBox(width, height);

            body->numShapes++;
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }
}

bool PhysicsSystem::loadBody2D(Body2DComponent& body){
    if (world2D && !body.body){
        b2BodyDef bodyDef;
        bodyDef.position.Set(0.0f, 0.0f);
        bodyDef.angle = 0.0f;
        bodyDef.type = b2_dynamicBody;

        body.body = world2D->CreateBody(&bodyDef);

        for (int i = 0; i < body.numShapes; i++){
            body.shapes[i].fixture = body.body->CreateFixture(body.shapes[i].shape, 1.0f);
        }

        return true;
    }

    return false;
}

void PhysicsSystem::destroyBody2D(Body2DComponent& body){
    // When a world leaves scope or is deleted by calling delete on a pointer
    // all the memory reserved for bodies, fixtures, and joints is freed
    if (world2D && body.body){
        for (int i = 0; i < body.numShapes; i++){
            body.body->DestroyFixture(body.shapes[i].fixture);

            body.shapes[i].shape = NULL;
            body.shapes[i].fixture = NULL;
        }

        world2D->DestroyBody(body.body);

        body.body = NULL;
    }
}

void PhysicsSystem::load(){
    if (!world2D){
        b2Vec2 gravity(0.0f, -10.0f);
        world2D = new b2World(gravity);
    }
}

void PhysicsSystem::destroy(){
    if (world2D){
        delete world2D;
        world2D = NULL;
    }
}

void PhysicsSystem::update(double dt){
	auto bodies2d = scene->getComponentArray<Body2DComponent>();
	for (int i = 0; i < bodies2d->size(); i++){
		Body2DComponent& body = bodies2d->getComponentFromIndex(i);
		Entity entity = bodies2d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        bool isNewBody = loadBody2D(body);

        if (signature.test(scene->getComponentType<Transform>())){
		    Transform& transform = scene->getComponent<Transform>(entity);

            if (isNewBody || transform.needUpdate){
                float scale = 1.0;

                b2Vec2 bPosition(transform.worldPosition.x / scale, transform.worldPosition.y / scale);
                body.body->SetTransform(bPosition, transform.worldRotation.getRoll());
            }
        }
    }

    if (bodies2d->size() > 0){
        int32 velocityIterations = 6;
        int32 positionIterations = 2;

        world2D->Step(dt, velocityIterations, positionIterations);
    }

	for (int i = 0; i < bodies2d->size(); i++){
		Body2DComponent& body = bodies2d->getComponentFromIndex(i);
		Entity entity = bodies2d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        b2Vec2 position = body.body->GetPosition();
        float angle = body.body->GetAngle();
        if (signature.test(scene->getComponentType<Transform>())){
		    Transform& transform = scene->getComponent<Transform>(entity);

            float scale = 1.0;

            Vector3 nPosition = Vector3(position.x * scale, position.y * scale, transform.worldPosition.z);
            if (transform.position != nPosition){
                transform.position = nPosition;
                transform.needUpdate = true;
            }

            if (transform.worldRotation.getRoll() != angle){
                Quaternion rotation;
                rotation.fromAngle(Angle::radToDefault(angle));

                transform.rotation = rotation;
                transform.needUpdate = true;
            }

        }
    }
}

void PhysicsSystem::draw(){

}

void PhysicsSystem::entityDestroyed(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentType<Body2DComponent>())){
        destroyBody2D(scene->getComponent<Body2DComponent>(entity));
    }
}