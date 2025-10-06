//
// (c) 2025 Eduardo Doria.
//

#include "PhysicsSystem.h"
#include "Scene.h"
#include "util/Angle.h"

#include "util/Box2DAux.h"
#include "util/JoltPhysicsAux.h"


using namespace Supernova;


PhysicsSystem::PhysicsSystem(Scene* scene): SubSystem(scene){
	signature.set(scene->getComponentId<Body2DComponent>());

	this->scene = scene;

    this->gravity = Vector3(0, -9.81f, 0);
    this->pointsToMeterScale2D = 64.0;

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = {this->gravity.x, this->gravity.y};
    world2D = b2CreateWorld(&worldDef);

    b2World_SetPreSolveCallback(world2D, Box2DAux::PreSolve, scene);
    // TODO: could there be a performance issue by checking this often?
    b2World_SetCustomFilterCallback(world2D, Box2DAux::CollisionFilter, scene);

    // https://github.com/jrouwe/JoltPhysics/issues/244
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    //JPH::Trace = TraceImpl;
    //JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

    // Create a factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    const unsigned int cMaxBodies = 1024;
    const unsigned int cNumBodyMutexes = 0;
    const unsigned int cMaxBodyPairs = 1024;
    const unsigned int cMaxContactConstraints = 1024;

    broad_phase_layer_interface = new JPH::BroadPhaseLayerInterfaceMask(MAX_BROADPHASELAYER_3D);
    object_vs_broadphase_layer_filter = new JPH::ObjectVsBroadPhaseLayerFilterMask(*broad_phase_layer_interface);
    object_vs_object_layer_filter = new JPH::ObjectLayerPairFilterMask();

    // Now we can create the actual physics system.
    world3D.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *broad_phase_layer_interface, *object_vs_broadphase_layer_filter, *object_vs_object_layer_filter);
    world3D.SetGravity(JPH::Vec3(this->gravity.x, this->gravity.y, this->gravity.z));

    temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    job_system = new JPH::JobSystemThreadPool (JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, JPH::thread::hardware_concurrency() - 1);

    activationListener3D = new JoltActivationListener(scene, this);
    world3D.SetBodyActivationListener(activationListener3D);

    contactListener3D = new JoltContactListener(scene, this);
    world3D.SetContactListener(contactListener3D);

    lock3DBodies = true;
}

PhysicsSystem::~PhysicsSystem(){
    b2DestroyWorld(world2D);
    world2D = b2_nullWorldId;

    delete activationListener3D;
    delete contactListener3D;

    //delete world3D;
    delete temp_allocator;
    delete job_system;
    delete broad_phase_layer_interface;
    delete object_vs_broadphase_layer_filter;
    delete object_vs_object_layer_filter;

    //delete JPH::Factory::sInstance;
    //JPH::UnregisterTypes();
}

Vector3 PhysicsSystem::getGravity() const{
    return gravity;
}

void PhysicsSystem::setGravity(Vector3 gravity){
    if (this->gravity != gravity){
        this->gravity = gravity;
        b2World_SetGravity(world2D, {gravity.x, gravity.y});
        world3D.SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }
}

void PhysicsSystem::setGravity(float x, float y){
    setGravity(Vector3(x, y, 0));
}

void PhysicsSystem::setGravity(float x, float y, float z){
    setGravity(Vector3(x, y, z));
}

float PhysicsSystem::getPointsToMeterScale2D() const{
    return pointsToMeterScale2D;
}

void PhysicsSystem::setPointsToMeterScale2D(float pointsToMeterScale2D){
    this->pointsToMeterScale2D = pointsToMeterScale2D;
}


void PhysicsSystem::setLock3DBodies(bool lock3DBodies){
    this->lock3DBodies = lock3DBodies;
}

bool PhysicsSystem::isLock3DBodies() const{
    return this->lock3DBodies;
}

void PhysicsSystem::updateBody2DPosition(Signature signature, Entity entity, Body2DComponent& body){
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        if (b2Body_IsValid(body.body)){

            b2Vec2 bNewPosition = {transform.worldPosition.x / pointsToMeterScale2D, transform.worldPosition.y / pointsToMeterScale2D};
            float bNewAngle = Angle::defaultToRad(transform.worldRotation.getRoll());

            if (body.newBody && transform.needUpdate){
                bNewPosition = {transform.position.x / pointsToMeterScale2D, transform.position.y / pointsToMeterScale2D};
                bNewAngle = Angle::defaultToRad(transform.rotation.getRoll());
                if (transform.parent != NULL_ENTITY){
                    Log::warn("Body position and rotation cannot be obtained from world: %u (%s)", entity, scene->getEntityName(entity).c_str());
                }
            }

            b2Transform bTransform = b2Body_GetTransform(body.body);

            if (bTransform.p != bNewPosition || b2Rot_GetAngle(bTransform.q) != bNewAngle){
                b2Body_SetTransform(body.body, bNewPosition, b2MakeRot(bNewAngle));
                b2Body_SetAwake(body.body, true);
            }
        }
    }
}

void PhysicsSystem::updateBody3DPosition(Signature signature, Entity entity, Body3DComponent& body){
    if (signature.test(scene->getComponentId<Transform>())){
        Transform& transform = scene->getComponent<Transform>(entity);
        if (!body.body.IsInvalid()){
            JPH::Vec3 jNewPosition(transform.worldPosition.x, transform.worldPosition.y, transform.worldPosition.z);
            JPH::Quat jNewQuat(transform.worldRotation.x, transform.worldRotation.y, transform.worldRotation.z, transform.worldRotation.w);

            if (body.newBody && transform.needUpdate){
                jNewPosition = JPH::Vec3(transform.position.x, transform.position.y, transform.position.z);
                jNewQuat = JPH::Quat(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
                if (transform.parent != NULL_ENTITY){
                    Log::warn("Body position and rotation cannot be obtained from world: %u (%s)", entity, scene->getEntityName(entity).c_str());
                }
            }

            JPH::BodyInterface &body_interface = world3D.GetBodyInterfaceNoLock();
            JPH::Vec3 jPosition;
            JPH::Quat jQuat;
            body_interface.GetPositionAndRotation(body.body, jPosition, jQuat);

            if (jPosition != jNewPosition || jQuat != jNewQuat){
                body_interface.SetPositionAndRotation(body.body, jNewPosition, jNewQuat, JPH::EActivation::Activate);
            }
        }
    }
}

void PhysicsSystem::createGenericJoltBody(Entity entity, Body3DComponent& body, const JPH::ShapeRefC shape){
    JPH::ObjectLayer layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
    JPH::EMotionType joltType = JPH::EMotionType::Static;
    JPH::EActivation activation = JPH::EActivation::DontActivate;
    if (body.type == BodyType::DYNAMIC){
        layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
        joltType = JPH::EMotionType::Dynamic;
        activation = JPH::EActivation::Activate;
    }else if (body.type == BodyType::KINEMATIC){
        layer = JPH::ObjectLayerPairFilterMask::sGetObjectLayer(1);
        joltType = JPH::EMotionType::Kinematic;
        activation = JPH::EActivation::Activate;
    }

    JPH::BodyCreationSettings settings(shape, JPH::Vec3(0.0, 0.0, 0.0), JPH::Quat::sIdentity(), joltType, layer);

    JPH::BodyInterface &body_interface = world3D.GetBodyInterface();

    if(body.overrideMassProperties){
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
        settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3(body.solidBoxSize.x, body.solidBoxSize.y, body.solidBoxSize.z), body.solidBoxDensity);
    }

    JPH::Body* jbody = body_interface.CreateBody(settings);
    jbody->SetUserData(entity);
    //if (type != BodyType::STATIC){
    //    jbody->SetAllowSleeping(false);
    //}

    body.body = jbody->GetID();

    body_interface.AddBody(body.body, activation);
}

