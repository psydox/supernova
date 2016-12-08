#ifndef SceneRender_h
#define SceneRender_h

class SceneRender {
    
public:
    
    inline virtual ~SceneRender(){}
    
    virtual void updateLights(std::vector<Light*> lights) = 0;
    virtual void setAmbientLight(Vector3 ambientLight) = 0;

    virtual bool load() = 0;
    virtual bool draw() = 0;
    virtual bool viewSize(int x, int y, int width, int height) = 0;
    
};

#endif /* SceneRender_h */