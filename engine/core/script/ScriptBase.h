//
// (c) 2025 Eduardo Doria.
//

#ifndef SCRIPT_BASE_H
#define SCRIPT_BASE_H

#include "Scene.h"
#include "Entity.h"

namespace Supernova {

    class SUPERNOVA_API ScriptBase {
    protected:
        Scene* scene;
        Entity entity;

    public:
        ScriptBase(Scene* scene, Entity entity);
        virtual ~ScriptBase();

        Scene* getScene() const;
        Entity getEntity() const;
    };

}

#endif //SCRIPT_BASE_H