void PhysicsSystem::createBody2D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (!signature.test(scene->getComponentId<Body2DComponent>())){
        scene->addComponent<Body2DComponent>(entity, {});
        //loadBody2D(entity);
    }
}

void PhysicsSystem::removeBody2D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<Body2DComponent>())){
        destroyBody2D(scene->getComponent<Body2DComponent>(entity));
        scene->removeComponent<Body2DComponent>(entity);
    }
}

int PhysicsSystem::createBoxShape2D(Entity entity, float width, float height){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::POLYGON;

            b2Polygon polygon = { 0 };
            // same as shape.SetAsBox but using center on left corner
            polygon.count = 4;
            polygon.vertices[0] = { 0, 0 };
            polygon.vertices[1] = { width / pointsToMeterScale2D, 0 };
            polygon.vertices[2] = { width / pointsToMeterScale2D,  height / pointsToMeterScale2D };
            polygon.vertices[3] = { 0,  height / pointsToMeterScale2D };
            polygon.normals[0] = { 0.0f, -1.0f };
            polygon.normals[1] = { 1.0f, 0.0f };
            polygon.normals[2] = { 0.0f, 1.0f };
            polygon.normals[3] = { -1.0f, 0.0f };
            polygon.radius = 0.0f;
            polygon.centroid = b2Vec2_zero;

            return loadShape2D(*body, &polygon, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createCenteredBoxShape2D(Entity entity, float width, float height, Vector2 center, float angle){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::POLYGON;

            float halfW = width / 2.0 / pointsToMeterScale2D;
            float halfH = height / 2.0 / pointsToMeterScale2D;

            b2Polygon polygon = b2MakeOffsetBox(halfW, halfH, {center.x / pointsToMeterScale2D, center.y / pointsToMeterScale2D}, b2MakeRot(Angle::defaultToRad(angle)));

            return loadShape2D(*body, &polygon, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createRoundedBoxShape2D(Entity entity, float width, float height, float radius){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::POLYGON;

            float halfW = width / 2.0 / pointsToMeterScale2D;
            float halfH = height / 2.0 / pointsToMeterScale2D;
            float sRadius = radius / pointsToMeterScale2D;

            b2Polygon polygon = b2MakeRoundedBox(halfW, halfW, sRadius);

            return loadShape2D(*body, &polygon, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createPolygonShape2D(Entity entity, std::vector<Vector2> vertices){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::POLYGON;

            std::vector<b2Vec2> b2vertices(vertices.size());
            for (int i = 0; i < vertices.size(); i++){
                b2vertices[i] = {vertices[i].x / pointsToMeterScale2D, vertices[i].y / pointsToMeterScale2D};
            }
            b2Hull hull = b2ComputeHull(&b2vertices[0], (int)vertices.size());

            b2Polygon polygon = b2MakePolygon( &hull, 0.0f );

            return loadShape2D(*body, &polygon, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createCircleShape2D(Entity entity, Vector2 center, float radius){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::CIRCLE;

            b2Circle circle = { 0 };
            circle.center = {center.x / pointsToMeterScale2D, center.y / pointsToMeterScale2D};
            circle.radius = radius / pointsToMeterScale2D;

            return loadShape2D(*body, &circle, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createCapsuleShape2D(Entity entity, Vector2 center1, Vector2 center2, float radius){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::CAPSULE;

            b2Capsule capsule = { 0 };
            capsule.center1 = {center1.x / pointsToMeterScale2D, center1.y / pointsToMeterScale2D};
            capsule.center2 = {center2.x / pointsToMeterScale2D, center2.y / pointsToMeterScale2D};
            capsule.radius = radius / pointsToMeterScale2D;

            return loadShape2D(*body, &capsule, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createSegmentShape2D(Entity entity, Vector2 point1, Vector2 point2){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::SEGMENT;

            b2Segment segment = { 0 };
            segment.point1 = {point1.x / pointsToMeterScale2D, point1.y / pointsToMeterScale2D};
            segment.point2 = {point2.x / pointsToMeterScale2D, point2.y / pointsToMeterScale2D};

            return loadShape2D(*body, &segment, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createChainShape2D(Entity entity, std::vector<Vector2> vertices, bool loop){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){

            body->shapes[body->numShapes].type = Shape2DType::CHAIN;

            std::vector<b2Vec2> b2vertices(vertices.size());
            for (int i = 0; i < vertices.size(); i++){
                b2vertices[i] = {vertices[i].x / pointsToMeterScale2D, vertices[i].y / pointsToMeterScale2D};
            }

            b2ChainDef chainDef = b2DefaultChainDef();
            chainDef.points = &b2vertices[0];
            chainDef.count = (int)vertices.size();
            chainDef.isLoop = loop;

            return loadShape2D(*body, &chainDef, body->shapes[body->numShapes].type);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

void PhysicsSystem::removeAllShapes2D(Entity entity){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        for (int i = 0; i < body->numShapes; i++){
            destroyShape2D(*body, i);
        }
        body->numShapes = 0;
    }
}

void PhysicsSystem::createBody3D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (!signature.test(scene->getComponentId<Body3DComponent>())){
        scene->addComponent<Body3DComponent>(entity, {});
        //loadBody3D(entity);
    }
}

void PhysicsSystem::removeBody3D(Entity entity){
    Signature signature = scene->getSignature(entity);

    if (signature.test(scene->getComponentId<Body3DComponent>())){
        destroyBody3D(scene->getComponent<Body3DComponent>(entity));
        scene->removeComponent<Body3DComponent>(entity);
    }
}

int PhysicsSystem::createBoxShape3D(Entity entity, Vector3 position, Quaternion rotation, float width, float height, float depth){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::BOX;
            JPH::BoxShapeSettings shape_settings(JPH::Vec3(width/2.0, height/2.0, depth/2.0));

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createSphereShape3D(Entity entity, Vector3 position, Quaternion rotation, float radius){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::SPHERE;
            JPH::SphereShapeSettings shape_settings(radius);

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createCapsuleShape3D(Entity entity, Vector3 position, Quaternion rotation, float halfHeight, float radius){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::CAPSULE;
            JPH::CapsuleShapeSettings shape_settings(halfHeight, radius);

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createTaperedCapsuleShape3D(Entity entity, Vector3 position, Quaternion rotation, float halfHeight, float topRadius, float bottomRadius){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::TAPERED_CAPSULE;
            JPH::TaperedCapsuleShapeSettings shape_settings(halfHeight, topRadius, bottomRadius);

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createCylinderShape3D(Entity entity, Vector3 position, Quaternion rotation, float halfHeight, float radius){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::CYLINDER;
            JPH::CylinderShapeSettings shape_settings(halfHeight, radius);

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createConvexHullShape3D(Entity entity, Vector3 position, Quaternion rotation, std::vector<Vector3> vertices){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::CONVEX_HULL;

            JPH::Array<JPH::Vec3> jvertices;
            jvertices.resize(vertices.size());
            for (int i = 0; i < vertices.size(); i++){
                jvertices[i] = JPH::Vec3(vertices[i].x, vertices[i].y, vertices[i].z);
            }

            JPH::ConvexHullShapeSettings shape_settings(jvertices);

            return loadShape3D(*body, position, rotation, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createConvexHullShape3D(Entity entity, MeshComponent& mesh, Transform& transform){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::CONVEX_HULL;

            std::map<std::string, Buffer*> buffers;

            if (mesh.buffer.getSize() > 0){
                buffers["vertices"] = &mesh.buffer;
            }
            for (int i = 0; i < mesh.numExternalBuffers; i++){
                buffers[mesh.eBuffers[i].getName()] = &mesh.eBuffers[i];
            }

            Buffer* vertexBuffer = NULL;
            Attribute vertexAttr;

            JPH::Array<JPH::Vec3> jvertices;

            for (auto const& buf : buffers){
                if (buf.second->getAttribute(AttributeType::POSITION)) {
                    vertexBuffer = buf.second;
                    vertexAttr = *buf.second->getAttribute(AttributeType::POSITION);
                }
            }

            for (size_t i = 0; i < mesh.numSubmeshes; i++) {
                for (auto const& attr : mesh.submeshes[i].attributes){
                    if (attr.first == AttributeType::POSITION){
                        vertexBuffer = buffers[attr.second.getBufferName()];
                        vertexAttr = attr.second;
                    }
                }

                if (vertexAttr.getDataType() != AttributeDataType::FLOAT){
                    Log::error("Cannot create convex hull shape of non float position vertex for 3D Body entity: %u", entity);
                    return -1;
                }

                int verticesize = int(vertexAttr.getCount());
                for (int i = 0; i < verticesize; i++){
                    Vector3 vertice = vertexBuffer->getVector3(&vertexAttr, i) * transform.scale;
                    jvertices.push_back(JPH::Vec3(vertice.x, vertice.y, vertice.z));
                }
            }

            if (jvertices.size() == 0){
                Log::error("Cannot create convex hull shape without vertices for 3D Body entity: %u", entity);
            }else{
                JPH::ConvexHullShapeSettings shape_settings(jvertices);

                return loadShape3D(*body, Vector3::ZERO, Quaternion::IDENTITY, &shape_settings);
            }

        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createMeshShape3D(Entity entity, Vector3 position, Quaternion rotation, std::vector<Vector3> vertices, std::vector<uint16_t> indices){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::MESH;

            JPH::VertexList jvertices;
            jvertices.resize(vertices.size());
            for (int i = 0; i < vertices.size(); i++){
                jvertices[i] = JPH::Float3(vertices[i].x, vertices[i].y, vertices[i].z);
            }

            JPH::IndexedTriangleList jindices;
            int indicesize = int(indices.size() / 3);
            jindices.resize(indicesize);
            for (int i = 0; i < indicesize; i++){
                for (int j = 0; j < 3; j++){
                    jindices[i].mIdx[j] = indices[(3*i)+j];
                }
            }

            JPH::MeshShapeSettings shape_settings(jvertices, jindices);

            return loadShape3D(*body, Vector3::ZERO, Quaternion::IDENTITY, &shape_settings);
        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return -1;
}

int PhysicsSystem::createMeshShape3D(Entity entity, MeshComponent& mesh, Transform& transform){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::MESH;

            std::map<std::string, Buffer*> buffers;

            if (mesh.buffer.getSize() > 0){
                buffers["vertices"] = &mesh.buffer;
            }
            if (mesh.indices.getSize() > 0){
                buffers["indices"] = &mesh.indices;
            }
            for (int i = 0; i < mesh.numExternalBuffers; i++){
                buffers[mesh.eBuffers[i].getName()] = &mesh.eBuffers[i];
            }

            Buffer* indexBuffer = NULL;
            Attribute indexAttr;

            Buffer* vertexBuffer = NULL;
            Attribute vertexAttr;

            for (auto const& buf : buffers){
                if (buf.second->getAttribute(AttributeType::INDEX)){
                    indexBuffer = buf.second;
                    indexAttr = *buf.second->getAttribute(AttributeType::INDEX);
                }
                if (buf.second->getAttribute(AttributeType::POSITION)) {
                    vertexBuffer = buf.second;
                    vertexAttr = *buf.second->getAttribute(AttributeType::POSITION);
                }
            }

            for (size_t i = 0; i < mesh.numSubmeshes; i++) {
                for (auto const& attr : mesh.submeshes[i].attributes){
                    if (attr.first == AttributeType::INDEX){
                        indexBuffer = buffers[attr.second.getBufferName()];
                        indexAttr = attr.second;
                    }
                    if (attr.first == AttributeType::POSITION){
                        vertexBuffer = buffers[attr.second.getBufferName()];
                        vertexAttr = attr.second;
                    }
                }

                if (mesh.submeshes[i].primitiveType != PrimitiveType::TRIANGLES){
                    Log::error("Cannot create mesh shape of non triangles mesh for 3D Body entity: %u", entity);
                    return -1;
                }

                if (vertexAttr.getDataType() != AttributeDataType::FLOAT){
                    Log::error("Cannot create mesh shape of non float position vertex for 3D Body entity: %u", entity);
                    return -1;
                }

                if (indexAttr.getCount() > 0){

                    JPH::IndexedTriangleList jindices;
                    JPH::VertexList jvertices;

                    int numIdxTriangles = int(indexAttr.getCount() / 3);
                    for (int i = 0; i < numIdxTriangles; i++){
                        JPH::uint32 it[3];
                        for (int j = 0; j < 3; j++){
                            uint32_t indice;
                            if (indexAttr.getDataType() == AttributeDataType::UNSIGNED_INT){
                                indice = indexBuffer->getUInt32(&indexAttr, (3*i)+j);
                            }else if (indexAttr.getDataType() == AttributeDataType::UNSIGNED_SHORT){
                                indice = indexBuffer->getUInt16(&indexAttr, (3*i)+j);
                            }
                            it[j] = indice;
                        }
                        jindices.push_back(JPH::IndexedTriangle(it[0], it[1], it[2]));
                    }

                    int verticesize = int(vertexAttr.getCount());
                    for (int i = 0; i < verticesize; i++){
                        Vector3 vertice = vertexBuffer->getVector3(&vertexAttr, i) * transform.scale;
                        jvertices.push_back(JPH::Float3(vertice.x, vertice.y, vertice.z));
                    }

                    if (jindices.size() == 0){
                        Log::error("Cannot create mesh shape without indices for 3D Body entity: %u", entity);
                    }else if (jvertices.size() == 0){
                        Log::error("Cannot create mesh shape without vertices for 3D Body entity: %u", entity);
                    }else{
                        JPH::MeshShapeSettings shape_settings(jvertices, jindices);

                        if (body->numShapes < MAX_SHAPES){
                            loadShape3D(*body, Vector3::ZERO, Quaternion::IDENTITY, &shape_settings);
                        }else{
                            Log::error("Cannot create mesh shape, need to increase MAX_SHAPES for 3D Body entity: %u", entity);
                        }
                    }

                }else{

                    JPH::TriangleList jtriangles;

                    int verticesize = int(vertexAttr.getCount());
                    int numTriangles = verticesize / 3;
                    for (int i = 0; i < numTriangles; i++){
                        JPH::Float3 vt[3];
                        for (int j = 0; j < 3; j++){
                            Vector3 vertice = vertexBuffer->getVector3(&vertexAttr, (3*i)+j) * transform.scale;
                            vt[j] = JPH::Float3(vertice.x, vertice.y, vertice.z);
                        }
                        jtriangles.push_back(JPH::Triangle(vt[0], vt[1], vt[2]));
                    }

                    if (jtriangles.size() == 0){
                        Log::error("Cannot create mesh shape without vertices for 3D Body entity: %u", entity);
                    }else{
                        JPH::MeshShapeSettings shape_settings(jtriangles);

                        if (body->numShapes < MAX_SHAPES){
                            loadShape3D(*body, Vector3::ZERO, Quaternion::IDENTITY, &shape_settings);
                        }else{
                            Log::error("Cannot create mesh shape, need to increase MAX_SHAPES for 3D Body entity: %u", entity);
                        }
                    }

                }
            }

        }else{
            Log::error("Cannot add more shapes in this body, please increase value MAX_SHAPES");
        }
    }

    return (body->numShapes - 1);
}

int PhysicsSystem::createHeightFieldShape3D(Entity entity, TerrainComponent& terrain, unsigned int samplesSize){
    Body3DComponent* body = scene->findComponent<Body3DComponent>(entity);

    if (!terrain.heightMapLoaded){
        Log::error("Cannot create heightfield shape without heightmap image loaded");
        return -1;
    }

    float logsamples = log2(samplesSize);
    if (ceil(logsamples) != floor(logsamples)) {
        Log::error("Cannot create terrain shape. Must insert a power of two samplesSize, not: %u", samplesSize);
        return -1;
    }

    if (body){
        if (body->numShapes < MAX_SHAPES){
            body->shapes[body->numShapes].type = Shape3DType::HEIGHTFIELD;

            TextureData& textureData = terrain.heightMap.getData();

            if (samplesSize == 0){
                samplesSize = textureData.getMinNearestPowerOfTwo();
                if (samplesSize > std::min(textureData.getOriginalWidth(), textureData.getOriginalHeight())){
                    samplesSize = samplesSize / 2;
                }
            }

            JPH::Vec3 terrainOffset = JPH::Vec3(-terrain.terrainSize/2.0, 0.0f ,-terrain.terrainSize/2.0);
            JPH::Vec3 terrainScale = JPH::Vec3(terrain.terrainSize/samplesSize, terrain.maxHeight, terrain.terrainSize/samplesSize);

            float *samples = new float [samplesSize * samplesSize];

            for (int x = 0; x < samplesSize; x++){
                for (int y = 0; y < samplesSize; y++){
                    TextureData& textureData = terrain.heightMap.getData();

                    int posX = floor(textureData.getWidth() * x / samplesSize);
                    int posY = floor(textureData.getHeight() * y / samplesSize);

                    float val = textureData.getColorComponent(posX, posY, 0) / 255.0f;

                    samples[x + (y * samplesSize)] = val;
                }
            }

            JPH::HeightFieldShapeSettings shape_settings(samples, terrainOffset, terrainScale, samplesSize);

            int shapeIndex = loadShape3D(*body, Vector3::ZERO, Quaternion::IDENTITY, &shape_settings);

            delete[] samples;
            return shapeIndex;
        }
    }

    return -1;
}

b2WorldId PhysicsSystem::getWorld2D() const{
     return world2D;
}

JPH::PhysicsSystem* PhysicsSystem::getWorld3D(){
    return &world3D;
}

b2BodyId PhysicsSystem::getBody(Entity entity){
    Body2DComponent* body = scene->findComponent<Body2DComponent>(entity);

    if (body){
        return body->body;
    }

    return b2_nullBodyId;
}

bool PhysicsSystem::loadBody2D(Entity entity){
    Body2DComponent& body = scene->getComponent<Body2DComponent>(entity);

    if (!b2Body_IsValid(body.body)){
        b2BodyDef bodyDef = b2DefaultBodyDef();

        if (body.type == BodyType::STATIC){
            bodyDef.type = b2_staticBody;
        }else if (body.type == BodyType::KINEMATIC){
            bodyDef.type = b2_kinematicBody;
        }else{
            bodyDef.type = b2_dynamicBody;
        }
        bodyDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        body.body = b2CreateBody(world2D, &bodyDef);
        body.newBody = true;

        return true;
    }

    return false;
}

void PhysicsSystem::destroyBody2D(Body2DComponent& body){
    // When a world leaves scope or is deleted by calling delete on a pointer
    // all the memory reserved for bodies, fixtures, and joints is freed
    if (b2Body_IsValid(body.body)){
        for (int i = 0; i < body.numShapes; i++){
            destroyShape2D(body, i);
        }
        body.numShapes = 0;

        b2DestroyBody(body.body);

        body.body = b2_nullBodyId;
    }
}

bool PhysicsSystem::loadBody3D(Entity entity){
    Body3DComponent& body = scene->getComponent<Body3DComponent>(entity);

    if (body.body.IsInvalid()){

        for (int i = 0; i < body.numShapes; i++){
            if (body.type != BodyType::STATIC){
                if (body.shapes[i].type == Shape3DType::HEIGHTFIELD){
                    Log::error("Heightfield body should be static for 3D Body entity: %u", entity);
                    return false;
                }
                if (body.shapes[i].type == Shape3DType::MESH && !body.overrideMassProperties){
                    Log::error("Mesh body should be static or with mass and inertia overridden for 3D Body entity: %u", entity);
                    return false;
                }
            }
        }

        if (body.numShapes > 1){

            JPH::StaticCompoundShapeSettings compound_shape;
            for (int i = 0; i < body.numShapes; i++){
                JPH::Vec3 jPosition(body.shapes[i].position.x, body.shapes[i].position.y, body.shapes[i].position.z);
                JPH::Quat jQuat(body.shapes[i].rotation.x, body.shapes[i].rotation.y, body.shapes[i].rotation.z, body.shapes[i].rotation.w);

                compound_shape.AddShape(jPosition, jQuat, body.shapes[i].shape);
            }

            JPH::ShapeSettings::ShapeResult shape_result = compound_shape.Create();

            if (shape_result.IsValid()){
                JPH::ShapeRefC shape = shape_result.Get();

                createGenericJoltBody(entity, body, shape);
            }else{
                Log::error("Cannot create StaticCompoundShape for 3D Body entity: %u", entity);
                return false;
            }

        }else if (body.numShapes == 1){

            if (body.shapes[0].position != Vector3::ZERO || body.shapes[0].rotation != Quaternion::IDENTITY){

                JPH::Vec3 jPosition(body.shapes[0].position.x, body.shapes[0].position.y, body.shapes[0].position.z);
                JPH::Quat jQuat(body.shapes[0].rotation.x, body.shapes[0].rotation.y, body.shapes[0].rotation.z, body.shapes[0].rotation.w);

                JPH::RotatedTranslatedShapeSettings rtShape(jPosition, jQuat, body.shapes[0].shape);

                JPH::ShapeSettings::ShapeResult shape_result = rtShape.Create();

                if (shape_result.IsValid()){
                    JPH::ShapeRefC shape = shape_result.Get();

                    createGenericJoltBody(entity, body, shape);
                }else{
                    Log::error("Cannot create RotatedTranslatedShapeSettings for 3D Body entity: %u", entity);
                    return false;
                }

            }else{

                createGenericJoltBody(entity, body, body.shapes[0].shape);
            }

        }else{
            Log::error("Cannot load body without shapes, entity: %u", entity);
            return false;
        }

    }

    body.newBody = true;
    return true;
}

void PhysicsSystem::destroyBody3D(Body3DComponent& body){
    if (!body.body.IsInvalid()){
        JPH::BodyInterface &body_interface = world3D.GetBodyInterface();
        body_interface.RemoveBody(body.body);

        for (int i = 0; i < body.numShapes; i++){
            destroyShape3D(body, i);
        }
        body.numShapes = 0;
    }
}

int PhysicsSystem::loadShape2D(Body2DComponent& body, void* shape, Shape2DType type){
    if (!b2Shape_IsValid(body.shapes[body.numShapes].shape)){
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.userData = reinterpret_cast<void*>(body.numShapes);

        if (!b2Body_IsValid(body.body)){
            Log::error("Cannot create 2D body shape without loaded body");
            return -1;
        }

        body.shapes[body.numShapes].shape = b2_nullShapeId;
        body.shapes[body.numShapes].chain = b2_nullChainId;

        if (type == Shape2DType::POLYGON){
            body.shapes[body.numShapes].shape = b2CreatePolygonShape(body.body, &shapeDef, (b2Polygon*)shape);
        }else if (type == Shape2DType::CIRCLE){
            body.shapes[body.numShapes].shape = b2CreateCircleShape(body.body, &shapeDef, (b2Circle*)shape);
        }else if (type == Shape2DType::CAPSULE){
            body.shapes[body.numShapes].shape = b2CreateCapsuleShape(body.body, &shapeDef, (b2Capsule*)shape);
        }else if (type == Shape2DType::SEGMENT){
            body.shapes[body.numShapes].shape = b2CreateSegmentShape(body.body, &shapeDef, (b2Segment*)shape);
        }else if (type == Shape2DType::CHAIN){
            body.shapes[body.numShapes].chain = b2CreateChain(body.body, (b2ChainDef*)shape);
        }

        body.numShapes++;

        return (body.numShapes - 1);
    }

    return -1;
}

void PhysicsSystem::destroyShape2D(Body2DComponent& body, size_t index){
    if (b2Shape_IsValid(body.shapes[index].shape)){
        b2DestroyShape(body.shapes[index].shape, true);

        body.shapes[index].shape = b2_nullShapeId;
    }
    if (b2Chain_IsValid(body.shapes[index].chain)){
        b2DestroyChain(body.shapes[index].chain);

        body.shapes[index].chain = b2_nullChainId;
    }
}

int PhysicsSystem::loadShape3D(Body3DComponent& body, const Vector3& position, const Quaternion& rotation, JPH::ShapeSettings* shapeSettings){
    JPH::ShapeSettings::ShapeResult shape_result = shapeSettings->Create();
    if (shape_result.IsValid()){
        JPH::Shape* shape = shape_result.Get();
        shape->SetEmbedded();
        shape->SetUserData(body.numShapes);

        body.shapes[body.numShapes].shape = shape;
        body.shapes[body.numShapes].position = position;
        body.shapes[body.numShapes].rotation = rotation;

        body.numShapes++;

        return (body.numShapes - 1);
    }else{
        Log::error("Cannot create shape for 3D Body");
    }

    return -1;
}

void PhysicsSystem::destroyShape3D(Body3DComponent& body, size_t index){
    if (body.shapes[index].shape){
        body.shapes[index].shape->Release();

        body.shapes[index].shape = NULL;
    }
}

bool PhysicsSystem::loadDistanceJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchorA, Vector2 anchorB, bool rope){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivotA = {anchorA.x / pointsToMeterScale2D, anchorA.y / pointsToMeterScale2D};
        b2Vec2 worldPivotB = {anchorB.x / pointsToMeterScale2D, anchorB.y / pointsToMeterScale2D};

        b2DistanceJointDef jointDef = b2DefaultDistanceJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivotA);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivotB);
        jointDef.length = b2Distance(worldPivotA, worldPivotB);
        if (rope){
            jointDef.minLength = 0;
        }
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateDistanceJoint(world2D, &jointDef);
        joint.type = Joint2DType::DISTANCE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadRevoluteJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};

        b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateRevoluteJoint(world2D, &jointDef);
        joint.type = Joint2DType::REVOLUTE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPrismaticJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};
        b2Vec2 worldAxis = {axis.x, axis.y};

        b2PrismaticJointDef jointDef = b2DefaultPrismaticJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.localAxisA = b2Body_GetLocalVector(myBodyA.body, worldAxis);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreatePrismaticJoint(world2D, &jointDef);
        joint.type = Joint2DType::PRISMATIC;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadMouseJoint2D(Entity entity, Joint2DComponent& joint, Entity body, Vector2 target){
    Signature signature = scene->getSignature(body);

    if (signature.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBody = scene->getComponent<Body2DComponent>(body);

        updateBody2DPosition(signature, body, myBody);

        b2Vec2 worldTarget = {target.x / pointsToMeterScale2D, target.y / pointsToMeterScale2D};

        b2MouseJointDef jointDef = b2DefaultMouseJointDef();
        jointDef.bodyIdA = myBody.body;
        jointDef.bodyIdB = myBody.body;
        jointDef.target = worldTarget;
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateMouseJoint(world2D, &jointDef);
        joint.type = Joint2DType::MOUSE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadWheelJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor, Vector2 axis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};
        b2Vec2 worldAxis = {axis.x, axis.y};

        b2WheelJointDef jointDef = b2DefaultWheelJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.localAxisA = b2Body_GetLocalVector(myBodyA.body, worldAxis);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateWheelJoint(world2D, &jointDef);
        joint.type = Joint2DType::WHEEL;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadWeldJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB, Vector2 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2Vec2 worldPivot = {anchor.x / pointsToMeterScale2D, anchor.y / pointsToMeterScale2D};

        b2WeldJointDef jointDef = b2DefaultWeldJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.localAnchorA = b2Body_GetLocalPoint(myBodyA.body, worldPivot);
        jointDef.localAnchorB = b2Body_GetLocalPoint(myBodyB.body, worldPivot);
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateWeldJoint(world2D, &jointDef);
        joint.type = Joint2DType::WELD;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadMotorJoint2D(Entity entity, Joint2DComponent& joint, Entity bodyA, Entity bodyB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body2DComponent>()) && signatureB.test(scene->getComponentId<Body2DComponent>())){

        Body2DComponent myBodyA = scene->getComponent<Body2DComponent>(bodyA);
        Body2DComponent myBodyB = scene->getComponent<Body2DComponent>(bodyB);

        updateBody2DPosition(signatureA, bodyA, myBodyA);
        updateBody2DPosition(signatureB, bodyB, myBodyB);

        b2MotorJointDef jointDef = b2DefaultMotorJointDef();
        jointDef.bodyIdA = myBodyA.body;
        jointDef.bodyIdB = myBodyB.body;
        jointDef.userData = reinterpret_cast<void*>((uint64_t)entity);

        joint.joint = b2CreateMotorJoint(world2D, &jointDef);
        joint.type = Joint2DType::MOTOR;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

void PhysicsSystem::destroyJoint2D(Joint2DComponent& joint){
    if (b2Joint_IsValid(joint.joint)){
        b2DestroyJoint(joint.joint);
        joint.joint = b2_nullJointId;
    }
}

bool PhysicsSystem::loadFixedJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::FixedConstraintSettings settings;
        settings.mAutoDetectPoint = true;

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::FIXED;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadDistanceJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::DistanceConstraintSettings settings;
        settings.mPoint1 = JPH::Vec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mPoint2 = JPH::Vec3(anchorB.x, anchorB.y, anchorB.z);
        //settings.mMinDistance = 4.0f;
        //settings.mMaxDistance = 8.0f;

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::DISTANCE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPointJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::PointConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::POINT;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadHingeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 axis, Vector3 normal){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::HingeConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mHingeAxis1 = settings.mHingeAxis2 = JPH::Vec3(axis.x, axis.y, axis.z);
        settings.mNormalAxis1 = settings.mNormalAxis2 = JPH::Vec3(normal.x, normal.y, normal.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::HINGE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadConeJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::ConeConstraintSettings settings;
        settings.mPoint1 = settings.mPoint2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mTwistAxis1 = settings.mTwistAxis2 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::CONE;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPrismaticJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 sliderAxis, float limitsMin, float limitsMax){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::SliderConstraintSettings settings;
        settings.mAutoDetectPoint = true;
        settings.SetSliderAxis(JPH::Vec3(sliderAxis.x, sliderAxis.y, sliderAxis.z));
		settings.mLimitsMin = limitsMin;
		settings.mLimitsMax = limitsMax;

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PRISMATIC;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadSwingTwistJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchor, Vector3 twistAxis, Vector3 planeAxis, float normalHalfConeAngle, float planeHalfConeAngle, float twistMinAngle, float twistMaxAngle){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::SwingTwistConstraintSettings settings;
        settings.mPosition1 = settings.mPosition2 = JPH::Vec3(anchor.x, anchor.y, anchor.z);
        settings.mTwistAxis1 = settings.mTwistAxis2 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);
        settings.mPlaneAxis1 = settings.mPlaneAxis2 = JPH::Vec3(planeAxis.x, planeAxis.y, planeAxis.z);
        settings.mNormalHalfConeAngle = Angle::defaultToRad(normalHalfConeAngle);
        settings.mPlaneHalfConeAngle = Angle::defaultToRad(planeHalfConeAngle);
        settings.mTwistMinAngle = Angle::defaultToRad(twistMinAngle);
        settings.mTwistMaxAngle = Angle::defaultToRad(twistMaxAngle);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::SWINGTWIST;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadSixDOFJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 axisX, Vector3 axisY){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::SixDOFConstraintSettings settings;
        settings.mPosition1 = JPH::Vec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mPosition2 = JPH::Vec3(anchorB.x, anchorB.y, anchorB.z);
        settings.mAxisX1 = settings.mAxisX2 = JPH::Vec3(axisX.x, axisX.y, axisX.z);
        settings.mAxisY1 = settings.mAxisY2 = JPH::Vec3(axisY.x, axisY.y, axisY.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::SIXDOF;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPathJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, std::vector<Vector3> positions, std::vector<Vector3> tangents, std::vector<Vector3> normals, Vector3 pathPosition, bool isLooping){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::Ref<JPH::PathConstraintPathHermite> path = new JPH::PathConstraintPathHermite;

        if (positions.size() != normals.size() != tangents.size()){
            Log::error("Cannot create joint, positions size is different from normals and tangents");
            return false;
        }

        for (int i = 0; i < positions.size(); i++){
            path->AddPoint(JPH::Vec3(positions[i].x, positions[i].y, positions[i].z), JPH::Vec3(tangents[i].x, tangents[i].y, tangents[i].z), JPH::Vec3(normals[i].x, normals[i].y, normals[i].z));
        }
        path->SetIsLooping(isLooping);

        JPH::PathConstraintSettings settings;
        settings.mPath = path;
        settings.mPathPosition = JPH::Vec3(pathPosition.x, pathPosition.y, pathPosition.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PATH;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadGearJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hingeA, Entity hingeB, int numTeethGearA, int numTeethGearB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);
    Signature signatureHingeA = scene->getSignature(hingeA);
    Signature signatureHingeB = scene->getSignature(hingeB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        if (signatureHingeA.test(scene->getComponentId<Joint3DComponent>()) && signatureHingeB.test(scene->getComponentId<Joint3DComponent>())){

            Joint3DComponent myHingeA = scene->getComponent<Joint3DComponent>(hingeA);
            Joint3DComponent myHingeB = scene->getComponent<Joint3DComponent>(hingeB);

            if ((myHingeA.type != Joint3DType::HINGE) || (myHingeB.type != Joint3DType::HINGE)){
                Log::error("Cannot create joint, hingeA or hingeB is not hinge type");
                return false;
            }

            const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
            JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
            JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

            // Disable collision between gears
            JPH::Ref<JPH::GroupFilterTable> group_filter = new JPH::GroupFilterTable(2);
            group_filter->DisableCollision(0, 1);
            lock.GetBody(0)->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 0));
            lock.GetBody(1)->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 1));

            JPH::HingeConstraintSettings* hingeSetA = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHingeA.joint)->GetConstraintSettings().GetPtr();
            JPH::HingeConstraintSettings* hingeSetB = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHingeB.joint)->GetConstraintSettings().GetPtr();

            JPH::GearConstraintSettings settings;
            settings.mHingeAxis1 = hingeSetA->mHingeAxis1;
            settings.mHingeAxis2 = hingeSetB->mHingeAxis1;
            settings.SetRatio(numTeethGearA, numTeethGearB);

            joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

            ((JPH::GearConstraint *)joint.joint)->SetConstraints(myHingeA.joint, myHingeB.joint);
            world3D.AddConstraint(joint.joint);
            joint.type = Joint3DType::GEAR;
        }else{
            Log::error("Cannot create joint, error in hingeA or hingeB");
            return false;
        }

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadRackAndPinionJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Entity hinge, Entity slider, int numTeethRack, int numTeethGear, int rackLength){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);
    Signature signatureHinge = scene->getSignature(hinge);
    Signature signatureSlider = scene->getSignature(slider);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        if (signatureHinge.test(scene->getComponentId<Joint3DComponent>()) && signatureSlider.test(scene->getComponentId<Joint3DComponent>())){

            Joint3DComponent myHinge = scene->getComponent<Joint3DComponent>(hinge);
            Joint3DComponent mySlider = scene->getComponent<Joint3DComponent>(slider);

            if ((myHinge.type != Joint3DType::HINGE) || (mySlider.type != Joint3DType::PRISMATIC)){
                Log::error("Cannot create joint, hinge or slider is not hinge and prismatic (slider) types");
                return false;
            }

            const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
            JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
            JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

            // Disable collision between gears
            JPH::Ref<JPH::GroupFilterTable> group_filter = new JPH::GroupFilterTable(2);
            group_filter->DisableCollision(0, 1);
            lock.GetBody(0)->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 0));
            lock.GetBody(1)->SetCollisionGroup(JPH::CollisionGroup(group_filter, 0, 1));

            JPH::HingeConstraintSettings* hingeSetA = (JPH::HingeConstraintSettings*)((JPH::HingeConstraint*)myHinge.joint)->GetConstraintSettings().GetPtr();
            JPH::SliderConstraintSettings* sliderSetB = (JPH::SliderConstraintSettings*)((JPH::SliderConstraint*)mySlider.joint)->GetConstraintSettings().GetPtr();

            JPH::RackAndPinionConstraintSettings settings;
            settings.mHingeAxis = hingeSetA->mHingeAxis1;
            settings.mSliderAxis = sliderSetB->mSliderAxis2;
            settings.SetRatio(numTeethRack, rackLength, numTeethGear);

            joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

            ((JPH::GearConstraint *)joint.joint)->SetConstraints(myHinge.joint, mySlider.joint);
            world3D.AddConstraint(joint.joint);
            joint.type = Joint3DType::RACKANDPINON;
        }else{
            Log::error("Cannot create joint, error in hinge or slider");
            return false;
        }

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

bool PhysicsSystem::loadPulleyJoint3D(Joint3DComponent& joint, Entity bodyA, Entity bodyB, Vector3 anchorA, Vector3 anchorB, Vector3 fixedPointA, Vector3 fixedPointB){
    Signature signatureA = scene->getSignature(bodyA);
    Signature signatureB = scene->getSignature(bodyB);

    if (signatureA.test(scene->getComponentId<Body3DComponent>()) && signatureB.test(scene->getComponentId<Body3DComponent>())){

        Body3DComponent myBodyA = scene->getComponent<Body3DComponent>(bodyA);
        Body3DComponent myBodyB = scene->getComponent<Body3DComponent>(bodyB);

        updateBody3DPosition(signatureA, bodyA, myBodyA);
        updateBody3DPosition(signatureB, bodyB, myBodyB);

        JPH::PulleyConstraintSettings settings;
        settings.mBodyPoint1 = JPH::Vec3(anchorA.x, anchorA.y, anchorA.z);
        settings.mBodyPoint2 = JPH::Vec3(anchorB.x, anchorB.y, anchorB.z);
        settings.mFixedPoint1 = JPH::Vec3(fixedPointA.x, fixedPointA.y, fixedPointA.z);
        settings.mFixedPoint2 = JPH::Vec3(fixedPointB.x, fixedPointB.y, fixedPointB.z);

        const JPH::BodyLockInterface& bodyLockInterface = lock3DBodies? static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterface()) : static_cast<const JPH::BodyLockInterface &>(world3D.GetBodyLockInterfaceNoLock());
        JPH::BodyID bodies[2] = {myBodyA.body, myBodyB.body};
        JPH::BodyLockMultiWrite lock(bodyLockInterface, bodies, 2);

        joint.joint = settings.Create(*lock.GetBody(0), *lock.GetBody(1));

        world3D.AddConstraint(joint.joint);
        joint.type = Joint3DType::PULLEY;

    }else{
        Log::error("Cannot create joint, error in bodyA or bodyB");
        return false;
    }

    return true;
}

void PhysicsSystem::destroyJoint3D(Joint3DComponent& joint){
    if (joint.joint){
        world3D.RemoveConstraint(joint.joint);

        joint.joint = NULL;
    }
}

void PhysicsSystem::addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude){
    addBroadPhaseLayer3D(index, groupsToInclude, 0);
}

void PhysicsSystem::addBroadPhaseLayer3D(uint8_t index, uint32_t groupsToInclude, uint32_t groupsToExclude){
    if (index >= 0 && index < MAX_BROADPHASELAYER_3D){
        broad_phase_layer_interface->ConfigureLayer(JPH::BroadPhaseLayer(index), groupsToInclude, groupsToExclude);

        auto bodies3d = scene->getComponentArray<Body3DComponent>();
        if (bodies3d->size() > 0){
            Log::warn("BroadPhaseLayers should be add before any 3D body creation");
        }
    }else{
        Log::error("BroadPhaseLayer index should be from 0 to %i, increase MAX_BROADPHASELAYER_3D to more layers", (MAX_BROADPHASELAYER_3D-1));
    }
}

void PhysicsSystem::load(){
}

void PhysicsSystem::destroy(){

}

void PhysicsSystem::update(double dt){
    if (paused) {
        return;
    }

	auto bodies2d = scene->getComponentArray<Body2DComponent>();

	for (int i = 0; i < bodies2d->size(); i++){
		Body2DComponent& body = bodies2d->getComponentFromIndex(i);
		Entity entity = bodies2d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (b2Body_IsValid(body.body)){
            updateBody2DPosition(signature, entity, body);

            body.newBody = false;
        }
    }

    if (bodies2d->size() > 0){
        int32_t subSteps = 4;
        b2World_Step(world2D, dt, subSteps);
    }

    b2BodyEvents events = b2World_GetBodyEvents(world2D);
    for (int i = 0; i < events.moveCount; ++i){
        const b2BodyMoveEvent* event = events.moveEvents + i;

        Entity entity = reinterpret_cast<uintptr_t>(event->userData);
        Signature signature = scene->getSignature(entity);

        b2Transform bTransform = event->transform;
        if (signature.test(scene->getComponentId<Transform>())){
            Transform& transform = scene->getComponent<Transform>(entity);

            Vector3 nPosition = Vector3(bTransform.p.x * pointsToMeterScale2D, bTransform.p.y * pointsToMeterScale2D, transform.worldPosition.z);
            Quaternion nRotation = Quaternion(Angle::radToDefault(b2Rot_GetAngle(bTransform.q)), Vector3(0, 0, 1));

            if (transform.parent != NULL_ENTITY){
                Transform& transformParent = scene->getComponent<Transform>(transform.parent);

                nPosition = transformParent.modelMatrix.inverse() * nPosition;
                nRotation = transformParent.worldRotation.inverse() * nRotation;
            }

            if (transform.position != nPosition){
                transform.position = nPosition;
                transform.needUpdate = true;
            }

            if (transform.rotation != nRotation){
                transform.rotation = nRotation;
                transform.needUpdate = true;
            }
        }
    }

    Box2DAux::manageEvents(scene, world2D);

    auto bodies3d = scene->getComponentArray<Body3DComponent>();

	for (int i = 0; i < bodies3d->size(); i++){
		Body3DComponent& body = bodies3d->getComponentFromIndex(i);
		Entity entity = bodies3d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (!body.body.IsInvalid()){
            updateBody3DPosition(signature, entity, body);

            body.newBody = false;
        }
    }

    if (bodies3d->size() > 0){
		const int cCollisionSteps = 1;

		world3D.Update(dt, cCollisionSteps, temp_allocator, job_system);
	}

	for (int i = 0; i < bodies3d->size(); i++){
		Body3DComponent& body = bodies3d->getComponentFromIndex(i);
		Entity entity = bodies3d->getEntity(i);
		Signature signature = scene->getSignature(entity);

        if (!body.body.IsInvalid()){
            JPH::BodyInterface &body_interface = world3D.GetBodyInterfaceNoLock();
            JPH::RVec3 position = body_interface.GetPosition(body.body);
            JPH::Quat rotation = body_interface.GetRotation(body.body);
            if (signature.test(scene->getComponentId<Transform>())){
                Transform& transform = scene->getComponent<Transform>(entity);

                if (!std::isnan(position.GetX()) && !std::isnan(position.GetY()) && !std::isnan(position.GetZ())){
                    Vector3 nPosition = Vector3(position.GetX(), position.GetY(), position.GetZ());
                    Quaternion nRotation = Quaternion(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());

                    if (transform.parent != NULL_ENTITY){
                        Transform& transformParent = scene->getComponent<Transform>(transform.parent);

                        nPosition = transformParent.modelMatrix.inverse() * nPosition;
                        nRotation = transformParent.worldRotation.inverse() * nRotation;
                    }

                    if (transform.position != nPosition){
                        transform.position = nPosition;
                        transform.needUpdate = true;
                    }

                    if (transform.rotation != nRotation){
                        transform.rotation = nRotation;
                        transform.needUpdate = true;
                    }
                }

            }
        }
    }
    
}

void PhysicsSystem::draw(){

}

void PhysicsSystem::onComponentAdded(Entity entity, ComponentId componentId) {

}

void PhysicsSystem::onComponentRemoved(Entity entity, ComponentId componentId) {
	if (componentId == scene->getComponentId<Body2DComponent>()) {
		Body2DComponent& body2d = scene->getComponent<Body2DComponent>(entity);
		destroyBody2D(body2d);
	} else if (componentId == scene->getComponentId<Body3DComponent>()) {
		Body3DComponent& body3d = scene->getComponent<Body3DComponent>(entity);
		destroyBody3D(body3d);
	} else if (componentId == scene->getComponentId<Joint2DComponent>()) {
		Joint2DComponent& joint2d = scene->getComponent<Joint2DComponent>(entity);
		destroyJoint2D(joint2d);
	} else if (componentId == scene->getComponentId<Joint3DComponent>()) {
		Joint3DComponent& joint3d = scene->getComponent<Joint3DComponent>(entity);
		destroyJoint3D(joint3d);
	}
}