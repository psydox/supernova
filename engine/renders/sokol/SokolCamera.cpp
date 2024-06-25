//
// (c) 2024 Eduardo Doria.
//

#include "SokolCamera.h"

#include "System.h"
#include "SokolCmdQueue.h"

#include "sokol_gfx.h"

using namespace Supernova;

SokolCamera::SokolCamera(){
    pass = {0};
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
}

SokolCamera::SokolCamera(const SokolCamera& rhs) : pass(rhs.pass) {}

SokolCamera& SokolCamera::operator=(const SokolCamera& rhs) { 
    pass = rhs.pass; 
    return *this;
}

SokolCamera::~SokolCamera(){

}

void SokolCamera::setClearColor(Vector4 clearColor){
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
}

void SokolCamera::startFrameBuffer(FramebufferRender* framebuffer, size_t face){
    //sg_pass pass = {0};
    //pass.action = pass_action;
    pass.attachments = framebuffer->backend.get(face);
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::startFrameBuffer(){
    //sg_pass pass = {0};
    //pass.action = pass_action;
    pass.swapchain = System::instance().getSokolSwapchain();
    //SokolCmdQueue::add_command_begin_pass(pass);
    sg_begin_pass(pass);
}

void SokolCamera::applyViewport(Rect rect){
    //SokolCmdQueue::add_command_apply_viewport((int)rect.getX(), (int)rect.getY(), (int)rect.getWidth(), (int)rect.getHeight(), false);
    sg_apply_viewport((int)rect.getX(), (int)rect.getY(), (int)rect.getWidth(), (int)rect.getHeight(), false);
}

void SokolCamera::applyScissor(Rect rect){
    //SokolCmdQueue::add_command_apply_scissor_rect((int)rect.getX(), (int)rect.getY(), (int)rect.getWidth(), (int)rect.getHeight(), false);
    sg_apply_scissor_rect((int)rect.getX(), (int)rect.getY(), (int)rect.getWidth(), (int)rect.getHeight(), false);
}

void SokolCamera::endFrameBuffer(){
    //SokolCmdQueue::add_command_end_pass();
    sg_end_pass();
}